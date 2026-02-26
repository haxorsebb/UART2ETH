/**
 * @file update_manager.c
 * @brief Firmware update module implementation
 * 
 * Implements OTA firmware updates using RP2350's Try-Before-You-Buy (TBYB)
 * mechanism with A/B partition support. Properly parses UF2 file format
 * and writes firmware blocks to the correct flash addresses.
 * 
 * Documentation Reference:
 * - ADR-017: Update Module Architecture
 * - arc42 Chapter 5 - Update Manager Building Block
 */

#include "update/update_manager.h"
#include "log_manager.h"
#include "debug.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include "boot/uf2.h"

#include <string.h>
#include <stdio.h>

// ============================================================================
// Constants
// ============================================================================

#define UPDATE_WORKAREA_SIZE        (4 * 1024)  // 4KB workarea for ROM functions
#define UF2_BLOCK_SIZE              512         // Size of one UF2 block
#define UF2_PAYLOAD_SIZE            256         // Actual data payload per UF2 block

// UF2 Family ID for RP2350-ARM-S (secure ARM mode)
#define RP2350_FAMILY_ID            0xe48bff59

// ============================================================================
// Internal State
// ============================================================================

/**
 * @brief Parameters for flash operations via flash_safe_execute
 */
typedef struct {
    uint32_t address;       ///< Flash address (runtime address with XIP_BASE)
    uint32_t size;          ///< Operation size
    uint8_t* data;          ///< Data pointer (for program operations)
    struct cflash_flags flags; ///< Flash operation flags
    int result;             ///< Operation result
} update_flash_params_t;

/**
 * @brief Internal update state
 */
typedef struct {
    // Initialization state
    bool initialized;
    
    // Partition information (discovered via ROM on first UF2 block)
    uint32_t partition_start;       ///< Partition start address (flash offset)
    uint32_t partition_end;         ///< Partition end address (flash offset)
    uint32_t partition_size;        ///< Partition size in bytes
    int32_t write_offset;           ///< Offset between UF2 target addr and actual storage
    uint32_t flash_update_addr;     ///< Address for reboot (partition_start + XIP_BASE)
    
    // Upload session state
    update_state_t state;
    uint32_t expected_size;
    uint32_t bytes_received;
    uint32_t bytes_written;
    uint32_t blocks_processed;
    uint32_t num_blocks;            ///< Total UF2 blocks expected (from first block)
    uint32_t highest_erased_sector;
    uint32_t last_error_code;
    uint32_t family_id;             ///< UF2 family ID (validated)
    bool first_block_done;          ///< True after first UF2 block processed
    
    // Reboot handling
    reboot_reason_t reboot_reason;
    
    // Workareas (must be word-aligned)
    __attribute__((aligned(4))) uint8_t workarea[UPDATE_WORKAREA_SIZE];
    
    // UF2 block assembly buffer
    __attribute__((aligned(4))) uint8_t uf2_buffer[UF2_BLOCK_SIZE];
    uint32_t uf2_buffer_offset;     ///< Current offset within UF2 buffer
    
} update_state_internal_t;

static update_state_internal_t g_update = {0};

// Use the uf2_block struct from the SDK
typedef struct uf2_block uf2_block_t;

// ============================================================================
// Forward Declarations (flash_safe_execute callbacks)
// ============================================================================

static void call_flash_erase_for_update(void* param);
static void call_flash_program_for_update(void* param);
static void call_explicit_buy_internal(void* param);

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * @brief Write a single UF2 block's payload to flash
 * 
 * Erases the sector if needed and programs the 256-byte payload.
 * Uses flash_safe_execute for dual-core safety.
 * 
 * @param block Pointer to the UF2 block
 * @return true on success, false on error
 */
static bool write_uf2_block_to_flash(const uf2_block_t* block) {
    // Calculate the actual flash address for this block
    uint32_t flash_addr = block->target_addr + g_update.write_offset;
    
    DEBUG_ONLY({
        printf("UPDATE: Writing UF2 block %u/%u to 0x%08X (target_addr=0x%08X)\n",
               block->block_no + 1, block->num_blocks, flash_addr, block->target_addr);
    });
    
    // Prepare parameters for flash operations
    update_flash_params_t params;
    params.result = 0;
    
    // Check if we need to erase this sector (using target_addr for sector calculation)
    uint32_t current_sector = block->target_addr / FLASH_SECTOR_SIZE;
    if (current_sector > g_update.highest_erased_sector || g_update.highest_erased_sector == 0) {
        // Erase the sector at the actual flash location
        uint32_t erase_addr = block->target_addr + g_update.write_offset;
        params.address = erase_addr;
        params.size = FLASH_SECTOR_SIZE;
        params.flags.flags = 
            (CFLASH_OP_VALUE_ERASE << CFLASH_OP_LSB) |
            (CFLASH_SECLEVEL_VALUE_SECURE << CFLASH_SECLEVEL_LSB) |
            (CFLASH_ASPACE_VALUE_STORAGE << CFLASH_ASPACE_LSB);
        
        int rc = flash_safe_execute(call_flash_erase_for_update, &params, UINT32_MAX);
        if (rc != PICO_OK || params.result != 0) {
            printf("UPDATE: Flash erase FAILED at 0x%08X (rc=%d, result=%d)\n", 
                   params.address, rc, params.result);
            g_update.last_error_code = (rc != PICO_OK) ? (uint32_t)rc : (uint32_t)params.result;
            return false;
        }
        
        g_update.highest_erased_sector = current_sector;
        
        DEBUG_ONLY({
            printf("UPDATE: Erased sector %u at 0x%08X\n", current_sector, erase_addr);
        });
    }
    
    // Program the 256-byte payload
    params.address = flash_addr;
    params.size = UF2_PAYLOAD_SIZE;
    params.data = (uint8_t*)block->data;  // Cast away const - data is not modified
    params.flags.flags = 
        (CFLASH_OP_VALUE_PROGRAM << CFLASH_OP_LSB) |
        (CFLASH_SECLEVEL_VALUE_SECURE << CFLASH_SECLEVEL_LSB) |
        (CFLASH_ASPACE_VALUE_STORAGE << CFLASH_ASPACE_LSB);
    
    int rc = flash_safe_execute(call_flash_program_for_update, &params, UINT32_MAX);
    if (rc != PICO_OK || params.result != 0) {
        printf("UPDATE: Flash program FAILED at 0x%08X (rc=%d, result=%d)\n",
               flash_addr, rc, params.result);
        g_update.last_error_code = (rc != PICO_OK) ? (uint32_t)rc : (uint32_t)params.result;
        return false;
    }
    
    g_update.bytes_written += UF2_PAYLOAD_SIZE;
    
    return true;
}

/**
 * @brief Process a complete UF2 block
 * 
 * Validates the UF2 block structure, extracts partition info on first block,
 * and writes the payload to flash.
 * 
 * @param block Pointer to the UF2 block
 * @return true on success, false on error
 */
static bool process_uf2_block(const uf2_block_t* block) {
    // Validate UF2 magic numbers
    if (block->magic_start0 != UF2_MAGIC_START0 ||
        block->magic_start1 != UF2_MAGIC_START1 ||
        block->magic_end != UF2_MAGIC_END) {
        printf("UPDATE: Invalid UF2 magic numbers (0x%08X 0x%08X ... 0x%08X)\n",
               block->magic_start0, block->magic_start1, block->magic_end);
        g_update.last_error_code = 0xBAD1;
        return false;
    }
    
    // First block: extract partition info and calculate write offset
    if (!g_update.first_block_done) {
        g_update.num_blocks = block->num_blocks;
        g_update.family_id = block->file_size;  // file_size field is family ID when flag set
        
        printf("UPDATE: First UF2 block - %u total blocks, family_id=0x%08X\n",
               g_update.num_blocks, g_update.family_id);
        
        // Validate family ID (should be RP2350-ARM-S)
        if (g_update.family_id != RP2350_FAMILY_ID) {
            printf("UPDATE: Warning - unexpected family ID 0x%08X (expected 0x%08X)\n",
                   g_update.family_id, RP2350_FAMILY_ID);
            // Continue anyway - might be a different valid RP2350 variant
        }
        
        // Get target partition using ROM function
        resident_partition_t target_partition;
        rom_flash_flush_cache();
        
        int rc = rom_get_uf2_target_partition(g_update.workarea, sizeof(g_update.workarea),
                                              g_update.family_id, &target_partition);
        if (rc != 0) {
            printf("UPDATE: Failed to get target partition (rc=%d)\n", rc);
            g_update.last_error_code = (uint32_t)rc;
            return false;
        }
        
        printf("UPDATE: Target partition: permissions_and_location=0x%08lX\n",
               (unsigned long)target_partition.permissions_and_location);
        
        // Extract partition boundaries
        uint16_t first_sector = (target_partition.permissions_and_location & 
                                 PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >> 
                                 PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
        uint16_t last_sector = (target_partition.permissions_and_location & 
                                PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >> 
                                PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
        
        g_update.partition_start = first_sector * FLASH_SECTOR_SIZE;
        g_update.partition_end = (last_sector + 1) * FLASH_SECTOR_SIZE;
        g_update.partition_size = g_update.partition_end - g_update.partition_start;
        g_update.flash_update_addr = g_update.partition_start + XIP_BASE;
        
        printf("UPDATE: Partition: 0x%08X - 0x%08X (%u KB)\n",
               g_update.partition_start, g_update.partition_end, 
               g_update.partition_size / 1024);
        
        // Calculate write offset: where UF2 blocks think they're writing vs actual location
        // This is exactly how the official pico example does it
        g_update.write_offset = g_update.partition_start + XIP_BASE - block->target_addr;
        
        printf("UPDATE: Write offset: 0x%08X (target_addr=0x%08X)\n",
               g_update.write_offset, block->target_addr);
        
        g_update.first_block_done = true;
    }
    
    // Validate block number sequence
    if (block->block_no != g_update.blocks_processed) {
        printf("UPDATE: Block number mismatch - expected %u, got %u\n",
               g_update.blocks_processed, block->block_no);
        g_update.last_error_code = 0xBAD2;
        return false;
    }
    
    // Validate family ID consistency
    if (block->file_size != g_update.family_id) {
        printf("UPDATE: Family ID mismatch in block %u\n", block->block_no);
        g_update.last_error_code = 0xBAD3;
        return false;
    }
    
    // Write the block to flash
    if (!write_uf2_block_to_flash(block)) {
        return false;
    }
    
    g_update.blocks_processed++;
    
    // Progress reporting every 64 blocks (~16KB)
    if ((g_update.blocks_processed % 64) == 0 || 
        g_update.blocks_processed == g_update.num_blocks) {
        printf("UPDATE: Progress %u / %u blocks (%u%%)\n",
               g_update.blocks_processed, g_update.num_blocks,
               (g_update.blocks_processed * 100) / g_update.num_blocks);
    }
    
    return true;
}

// ============================================================================
// flash_safe_execute Callbacks
// ============================================================================

/**
 * @brief Callback for flash erase operation
 */
static void call_flash_erase_for_update(void* param) {
    update_flash_params_t* p = (update_flash_params_t*)param;
    p->result = rom_flash_op(p->flags, p->address, p->size, NULL);
}

/**
 * @brief Callback for flash program operation
 */
static void call_flash_program_for_update(void* param) {
    update_flash_params_t* p = (update_flash_params_t*)param;
    p->result = rom_flash_op(p->flags, p->address, p->size, p->data);
}

/**
 * @brief Callback for explicit buy operation
 */
static void call_explicit_buy_internal(void* param) {
    int* result = (int*)param;
    *result = rom_explicit_buy(g_update.workarea, sizeof(g_update.workarea));
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool update_init(void) {
    if (g_update.initialized) {
        return true;
    }
    
    printf("UPDATE: Initializing update module\n");
    
    // Initialize state
    memset(&g_update, 0, sizeof(g_update));
    g_update.state = UPDATE_STATE_IDLE;
    g_update.reboot_reason = REBOOT_REASON_NONE;
    
    g_update.initialized = true;
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0x0017);
    
    printf("UPDATE: Module initialized\n");
    return true;
}

bool update_start_upload(uint32_t expected_size) {
    if (!g_update.initialized) {
        printf("UPDATE: Module not initialized\n");
        return false;
    }
    
    if (g_update.state == UPDATE_STATE_RECEIVING) {
        printf("UPDATE: Upload already in progress\n");
        return false;
    }
    
    printf("UPDATE: Starting upload, expected size: %u bytes\n", expected_size);
    
    // Initialize upload session state
    g_update.state = UPDATE_STATE_RECEIVING;
    g_update.expected_size = expected_size;
    g_update.bytes_received = 0;
    g_update.bytes_written = 0;
    g_update.blocks_processed = 0;
    g_update.num_blocks = 0;
    g_update.highest_erased_sector = 0;
    g_update.last_error_code = 0;
    g_update.first_block_done = false;
    g_update.uf2_buffer_offset = 0;
    g_update.family_id = 0;
    g_update.write_offset = 0;
    g_update.partition_start = 0;
    g_update.partition_end = 0;
    g_update.partition_size = 0;
    g_update.flash_update_addr = 0;
    
    // Clear UF2 buffer
    memset(g_update.uf2_buffer, 0, UF2_BLOCK_SIZE);
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_FIRMWARE_UPDATE_START, expected_size);
    
    return true;
}

bool update_write_block(uint8_t* data, uint32_t size, bool finished) {
    if (!g_update.initialized) {
        return false;
    }
    
    if (g_update.state != UPDATE_STATE_RECEIVING) {
        printf("UPDATE: Not in receiving state\n");
        return false;
    }
    
    // Handle data if provided
    if (data != NULL && size > 0) {
        g_update.bytes_received += size;
        
        // Buffer incoming data and process complete UF2 blocks
        uint32_t data_offset = 0;
        
        while (data_offset < size) {
            // Calculate how much we can copy to UF2 buffer
            uint32_t space_in_buffer = UF2_BLOCK_SIZE - g_update.uf2_buffer_offset;
            uint32_t copy_size = size - data_offset;
            if (copy_size > space_in_buffer) {
                copy_size = space_in_buffer;
            }
            
            // Copy to UF2 buffer
            memcpy(g_update.uf2_buffer + g_update.uf2_buffer_offset, 
                   data + data_offset, copy_size);
            
            g_update.uf2_buffer_offset += copy_size;
            data_offset += copy_size;
            
            // If we have a complete UF2 block, process it
            if (g_update.uf2_buffer_offset >= UF2_BLOCK_SIZE) {
                uf2_block_t* block = (uf2_block_t*)g_update.uf2_buffer;
                
                if (!process_uf2_block(block)) {
                    g_update.state = UPDATE_STATE_ERROR;
                    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, 
                              LOG_EVENT_FIRMWARE_UPDATE_FAILED, g_update.last_error_code);
                    return false;
                }
                
                // Reset buffer for next block
                g_update.uf2_buffer_offset = 0;
                memset(g_update.uf2_buffer, 0, UF2_BLOCK_SIZE);
            }
        }
    }
    
    // Handle finish
    if (finished) {
        // Check if we processed all expected blocks
        if (g_update.num_blocks > 0 && g_update.blocks_processed < g_update.num_blocks) {
            printf("UPDATE: Warning - only %u/%u blocks received\n",
                   g_update.blocks_processed, g_update.num_blocks);
        }
        
        printf("UPDATE: Upload complete - %u bytes received, %u blocks processed, %u bytes written\n",
               g_update.bytes_received, g_update.blocks_processed, g_update.bytes_written);
        
        g_update.state = UPDATE_STATE_COMPLETE;
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, 
                  LOG_EVENT_FIRMWARE_UPDATE_COMPLETE, g_update.bytes_written);
    }
    
    return true;
}

void update_abort_upload(void) {
    if (g_update.state == UPDATE_STATE_RECEIVING) {
        printf("UPDATE: Upload aborted\n");
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_FIRMWARE_UPDATE_FAILED, 0xABCD);
    }
    
    g_update.state = UPDATE_STATE_IDLE;
    g_update.bytes_received = 0;
    g_update.bytes_written = 0;
    g_update.blocks_processed = 0;
    g_update.uf2_buffer_offset = 0;
    g_update.first_block_done = false;
    memset(g_update.uf2_buffer, 0, UF2_BLOCK_SIZE);
}

update_state_t update_get_state(void) {
    return g_update.state;
}

void update_get_stats(update_stats_t* stats) {
    if (stats) {
        stats->bytes_received = g_update.bytes_received;
        stats->bytes_written = g_update.bytes_written;
        stats->blocks_processed = g_update.blocks_processed;
        stats->sectors_erased = g_update.highest_erased_sector;
        stats->last_error_code = g_update.last_error_code;
    }
}

bool update_buy_current_image(void) {
    printf("UPDATE: Attempting to buy current image\n");
    
    // Check if this boot was a flash update boot
    boot_info_t boot_info = {0};
    int rc = rom_get_boot_info(&boot_info);
    
    printf("UPDATE: Boot info - partition=%d, type=%d\n", 
           boot_info.partition, rom_get_last_boot_type());
    
    if (rom_get_last_boot_type() != BOOT_TYPE_FLASH_UPDATE) {
        printf("UPDATE: Not a flash update boot, nothing to buy\n");
        return true;  // Not an error, just nothing to do
    }
    
    printf("UPDATE: Flash update boot detected, performing explicit buy\n");
    
    if (boot_info.reboot_params[0]) {
        printf("UPDATE: Flash update base was 0x%X\n", boot_info.reboot_params[0]);
    }
    if (boot_info.tbyb_and_update_info) {
        printf("UPDATE: TBYB info before buy: 0x%X\n", boot_info.tbyb_and_update_info);
    }
    
    // Perform explicit buy via flash_safe_execute
    int buy_result = -1;
    rc = flash_safe_execute(call_explicit_buy_internal, &buy_result, UINT32_MAX);
    
    if (rc != PICO_OK) {
        printf("UPDATE: flash_safe_execute failed for buy (rc=%d)\n", rc);
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_FIRMWARE_BUY_FAILED, rc);
        return false;
    }
    
    if (buy_result != 0) {
        printf("UPDATE: rom_explicit_buy returned error %d\n", buy_result);
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_FIRMWARE_BUY_FAILED, buy_result);
        return false;
    }
    
    // Verify buy succeeded
    rc = rom_get_boot_info(&boot_info);
    printf("UPDATE: TBYB info after buy: 0x%X\n", boot_info.tbyb_and_update_info);
    
    printf("UPDATE: Image bought successfully\n");
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_FIRMWARE_BUY_SUCCESS, 0);
    
    return true;
}

void update_set_reboot_reason(reboot_reason_t reason) {
    g_update.reboot_reason = reason;
    printf("UPDATE: Reboot reason set to %s\n", update_reboot_reason_to_string(reason));
}

reboot_reason_t update_get_reboot_reason(void) {
    return g_update.reboot_reason;
}

void update_execute_reboot(void) {
    printf("UPDATE: Executing reboot (reason: %s)\n", 
           update_reboot_reason_to_string(g_update.reboot_reason));
    
    // Check if this is a firmware update reboot
    if (g_update.reboot_reason == REBOOT_REASON_UPDATE_COMPLETE && 
        g_update.state == UPDATE_STATE_COMPLETE &&
        g_update.flash_update_addr != 0) {
        
        printf("UPDATE: Triggering flash update reboot to 0x%08X\n", g_update.flash_update_addr);
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_REBOOT, 
                  (uint32_t)g_update.reboot_reason);
        
        // Small delay to allow log to flush
        sleep_ms(100);
        
        // Reboot into the new firmware using TBYB mechanism
        int ret = rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, 500, g_update.flash_update_addr, 0);
        
        // If we get here, reboot failed
        printf("UPDATE: Flash update reboot failed (ret=%d), falling back to watchdog\n", ret);
    }
    
    // Regular reboot or fallback
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_SYSTEM_REBOOT, 
              (uint32_t)g_update.reboot_reason);
    
    // Small delay to allow log to be written
    sleep_ms(100);
    
    // Use watchdog reboot for clean reset
    watchdog_reboot(0, 0, 0);
    
    // Should never reach here
    while (1) {
        tight_loop_contents();
    }
}

const char* update_reboot_reason_to_string(reboot_reason_t reason) {
    switch (reason) {
        case REBOOT_REASON_NONE:
            return "NONE";
        case REBOOT_REASON_UPDATE_BUY_FAILED:
            return "UPDATE_BUY_FAILED";
        case REBOOT_REASON_UPDATE_COMPLETE:
            return "UPDATE_COMPLETE";
        case REBOOT_REASON_USER_REQUESTED:
            return "USER_REQUESTED";
        case REBOOT_REASON_ERROR_RECOVERY:
            return "ERROR_RECOVERY";
        default:
            return "UNKNOWN";
    }
}
