/**
 * @file factory_defaults.c
 * @brief Factory defaults implementation with flash partition storage
 * 
 * Implements factory defaults loading and management using dedicated flash partition
 * with SHA256 integrity verification. Factory defaults are written once during
 * manufacturing and provide read-only device-specific configuration.
 * 
 * Documentation Reference:
 * - ADR-014: Factory Defaults Implementation Strategy
 * - arc42 Chapter 5 - Configuration Manager - Factory Defaults
 */

#include "factory_defaults.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include "pico/sha256.h"
#include "hardware/flash.h"
#include "boot/picobin.h"
#include <stdio.h>
#include <string.h>

// Partition table access (from flash_persistence.c)
#define PART_LOC_FIRST(x) ( ((x) & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB )
#define PART_LOC_LAST(x)  ( ((x) & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)  >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB )

// Partition table structures (from flash_persistence.c)
#define PARTITION_TABLE_MAX_PARTITIONS 16
#define PARTITION_EXTRA_FAMILY_ID_MAX 8
#define PARTITION_LOCATION_AND_FLAGS_SIZE  2
#define PARTITION_ID_SIZE                  2
#define PARTITION_NAME_MAX                 127
#define PARTITION_TABLE_FIXED_INFO_SIZE    (4 + PARTITION_TABLE_MAX_PARTITIONS * (PARTITION_LOCATION_AND_FLAGS_SIZE + PARTITION_ID_SIZE))

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

// Static storage for factory defaults
static factory_defaults_t g_factory_defaults = {0};
static bool g_factory_defaults_valid = false;

// Function prototypes
static bool find_factory_partition_info(uint32_t* start_addr, uint32_t* size);
static bool load_factory_defaults(void);
static bool validate_factory_defaults_integrity(const factory_defaults_t* defaults);
static uint32_t calculate_factory_defaults_checksum(const factory_defaults_t* defaults, uint8_t* checksum_out);
static int read_partition_table(pico_partition_table_t *pt);
static bool read_next_partition(pico_partition_table_t *pt, pico_partition_t *p);

/**
 * Initialize factory defaults system and load from flash
 */
bool factory_defaults_init(void) {
    printf("Factory defaults init...\n");
    
    if (!load_factory_defaults()) {
        printf("WARNING: Factory defaults invalid or corrupted\n");
        g_factory_defaults_valid = false;
        return false;
    }
    
    g_factory_defaults_valid = true;
    printf("Factory defaults loaded successfully\n");
    return true;
}

/**
 * Print serial number to console
 */
void factory_defaults_print_serial_number(void) {
    if (!g_factory_defaults_valid) {
        printf("Serial Number: NOT AVAILABLE (factory defaults invalid)\n");
        return;
    }
    
    // Convert 6-byte serial number to decimal value
    uint64_t serial_decimal = 0;
    for (int i = 0; i < 6; i++) {
        serial_decimal = (serial_decimal << 8) | g_factory_defaults.serial_number[i];
    }
    
    // Format: YYWW-NNNNNNNNNNNN (decimal, 12 digits with leading zeros)
    printf("Serial Number: %02u%02u-%012llu\n",
           g_factory_defaults.production_year,
           g_factory_defaults.production_week,
           serial_decimal);
}

/**
 * Apply factory defaults to shared memory configuration
 */
void factory_defaults_apply_to_config(void) {
    if (!g_factory_defaults_valid) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_WARN, LOG_EVENT_FACTORY_DEFAULTS_INVALID, 0);
        return;
    }
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FACTORY_DEFAULTS_APPLY_FAILED, 0);
        return;
    }
    
    // Apply network defaults
    layout->config.network.static_ip.addr = g_factory_defaults.default_ip;
    layout->config.network.static_netmask.addr = g_factory_defaults.default_netmask;
    layout->config.network.use_dhcp = g_factory_defaults.default_dhcp_enable;
    
    // Apply MAC address
    memcpy(layout->config.network.mac_address, g_factory_defaults.mac_address, 6);
    
    // TODO: Apply default password to user authentication system when implemented
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_FACTORY_DEFAULTS_APPLIED, g_factory_defaults.board_type);
}

/**
 * Get read-only pointer to factory defaults
 */
const factory_defaults_t* factory_defaults_get(void) {
    if (!g_factory_defaults_valid) {
        return NULL;
    }
    return &g_factory_defaults;
}

/**
 * Check if factory defaults are valid
 */
bool factory_defaults_is_valid(void) {
    return g_factory_defaults_valid;
}

// Internal implementation functions

/**
 * Load factory defaults from flash partition
 */
static bool load_factory_defaults(void) {
    uint32_t partition_start = 0;
    uint32_t partition_size = 0;
    
    // Find factory defaults partition
    if (!find_factory_partition_info(&partition_start, &partition_size)) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FACTORY_PARTITION_NOT_FOUND, 
                  FLASH_PARTITION_FACTORY_DEFAULTS);
        return false;
    }
    
    // Verify partition size
    if (partition_size < sizeof(factory_defaults_t)) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FACTORY_PARTITION_TOO_SMALL, 
                  partition_size);
        return false;
    }
    
    // Read factory defaults from flash using XIP raw flash access
    const uint8_t *flash_contents = (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + partition_start);
    memcpy(&g_factory_defaults, flash_contents, sizeof(factory_defaults_t));
    
    // Validate integrity
    if (!validate_factory_defaults_integrity(&g_factory_defaults)) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FACTORY_DEFAULTS_CHECKSUM_FAILED, 0);
        return false;
    }
    
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_FACTORY_DEFAULTS_LOADED, 
              g_factory_defaults.board_type);
    
    return true;
}

/**
 * Validate factory defaults integrity using SHA256
 */
static bool validate_factory_defaults_integrity(const factory_defaults_t* defaults) {
    if (!defaults) {
        return false;
    }
    
    // Calculate SHA256 checksum of all fields after sha256_checksum
    uint8_t calculated_checksum[32];
    if (calculate_factory_defaults_checksum(defaults, calculated_checksum) != 0) {
        return false;
    }
    
    // Compare checksums
    return (memcmp(defaults->sha256_checksum, calculated_checksum, 32) == 0);
}

/**
 * Calculate SHA256 checksum of factory defaults
 */
static uint32_t calculate_factory_defaults_checksum(const factory_defaults_t* defaults, uint8_t* checksum_out) {
    if (!defaults || !checksum_out) {
        return -1;
    }
    
    // Hash everything after sha256_checksum field
    const uint8_t* data_start = (const uint8_t*)&defaults->production_week;
    size_t data_size = sizeof(factory_defaults_t) - offsetof(factory_defaults_t, production_week);
    
    // Use RP2350 hardware SHA-256 accelerator
    pico_sha256_state_t state;
    int rc = pico_sha256_start_blocking(&state, SHA256_BIG_ENDIAN, true);
    if (rc != PICO_OK) {
        return -1;
    }
    
    pico_sha256_update_blocking(&state, data_start, data_size);
    
    sha256_result_t result;
    pico_sha256_finish(&state, &result);
    
    memcpy(checksum_out, result.bytes, SHA256_RESULT_BYTES);
    
    return 0;
}

/**
 * Find factory defaults partition using bootrom APIs
 */
static bool find_factory_partition_info(uint32_t* start_addr, uint32_t* size) {
    pico_partition_table_t pt;
    int rc = read_partition_table(&pt);
    if (rc != 0) {
        return false;
    }
    
    if (!pt.has_partition_table || pt.partition_count == 0) {
        return false;
    }
    
    pico_partition_t p;
    while (read_next_partition(&pt, &p)) {
        if (p.partition_id == FLASH_PARTITION_FACTORY_DEFAULTS) {
            *start_addr = p.first_sector * FLASH_SECTOR_SIZE;
            *size = ((p.last_sector + 1) - p.first_sector) * FLASH_SECTOR_SIZE;
            return true;
        }
    }
    
    return false;
}

/**
 * Read partition table from bootrom
 */
static int read_partition_table(pico_partition_table_t *pt) {
    uint32_t flags = PT_INFO_PT_INFO | PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_PARTITION_ID;
    int rc = rom_get_partition_table_info(pt->table, sizeof(pt->table), flags);
    if (rc < 0) {
        pt->partition_count = 0;
        pt->status = rc;
        return rc;
    }
    
    size_t pos = 0;
    pt->fields = pt->table[pos++];
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

/**
 * Read next partition from partition table
 */
static bool read_next_partition(pico_partition_table_t *pt, pico_partition_t *p) {
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
    pt->current_partition++;
    
    return true;
}

// Manufacturing functions (only compiled in factory build)
#ifdef FACTORY_INTERNAL_VERSION

/**
 * Write factory defaults to flash partition
 */
bool factory_defaults_write(const factory_defaults_t* defaults) {
    if (!defaults) {
        return false;
    }
    
    uint32_t partition_start = 0;
    uint32_t partition_size = 0;
    
    // Find factory defaults partition
    if (!find_factory_partition_info(&partition_start, &partition_size)) {
        printf("ERROR: Factory defaults partition not found\n");
        return false;
    }
    
    // Create local copy with calculated checksum
    factory_defaults_t write_data;
    memcpy(&write_data, defaults, sizeof(factory_defaults_t));
    
    // Calculate SHA256 checksum
    if (calculate_factory_defaults_checksum(&write_data, write_data.sha256_checksum) != 0) {
        printf("ERROR: Failed to calculate checksum\n");
        return false;
    }
    
    // Erase partition first
    if (!factory_defaults_erase()) {
        printf("ERROR: Failed to erase factory defaults partition\n");
        return false;
    }
    
    // Write data to flash
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(partition_start, (const uint8_t*)&write_data, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    
    printf("Factory defaults written successfully\n");
    return true;
}

/**
 * Erase factory defaults partition
 */
bool factory_defaults_erase(void) {
    uint32_t partition_start = 0;
    uint32_t partition_size = 0;
    
    // Find factory defaults partition
    if (!find_factory_partition_info(&partition_start, &partition_size)) {
        printf("ERROR: Factory defaults partition not found\n");
        return false;
    }
    
    // Erase partition (erase full 8KB = 2 sectors)
    uint32_t ints = save_and_disable_interrupts();
    for (uint32_t offset = 0; offset < partition_size; offset += FLASH_SECTOR_SIZE) {
        flash_range_erase(partition_start + offset, FLASH_SECTOR_SIZE);
    }
    restore_interrupts(ints);
    
    printf("Factory defaults partition erased\n");
    return true;
}

#endif // FACTORY_INTERNAL_VERSION
