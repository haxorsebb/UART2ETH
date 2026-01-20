/**
 * @file factory_defaults.h
 * @brief Factory defaults storage and management for device-specific configuration
 * 
 * Provides read-only access to factory-programmed device configuration including
 * serial numbers, MAC addresses, board types, and default network settings.
 * Factory defaults are stored in a dedicated flash partition (ID=2, 8KB) separate
 * from user configuration.
 * 
 * Integration:
 * - Loaded early in boot sequence (before factory reset check)
 * - Applied to shared memory during factory reset operations
 * - Serial number displayed in boot messages for field identification
 * 
 * Documentation Reference:
 * - ADR-014: Factory Defaults Implementation Strategy
 * - arc42 Chapter 5 - Configuration Manager - Factory Defaults
 */

#ifndef FACTORY_DEFAULTS_H
#define FACTORY_DEFAULTS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Board type enumeration for hardware variant identification
 */
typedef enum {
    BOARD_TYPE_STANDARD_4CH = 0,    // Standard 4-channel version
    BOARD_TYPE_COMPACT_2CH = 1,     // Compact 2-channel version  
    BOARD_TYPE_INDUSTRIAL_4CH = 2   // Industrial 4-channel version
} board_type_t;

/**
 * @brief Factory defaults data structure (88 bytes + padding to sector alignment)
 * 
 * Note: sha256_checksum is placed first to simplify exclusion during hash calculation.
 * The hash covers all fields after sha256_checksum.
 * Total structure size fits within 8KB partition.
 */
typedef struct {
    uint8_t sha256_checksum[32];       // SHA256 integrity verification (FIRST for easy hash exclusion)
    
    // Serial number (8 bytes total) - Format: YYWW-NNNNNN
    uint8_t production_week;           // Week number 1-52
    uint8_t production_year;           // Years since 2000 (e.g., 26 for 2026)
    uint8_t serial_number[6];          // 6-byte running number (unique per device)
    
    // Network identity (6 bytes)
    uint8_t mac_address[6];            // Raw MAC address (uniqueness ensured externally)
    
    // Board type (1 byte)
    uint8_t board_type;                // board_type_t enum value
    
    // Default network configuration (9 bytes)
    uint32_t default_ip;               // Default IP address (network byte order)
    uint32_t default_netmask;          // Default netmask (network byte order)
    bool default_dhcp_enable;          // Default DHCP enable flag
    
    // Security (32 bytes)
    char default_password[32];         // Factory default password (including null-terminator)
    
    // Reserved for future expansion (pad to clean alignment)
    uint8_t reserved[56];              // Pad to 136 bytes total
} __attribute__((packed)) factory_defaults_t;

/**
 * @brief Initialize factory defaults system and load from flash
 * 
 * Should be called early in boot sequence, immediately after stdio_uart_init.
 * Reads factory defaults from FLASH_PARTITION_FACTORY_DEFAULTS (ID=2).
 * Validates integrity using SHA256 checksum.
 * 
 * @return true if factory defaults loaded successfully, false if invalid/corrupted
 * 
 * @note System continues boot even if factory defaults invalid (uses built-in defaults)
 */
bool factory_defaults_init(void);

/**
 * @brief Print serial number to console for field identification
 * 
 * Formats serial number as: YYWW-NNNNNN
 * Example: 2601-0A1B2C = Week 1 of 2026, serial 0x0A1B2C
 * 
 * Should be called after factory_defaults_init() and after software version display.
 */
void factory_defaults_print_serial_number(void);

/**
 * @brief Apply factory defaults to shared memory configuration
 * 
 * Called during factory reset operations. Copies factory default values
 * to corresponding fields in g_shared_memory:
 * - default_ip → network.static_ip
 * - default_netmask → network.static_netmask
 * - default_dhcp_enable → network.use_dhcp
 * - default_password → (user authentication system)
 * - mac_address → network.mac_address
 * - board_type → (system configuration)
 * 
 * Does not modify shared_memory directly - caller must save to flash.
 * 
 * @note Requires factory_defaults_init() to have succeeded
 */
void factory_defaults_apply_to_config(void);

/**
 * @brief Get read-only pointer to factory defaults structure
 * 
 * @return Pointer to factory defaults, or NULL if not initialized
 * 
 * @warning Returned pointer is read-only. Do not modify.
 */
const factory_defaults_t* factory_defaults_get(void);

/**
 * @brief Check if factory defaults are valid and loaded
 * 
 * @return true if factory defaults loaded and valid, false otherwise
 */
bool factory_defaults_is_valid(void);

// Manufacturing functions (only compiled in factory build configuration)
#ifdef FACTORY_INTERNAL_VERSION

/**
 * @brief Write factory defaults to flash partition (manufacturing use only)
 * 
 * Calculates SHA256 checksum and writes complete factory_defaults_t structure
 * to FLASH_PARTITION_FACTORY_DEFAULTS. Erases partition before writing.
 * 
 * @param defaults Pointer to factory defaults structure to write
 * @return true if write successful, false on error
 * 
 * @warning This function erases and reprograms flash. Only use during manufacturing.
 * @note Requires FACTORY_INTERNAL_VERSION defined at compile time
 */
bool factory_defaults_write(const factory_defaults_t* defaults);

/**
 * @brief Erase factory defaults partition (manufacturing use only)
 * 
 * Erases FLASH_PARTITION_FACTORY_DEFAULTS partition.
 * 
 * @return true if erase successful, false on error
 * 
 * @warning This function erases flash. Only use during manufacturing.
 * @note Requires FACTORY_INTERNAL_VERSION defined at compile time
 */
bool factory_defaults_erase(void);

#endif // FACTORY_INTERNAL_VERSION

#ifdef __cplusplus
}
#endif

#endif // FACTORY_DEFAULTS_H
