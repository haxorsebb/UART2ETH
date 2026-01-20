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
#include "flash_persistence.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include <stdio.h>
#include <string.h>

// Static storage for factory defaults
static factory_defaults_t g_factory_defaults = {0};
static bool g_factory_defaults_valid = false;

// Function prototypes
static bool load_factory_defaults(void);
static bool validate_factory_defaults_integrity(const factory_defaults_t* defaults);

/**
 * Initialize factory defaults system and load from flash
 */
bool factory_defaults_init(void) {
    
    if (!load_factory_defaults()) {
        printf("WARNING: Factory defaults invalid or corrupted\n");
        g_factory_defaults_valid = false;
        return false;
    }
    
    g_factory_defaults_valid = true;
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
    
    // Find factory defaults partition using shared utility
    if (!flash_find_partition_info(FLASH_PARTITION_FACTORY_DEFAULTS, &partition_start, &partition_size)) {
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
    
    // Validate integrity
    if (!validate_factory_defaults_integrity((const factory_defaults_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + partition_start))) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_FACTORY_DEFAULTS_CHECKSUM_FAILED, 0);
        return false;
    }
    
    // Read factory defaults from flash using XIP raw flash access
    const uint8_t *flash_contents = (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + partition_start);
    memcpy(&g_factory_defaults, flash_contents, sizeof(factory_defaults_t));
    

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
    
    // Calculate SHA256 checksum of all fields after sha256_checksum using shared utility
    const uint8_t* data_start = (const uint8_t*)&defaults->production_week;
    size_t data_size = sizeof(factory_defaults_t) - offsetof(factory_defaults_t, production_week);
    
    uint8_t calculated_checksum[32];
    if (flash_calculate_sha256(data_start, data_size, calculated_checksum) != 0) {
        return false;
    }
    
    // Compare checksums
    return (memcmp(defaults->sha256_checksum, calculated_checksum, 32) == 0);
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
    
    // Find factory defaults partition using shared utility
    if (!flash_find_partition_info(FLASH_PARTITION_FACTORY_DEFAULTS, &partition_start, &partition_size)) {
        printf("ERROR: Factory defaults partition not found\n");
        return false;
    }
    
    // Create local copy with calculated checksum
    factory_defaults_t write_data;
    memcpy(&write_data, defaults, sizeof(factory_defaults_t));
    
    // Calculate SHA256 checksum using shared utility
    const uint8_t* data_start = (const uint8_t*)&write_data.production_week;
    size_t data_size = sizeof(factory_defaults_t) - offsetof(factory_defaults_t, production_week);
    
    if (flash_calculate_sha256(data_start, data_size, write_data.sha256_checksum) != 0) {
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
    
    // Find factory defaults partition using shared utility
    if (!flash_find_partition_info(FLASH_PARTITION_FACTORY_DEFAULTS, &partition_start, &partition_size)) {
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
