/**
 * @file flash_persistence.c
 * @brief Flash persistence implementation with 4-block ring buffer strategy
 * 
 * Implements flash persistence using RP2350 partition table access and
 * hardware SHA256 integrity verification as documented in ADR-006.
 * 
 * Documentation Reference:
 * - ADR-006: Flash Persistence Strategy - Detailed Implementation Strategy
 * - arc42 Chapter 5 - Configuration Manager - Flash Ring Buffer Persistence
 */
#include "flash_persistence.h"
#include "shared_memory.h"
#include "factory_defaults.h"
#include "device_mode.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include "pico/sha256.h"
#include <pico/flash.h>
#include <stdio.h>
#include "pico/bootrom.h"
#include "boot/picobin.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "utils/uf2_family_ids.h"
#include <string.h>
#include "debug.h"

#define PART_LOC_FIRST(x) ( ((x) & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB )
#define PART_LOC_LAST(x)  ( ((x) & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)  >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB )

#define PARTITION_LOCATION_AND_FLAGS_SIZE  2
#define PARTITION_ID_SIZE                  2
#define PARTITION_NAME_MAX                 127  // name length is indicated by 7 bits
#define PARTITION_TABLE_FIXED_INFO_SIZE    (4 + PARTITION_TABLE_MAX_PARTITIONS * (PARTITION_LOCATION_AND_FLAGS_SIZE + PARTITION_ID_SIZE))

/*
 * Stores partition table information and data read status
 */
typedef struct {
    uint32_t table[PARTITION_TABLE_FIXED_INFO_SIZE];
    uint32_t fields;
    bool has_partition_table;
    int partition_count;
    uint32_t unpartitioned_space_first_sector;
    uint32_t unpartitioned_space_last_sector;
    uint32_t flags_and_permissions;
    int current_partition;
    size_t pos;
    int status;
} pico_partition_table_t;

/*
 * Stores information on each partition
 */
typedef struct {
    uint32_t first_sector;
    uint32_t last_sector;
    uint32_t flags_and_permissions;
    bool has_id;
    uint64_t partition_id;
    bool has_name;
    char name[PARTITION_NAME_MAX + 1];
    uint32_t extra_family_id_count;
    uint32_t extra_family_ids[PARTITION_EXTRA_FAMILY_ID_MAX];
} pico_partition_t;

// Static state - persistent across function calls
static flash_persistence_state_t g_flash_state = {0};
static flash_persistence_block_t shadow_block_copy = {0};   //allocate only once, not on stack (stack is only 2k)

// Function prototypes for internal implementation
static bool read_flash_block(uint32_t block_index, flash_persistence_block_t* block_data);
static bool write_flash_block(uint32_t block_index, const flash_persistence_block_t* block_data);
static bool validate_block_integrity(const flash_persistence_block_t* block_data);
static int find_best_valid_block(void);
static void advance_ring_buffer_position(void);
static void call_flash_range_erase(void *param);
static void call_flash_range_program(void *param);
static bool read_next_partition(pico_partition_table_t *pt, pico_partition_t *p); 
static int read_partition_table(pico_partition_table_t *pt);

/**
 * Initialize flash persistence system
 * 
 * @return true if initialization successful, false otherwise
 */
bool flash_persistence_init(void) {
    if (g_flash_state.initialized) {
        return true;  // Already initialized
    }
    
    // Use the single configuration partition (ID=2)
    g_flash_state.active_config_partition_id = FLASH_PARTITION_CONFIGURATION_DATA;
    
    // Find the active configuration partition
    if (!flash_find_partition_info(g_flash_state.active_config_partition_id, 
                                    &g_flash_state.partition_start_offset, 
                                    &g_flash_state.partition_size)) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_PARTITION_ID, 
                  g_flash_state.active_config_partition_id);
        return false;
    }

    // Log the found partition details
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_FLASH_PARTITION_ID, 
              g_flash_state.active_config_partition_id);
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_FLASH_PARTITION_OFFSET, 
              g_flash_state.partition_start_offset);
    
    // Verify partition size is sufficient for 8-block ring buffer
    uint32_t required_size = FLASH_PERSISTENCE_RING_SIZE * FLASH_PERSISTENCE_BLOCK_SIZE;
    if (g_flash_state.partition_size < required_size) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_INIT, required_size);
        return false;
    }
    
    // Initialize ring buffer state
    g_flash_state.current_write_block = 0;
    g_flash_state.last_valid_block = 0;
    g_flash_state.total_writes_lifetime = 0;
    g_flash_state.corruption_events = 0;
    g_flash_state.last_written_revision = 0;
    g_flash_state.last_write_timestamp_ms = 0;
    g_flash_state.initialized = true;
    g_flash_state.write_in_progress = false;
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_FLASH_INIT, 
              g_flash_state.active_config_partition_id);
    
    return true;
}

/**
 * Load configuration from flash during system startup
 * 
 * @return true if configuration loaded successfully or factory reset applied
 */
bool flash_persistence_load_configuration(void) {
    if (!g_flash_state.initialized) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_INIT, 0);
        return false;
    }
    
    // Find the best valid block among all ring buffer blocks
    int best_block = find_best_valid_block();
    
    if (best_block < 0 || factory_reset_needed() ) {
        // No valid blocks found - trigger factory reset
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_WARN, LOG_EVENT_FACTORY_RESET, 0);
        flash_persistence_factory_reset();
        
        // Log factory reset event as first entry
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_FACTORY_RESET, 0);
        
        return true;  // Factory reset successful
    }
    
    // Load configuration from best valid block
    flash_persistence_block_t block_data;
    if (!read_flash_block(best_block, &block_data)) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_READ, best_block);
        flash_persistence_factory_reset();
        return true;  // Fallback to factory reset
    }
    
    // Copy loaded configuration to shared memory
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (layout) {
        memcpy(layout, &block_data.shared_memory_data, sizeof(shared_memory_layout_t));
        g_flash_state.last_written_revision = layout->revision_counter;
        g_flash_state.last_valid_block = best_block;
        
        // Clear runtime flags that should not be persisted
        layout->config_change_pending = false;  // This is a runtime flag, not config
        
        // Enforce device mode channel restrictions
        // Disable channels that are not available in current device mode
#if !DEVICE_CHANNEL_1_ENABLED
        layout->config.channels[CHANNEL_1].enabled = false;
#endif
#if !DEVICE_CHANNEL_2_ENABLED
        layout->config.channels[CHANNEL_2].enabled = false;
#endif
#if !DEVICE_CHANNEL_3_ENABLED
        layout->config.channels[CHANNEL_3].enabled = false;
#endif
#if !DEVICE_CHANNEL_4_ENABLED
        layout->config.channels[CHANNEL_4].enabled = false;
#endif
        
        // Validate and fix channel TCP ports (may be garbage from old flash data)
        for (int ch = CHANNEL_0; ch < CHANNEL_MAX; ch++) {
            if (layout->config.channels[ch].tcp_port < 1024 || 
                layout->config.channels[ch].tcp_port > 65535) {
                layout->config.channels[ch].tcp_port = 4001 + ch;
                printf("Flash: Fixed invalid port for channel %d -> %d\n", 
                       ch, layout->config.channels[ch].tcp_port);
            }
        }
        
        // ALWAYS enforce GPIO pins from device_mode.h (hardware-specific, not configurable)
        layout->config.channels[CHANNEL_0].tx_gpio = DEVICE_UART0_TX_GPIO;
        layout->config.channels[CHANNEL_0].rx_gpio = DEVICE_UART0_RX_GPIO;
        layout->config.channels[CHANNEL_1].tx_gpio = DEVICE_UART1_TX_GPIO;
        layout->config.channels[CHANNEL_1].rx_gpio = DEVICE_UART1_RX_GPIO;
        layout->config.channels[CHANNEL_2].tx_gpio = DEVICE_UART2_TX_GPIO;
        layout->config.channels[CHANNEL_2].rx_gpio = DEVICE_UART2_RX_GPIO;
        layout->config.channels[CHANNEL_3].tx_gpio = DEVICE_UART3_TX_GPIO;
        layout->config.channels[CHANNEL_3].rx_gpio = DEVICE_UART3_RX_GPIO;
        layout->config.channels[CHANNEL_4].tx_gpio = DEVICE_UART4_TX_GPIO;
        layout->config.channels[CHANNEL_4].rx_gpio = DEVICE_UART4_RX_GPIO;
        
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_CONFIG_LOADED, 
                  layout->revision_counter);
    }
    
    return true;
}

/**
 * Save configuration to flash if needed (deferred write-back)
 * 
 * @return true if save completed or deferred, false on error
 */
bool flash_persistence_save_configuration_if_needed(void) {
    if (!g_flash_state.initialized) {
        return false;
    }
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        return false;
    }
    
    // Check if configuration has changed
    if (layout->revision_counter == g_flash_state.last_written_revision) {
        return true;  // No changes to save
    }
    
    // Check write frequency limiting (30 seconds minimum interval)
    uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed_ms = current_time_ms - g_flash_state.last_write_timestamp_ms;
    
    if (elapsed_ms < FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS) {
        return true;  // Write deferred due to frequency limiting
    }
    
    // Perform the actual write operation
    return flash_persistence_force_save_configuration();
}

/**
 * check if a flash write is needed
 * 
 * @return true if write is needed, false if not
 */
bool flash_persistence_save_needed(void) {
    if (!g_flash_state.initialized) {
        return false;
    }
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        return false;
    }
    
    // Check if configuration has changed
    if (layout->revision_counter == g_flash_state.last_written_revision) {
        return false;  // No changes to save
    }
    
    // Check write frequency limiting (30 seconds minimum interval)
    uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed_ms = current_time_ms - g_flash_state.last_write_timestamp_ms;
    
    if (elapsed_ms > FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS) {
        return true;  // Write deferred due to frequency limiting
    }
    return false;
}

/**
 * Force save configuration to flash immediately
 * 
 * @return true if save successful, false on error
 */
bool flash_persistence_force_save_configuration(void) {
    if (!g_flash_state.initialized) {
        return false;
    }

    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        return false;
    }



    // Prepare flash block data structure
    shadow_block_copy.magic_number = FLASH_PERSISTENCE_MAGIC;

    // Disable interrupts during flash operation
    // (this is core_local)
    uint32_t ints = save_and_disable_interrupts();
    if(g_flash_state.write_in_progress) {
        restore_interrupts(ints);
        return false;
    }

    g_flash_state.write_in_progress = true;    
    //blocked now
    
    shadow_block_copy.revision_counter = layout->revision_counter;
    // Copy shared memory data
    memcpy(&shadow_block_copy.shared_memory_data, layout, sizeof(shared_memory_layout_t));
    
    //keep doing stuff while we finish this write
    restore_interrupts(ints);
        
    // Calculate SHA256 checksum of the complete block (excluding the checksum field itself)
    // Since sha256_checksum is at the start of the struct, hash from magic_number onwards
    flash_calculate_sha256(&shadow_block_copy.magic_number,
                           sizeof(flash_persistence_block_t) - sizeof(shadow_block_copy.sha256_checksum),
                           shadow_block_copy.sha256_checksum);
    
    // Write to next block in ring buffer
    if (!write_flash_block(g_flash_state.current_write_block, &shadow_block_copy)) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_WRITE, 
                  g_flash_state.current_write_block);
        g_flash_state.corruption_events++;
        
        ints = save_and_disable_interrupts();
        g_flash_state.write_in_progress = false;  
        restore_interrupts(ints);
        //unblocked now
        return false;
    }
    
    // Update persistence state
    g_flash_state.last_written_revision = shadow_block_copy.revision_counter;
    g_flash_state.last_write_timestamp_ms = to_ms_since_boot(get_absolute_time());
    g_flash_state.total_writes_lifetime++;
    
    // Advance to next block in ring buffer
    advance_ring_buffer_position();
    
    ints = save_and_disable_interrupts();
    g_flash_state.write_in_progress = false;  
    restore_interrupts(ints);
    //unblocked now

    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_CONFIG_SAVED, 
              layout->revision_counter);
    
    return true;
}

/**
 * Perform factory reset - invalidate all flash ring buffer entries
 * 
 * The proper way to do factory reset is to invalidate all existing ring buffer
 * blocks so that on next boot, the system finds no valid configuration and 
 * automatically uses factory defaults.
 */
void flash_persistence_factory_reset(void) {
    
    if (!g_flash_state.initialized) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FACTORY_RESET, 0);
        return;
    }
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_WARN, LOG_EVENT_FACTORY_RESET, 
              FLASH_PERSISTENCE_RING_SIZE);
    
    // Invalidate all 4 ring buffer blocks by erasing them (sets all bytes to 0xFF)
    for (int block_idx = 0; block_idx < FLASH_PERSISTENCE_RING_SIZE; block_idx++) {
        uint32_t flash_offset = g_flash_state.partition_start_offset + 
                               (block_idx * FLASH_PERSISTENCE_BLOCK_SIZE);
        log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_ERASE, block_idx);
        for(int block_idx = 0; block_idx < FLASH_PERSISTENCE_BLOCK_SIZE / FLASH_SECTOR_SIZE; block_idx++)
        {
            // Erase the entire block (sets all bytes to 0xFF)
            // Flash is "execute in place" and so will be in use when any code that is stored in flash runs, e.g. an interrupt handler
            // or code running on a different core.
            // Calling flash_range_erase or flash_range_program at the same time as flash is running code would cause a crash.
            // flash_safe_execute disables interrupts and tries to cooperate with the other core to ensure flash is not in use
            // See the documentation for flash_safe_execute and its assumptions and limitations
            int rc = flash_safe_execute(call_flash_range_erase, (void*)flash_offset + (block_idx*FLASH_SECTOR_SIZE), UINT32_MAX);
            if(rc != PICO_OK) {
                log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_ERASE, block_idx);
                log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_ERASE_FAILED, rc);
                g_flash_state.corruption_events++;
            }
        }
    }
    
    // Reset shared memory to factory defaults using the proper initialization function
    // This avoids code duplication and ensures consistency with shared_memory_init()
    if (!shared_memory_force_reinit()) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_SHARED_MEMORY_REINIT, 0);
        printf("-4\n");
    
        return;
    }
    
    // Apply factory defaults to configuration (if available)
    factory_defaults_apply_to_config();
    
    // Verify the factory defaults were applied
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (layout) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_SHARED_MEMORY_REINIT, 
                  layout->config.channels[0].baud_rate);
    }
    
    // Save configuration with factory defaults applied
    flash_persistence_force_save_configuration();
    
    // Update flash state
    g_flash_state.last_written_revision = 0;  // No valid data in flash
    g_flash_state.corruption_events++;       // Count factory reset as corruption event
    g_flash_state.current_write_block = 0;     // Reset to first block
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_FACTORY_RESET, 1);
}

/**
 * Get total number of write operations performed
 * 
 * @return Number of flash write operations
 */
uint32_t flash_persistence_get_write_count(void) {
    return g_flash_state.total_writes_lifetime;
}

/**
 * Get number of corruption events detected
 * 
 * @return Number of corruption/recovery events
 */
uint32_t flash_persistence_get_corruption_count(void) {
    return g_flash_state.corruption_events;
}

/**
 * Verify integrity of all ring buffer blocks
 * 
 * @return true if ring buffer is healthy, false if issues detected
 */
bool flash_persistence_verify_ring_buffer_integrity(void) {
    if (!g_flash_state.initialized) {
        return false;
    }
    
    uint32_t valid_blocks = 0;
    
    // Check all ring buffer blocks
    for (int i = 0; i < FLASH_PERSISTENCE_RING_SIZE; i++) {
        flash_persistence_block_t block_data;
        if (read_flash_block(i, &block_data)) {
            // Skip uninitialized blocks (factory state)
            if (block_data.magic_number == 0xFFFFFFFF) {
                continue;
            }
            
            if (validate_block_integrity(&block_data)) {
                valid_blocks++;
            }
        }
    }
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_SCAN, valid_blocks);
    
    // Consider ring buffer healthy if at least 1 block is valid
    return (valid_blocks > 0);
}

// Internal implementation functions

/**
 * Find partition information using bootrom APIs (exposed for factory_defaults)
 * 
 * @param partition_id Partition ID to search for
 * @param start_addr Output: partition start address
 * @param size Output: partition size in bytes
 * @return true if partition found, false otherwise
 */
bool flash_find_partition_info(uint32_t partition_id, uint32_t* start_addr, uint32_t* size) {
    // Query partition table from bootrom
    pico_partition_table_t pt;
    int rc;
    rc = read_partition_table(&pt);
    if (rc != 0) {
        panic("rom_get_partition_table_info returned %d", pt.status);
    }
    if (!pt.has_partition_table) {
        printf("there is no partition table\n");
    } else if (pt.partition_count == 0) {
        printf("the partition table is empty\n");
    }

    if (pt.partition_count == 0) {
        return false;
    }

    pico_partition_t p;
    while (read_next_partition(&pt, &p)) {
        if(p.partition_id == partition_id) {
            *start_addr = p.first_sector * FLASH_SECTOR_SIZE;
            *size = ((p.last_sector + 1) - p.first_sector) * FLASH_SECTOR_SIZE; 
            DEBUG_ONLY({
                printf("    %08x->%08x \n",
                p.first_sector * FLASH_SECTOR_SIZE, (p.last_sector + 1) * FLASH_SECTOR_SIZE
                );
            });
    
        }
    }
    if (pt.status != 0) {
        panic("rom_get_partition_table_info returned %d", pt.status);
    }

    return true;
}

/**
 * Read flash Block data using XIP raw flash access
 * 
 * @param block_index block_index index (0-3)
 * @param block_data Output buffer for Block data
 * @return true if read successful, false otherwise
 */
static bool read_flash_block(uint32_t block_index, flash_persistence_block_t* block_data) {
    if (block_index >= FLASH_PERSISTENCE_RING_SIZE || !block_data) {
        return false;
    }
    
    uint32_t flash_offset = g_flash_state.partition_start_offset + 
                           (block_index * FLASH_PERSISTENCE_BLOCK_SIZE);
    
    // Use XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE for raw flash access ! NOT XIP_NOCACHE_NOALLOC_BASE !
    const uint8_t *flash_target_contents = (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + flash_offset);
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_READ, block_index);
    
    // Copy from raw flash address
    memcpy(block_data, flash_target_contents, sizeof(flash_persistence_block_t));
    
    return true;
}

/**
 * Write flash Block data using raw flash access
 * 
 * @param block_index Block index (0-7)  
 * @param block_data Block data to write
 * @return true if write successful, false otherwise
 */
static bool write_flash_block(uint32_t block_index, const flash_persistence_block_t* block_data) {

    bool needs_update=false;
    bool write_ok = true;

    if (block_index >= FLASH_PERSISTENCE_RING_SIZE || !block_data) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_WRITE, block_index);
        return false;
    }
    
    uint32_t flash_offset = g_flash_state.partition_start_offset + 
                           (block_index * FLASH_PERSISTENCE_BLOCK_SIZE);
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_WRITE, block_index);
    
    for(int sector_idx=0; sector_idx < FLASH_PERSISTENCE_BLOCK_SIZE / FLASH_SECTOR_SIZE ; sector_idx++)
    {
        needs_update=false;
        
        //check for each sector if contents have changed and update only sectors that need updating
        const uint32_t* old_data = (const uint32_t*)((XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + flash_offset) + (sector_idx*FLASH_SECTOR_SIZE));
        const uint32_t* new_data = (const uint32_t*)( (uint8_t*)block_data + (sector_idx*FLASH_SECTOR_SIZE)); //convert to byte pointer before arithmetics!
        
        for(int data_idx = 0; data_idx<FLASH_SECTOR_SIZE/sizeof(uint32_t); data_idx++)
        {
            //check if the sector is different to the data
            if(!needs_update && old_data[data_idx] != new_data[data_idx] ) {
                needs_update = true;
                DEBUG_ONLY({
                    printf("-5 needs update at 0x%x (0x%x!=0x%x)!\n", data_idx, old_data[data_idx], new_data[data_idx]);
                });
                break;
            }
        }
       
        if(needs_update) {
            // Erase the entire block (sets all bytes to 0xFF)
            // Flash is "execute in place" and so will be in use when any code that is stored in flash runs, e.g. an interrupt handler
            // or code running on a different core.
            // Calling flash_range_erase or flash_range_program at the same time as flash is running code would cause a crash.
            // flash_safe_execute disables interrupts and tries to cooperate with the other core to ensure flash is not in use
            // See the documentation for flash_safe_execute and its assumptions and limitations
            int rc = flash_safe_execute(call_flash_range_erase, (void*)flash_offset + (sector_idx*FLASH_SECTOR_SIZE), UINT32_MAX);
            if(rc != PICO_OK) {
                printf("FLASH ERASE FAILED! Start: 0x%X\n", flash_offset + (sector_idx*FLASH_SECTOR_SIZE));
                log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_ERASE, block_index);
                log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_ERASE_FAILED, rc);
                g_flash_state.corruption_events++;
                write_ok=false;
            }
            // Program the entire block 
            // Flash is "execute in place" and so will be in use when any code that is stored in flash runs, e.g. an interrupt handler
            // or code running on a different core.
            // Calling flash_range_erase or flash_range_program at the same time as flash is running code would cause a crash.
            // flash_safe_execute disables interrupts and tries to cooperate with the other core to ensure flash is not in use
            // See the documentation for flash_safe_execute and its assumptions and limitations
            // TODO: this could be done in FLASH_BLOCK_SIZE instead of FLASH SECTOR_SIZE
            uintptr_t params[] = { (uintptr_t)(flash_offset + (sector_idx*FLASH_SECTOR_SIZE)), (uintptr_t)new_data};
            rc = flash_safe_execute(call_flash_range_program, params, UINT32_MAX);
            if(rc != PICO_OK) {
                printf("FLASH PROGRAM FAILED! Start: 0x%X\n",flash_offset + (sector_idx*FLASH_SECTOR_SIZE));
                log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_ERASE, block_index);
                log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_ERROR, LOG_EVENT_FLASH_ERASE_FAILED, rc);
                g_flash_state.corruption_events++;
                write_ok=false;
            }
        }

    }
    if(!write_ok)
    {
        printf("WRITE FAILED!\n");
        printf("WRITE FAILED!\n");
        printf("WRITE FAILED!\n");
    }
    // Restore interrupts (this is core_local)
    //restore_interrupts(ints);
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_WRITE, block_index);
    
    return true;
}

/**
 * Validate block integrity using SHA256 checksum
 * 
 * @param block_data block data to validate
 * @return true if block is valid, false otherwise
 */
static bool validate_block_integrity(const flash_persistence_block_t* block_data) {
    if (!block_data) {
        return false;
    }
    
    // Check magic number
    if (block_data->magic_number != FLASH_PERSISTENCE_MAGIC) {
        return false;
    }
    
    // Calculate and verify SHA256 checksum over the complete block (excluding checksum field)
    // Since sha256_checksum is at the start of the struct, hash from magic_number onwards
    uint8_t calculated_checksum[32];
    if (flash_calculate_sha256(&block_data->magic_number,
                               sizeof(flash_persistence_block_t) - sizeof(block_data->sha256_checksum),
                               calculated_checksum) != 0) {
        return false;
    }
    
    return (memcmp(block_data->sha256_checksum, calculated_checksum, 32) == 0);
}

/**
 * Calculate SHA256 checksum of arbitrary data (exposed for factory_defaults)
 * 
 * Uses RP2350 hardware-accelerated SHA-256 to hash the provided data buffer.
 * 
 * @param data Pointer to data to checksum
 * @param size Size of data in bytes
 * @param checksum_out Output buffer for 32-byte checksum
 * @return 0 on success, -1 on error
 */
int flash_calculate_sha256(const void* data, size_t size, uint8_t* checksum_out) {
    if (!data || !checksum_out || size == 0) {
        return -1;
    }
    
    // Use RP2350 hardware SHA-256 accelerator
    pico_sha256_state_t state;
    int rc = pico_sha256_start_blocking(&state, SHA256_BIG_ENDIAN, true);
    if (rc != PICO_OK) {
        return -1;
    }
    
    pico_sha256_update_blocking(&state, (const uint8_t*)data, size);
    
    sha256_result_t result;
    pico_sha256_finish(&state, &result);
    
    memcpy(checksum_out, result.bytes, SHA256_RESULT_BYTES);
    
    return 0;
}

/**
 * Find the best valid block (highest revision with valid checksum)
 * 
 * @return block index or -1 if no blocks found
 */
static int find_best_valid_block(void) {
    int best_block = -1;
    uint32_t highest_revision = 0;
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_SCAN, 0);
    
    // Check all blocks
    for (int block_idx = 0; block_idx < FLASH_PERSISTENCE_RING_SIZE; block_idx++) {
        flash_persistence_block_t block_data;
        
        if (!read_flash_block(block_idx, &block_data)) {
            log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_INVALID, block_idx);
            continue;
        }
        
        // Check for uninitialized flash (all 0xFF)
        if (block_data.magic_number == 0xFFFFFFFF) {
            log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_INVALID, block_idx + 10);
            continue;
        }
        
        // Check magic number
        if (block_data.magic_number != FLASH_PERSISTENCE_MAGIC) {
            log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_INVALID, block_idx + 20);
            continue;
        }
        
        // Validate checksum
        if (!validate_block_integrity(&block_data)) {
            log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_INVALID, block_idx + 30);
            continue;
        }
        
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_NUMBER, block_idx);
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_REVISION, 
                  block_data.revision_counter);
        
        // Track highest revision
        if (block_data.revision_counter > highest_revision) {
            highest_revision = block_data.revision_counter;
            best_block = block_idx;
        }
    }
    
    if (highest_revision == 0) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BLOCK_SCAN, 0);
        return -1;
    }
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BEST_BLOCK_NUMBER, best_block);
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_FLASH_BEST_BLOCK_REV, highest_revision);
    return best_block;
}

/**
 * Advance ring buffer write position
 */
static void advance_ring_buffer_position(void) {
    g_flash_state.current_write_block = (g_flash_state.current_write_block + 1) % FLASH_PERSISTENCE_RING_SIZE;
}


// This function will be called when it's safe to call flash_range_erase
static void call_flash_range_erase(void *param) {
    uint32_t offset = (uint32_t)param;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

// This function will be called when it's safe to call flash_range_program
static void call_flash_range_program(void *param) {
    uint32_t offset = ((uintptr_t*)param)[0];
    const uint8_t *data = (const uint8_t *)((uintptr_t*)param)[1];
    flash_range_program(offset, data, FLASH_SECTOR_SIZE);
}

/*
 * Read the partition table information.
 *
 * See the RP2350 datasheet 5.1.2, 5.4.8.16 for flags and structures that can be specified.
 */
int read_partition_table(pico_partition_table_t *pt) {
    // Reads fixed size fields
    uint32_t flags = PT_INFO_PT_INFO | PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_PARTITION_ID;
    int rc = rom_get_partition_table_info(pt->table, sizeof(pt->table), flags);
    if (rc < 0) {
        pt->partition_count = 0;
        pt->status = rc;
        return rc;
    }

    size_t pos = 0;
    pt->fields = pt->table[pos++];
    assert(pt->fields == flags);
    pt->partition_count = pt->table[pos] & 0x000000FF;
    pt->has_partition_table = pt->table[pos] & 0x00000100;
    pos++;
    uint32_t location = pt->table[pos++];
    pt->unpartitioned_space_first_sector = PART_LOC_FIRST(location);
    pt->unpartitioned_space_last_sector = PART_LOC_LAST(location);
    pt->flags_and_permissions = pt->table[pos++];
    pt->current_partition = 0;
    pt->pos = pos;
    pt->status = 0;

    return 0;
}

/*
 * Extract each partition information
 */
bool read_next_partition(pico_partition_table_t *pt, pico_partition_t *p) {
    if (pt->current_partition >= pt->partition_count) {
        return false;
    }

    size_t pos = pt->pos;
    uint32_t location = pt->table[pos++];
    p->first_sector = PART_LOC_FIRST(location);
    p->last_sector = PART_LOC_LAST(location);
    p->flags_and_permissions = pt->table[pos++];
    p->has_name = p->flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_NAME_BITS;
    p->has_id = p->flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS;

    if (p->has_id) {
        uint32_t id_low  = pt->table[pos++];
        uint32_t id_high = pt->table[pos++];
        p->partition_id = ((uint64_t)id_high << 32) | id_low;
    } else {
        p->partition_id = 0;
    }
    pt->pos = pos;

    p->extra_family_id_count = (p->flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_BITS)
                                   >> PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_LSB;
    if (p->extra_family_id_count | p->has_name) {
        // Read variable length fields
        uint32_t extra_family_ids_and_name[PARTITION_EXTRA_FAMILY_ID_MAX + (((PARTITION_NAME_MAX + 1) / sizeof(uint32_t)) + 1)];
        uint32_t flags = PT_INFO_SINGLE_PARTITION | PT_INFO_PARTITION_FAMILY_IDS | PT_INFO_PARTITION_NAME;
        int rc = rom_get_partition_table_info(extra_family_ids_and_name, sizeof(extra_family_ids_and_name),
                                              (pt->current_partition << 24 | flags));
        if (rc < 0) {
            pt->status = rc;
            return false;
        }
        size_t pos_ = 0;
        uint32_t __attribute__((unused)) fields = extra_family_ids_and_name[pos_++];
        assert(fields == flags);
        for (size_t i = 0; i < p->extra_family_id_count; i++, pos_++) {
            p->extra_family_ids[i] = extra_family_ids_and_name[pos_];
        }

        if (p->has_name) {
            uint8_t *name_buf = (uint8_t *)&extra_family_ids_and_name[pos_];
            uint8_t name_length = *name_buf++ & 0x7F;
            memcpy(p->name, name_buf, name_length);
            p->name[name_length] = '\0';
        }
    }
    if (!p->has_name)
         p->name[0] = '\0';

    pt->current_partition++;
    return true;
}


flash_persistence_state_t* get_persistence_state(void) {
    return &g_flash_state;
}