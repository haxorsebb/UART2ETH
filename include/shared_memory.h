/**
 * @file shared_memory.h
 * @brief Shared memory layout for config manager and log manager
 * 
 * SRAM Bank 4 Memory Layout:
 * [Config Structures][Performance Counters][Log Management][Ring Buffer Data]
 * 
 * Access Patterns:
 * - Core0: Read-only config access, read-write log access
 * - Core1: Read-write config access, read-write log access
 * 
 * Documentation Reference: 
 * - See arc42 Chapter 5 (Building Block View) - Configuration Manager
 * - See arc42 Chapter 5 (Building Block View) - Log Manager  
 * - See arc42 Chapter 6 (Runtime View) - Log Synchronization Pattern
 */

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <hardware/sync/spin_lock.h>
#include <stdint.h>
#include <stdbool.h>
#include "hardware/regs/addressmap.h"
#include "log_manager.h"  // For log_entry_t definition
#include "network/network_manager.h"  // For log_entry_t definition

// SRAM Bank 4 base address and size (RP2350)
// Note: RP2350 has non-contiguous SRAM banks: SRAM0, SRAM4, SRAM8, SRAM9
// No SRAM1-3 or SRAM5-7 exist. SRAM4 spans 0x20040000-0x20080000 (256KB total)
// but we only use 64KB for shared memory to avoid conflicts with other allocations
#define SRAM_BANK4_BASE     SRAM4_BASE        // Use official SDK constant (0x20040000)
#define SRAM_BANK4_SIZE     (64 * 1024)      // 64KB (design choice for shared memory)

/**
 * @brief channel enumeration for typing
 */
typedef enum {
    CHANNEL_0 = 0,  
    CHANNEL_1,
    CHANNEL_2,
    CHANNEL_3,
    CHANNEL_MAX,
    CHANNEL_ANY = -1
} channel_id_t;

typedef enum {
    UART_TYPE_PL011,    // Hardware UART (uart0, uart1)
    UART_TYPE_PIO       // PIO-based UART
} uart_type_t;

typedef struct {
    uint32_t    baud_rate;        // 300-500000 bps
    uint8_t     data_bits;        // 5-8 bits
    uint8_t     stop_bits;        // 1-2 bits  
    uint8_t     parity;           // 0=NONE, 1=ODD, 2=EVEN
    uint8_t     tx_gpio;          // the tx pin (GPIO, not chip-pin)
    uint8_t     rx_gpio;          // the rx pin (GPIO, not chip-pin)
    uart_type_t type;             // the UART hardware implementation PL011/PIO
    uint16_t    tcp_port;         // 4001-4004 default
    bool        enabled;          // Channel enabled
} channel_config_t;

// System configuration structures
typedef struct {
    channel_config_t channels[4];
    network_config_t network;
    uint8_t log_level;             // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
    uint32_t watchdog_timeout_ms;  // Default 200ms
} system_config_t;

// Performance counters  
typedef struct {
    struct {
        volatile uint64_t bytes_transmitted;
        volatile uint64_t bytes_received;
        volatile uint32_t messages_transmitted;
        volatile uint32_t messages_received;
        volatile uint32_t error_count;
    } uart_stats[4];
    
    volatile uint32_t uptime_seconds;
    volatile uint32_t ring_buffer_utilization_percent;
    volatile uint32_t cpu_usage_percent;
} __attribute__((aligned(4))) performance_counters_t;

// Log management variables for fixed-size entries
typedef struct {
    volatile uint32_t write_index;         // Next entry write position (in entries, not bytes)
    volatile uint32_t read_index;          // Core1 format position (in entries, not bytes)
    volatile uint32_t max_entries;         // Total number of log entries in buffer
    volatile uint32_t total_events_logged; // Total events logged across all cores
    volatile uint32_t core0_sequence;      // Per-core event sequence counter
    volatile uint32_t core1_sequence;      // Per-core event sequence counter
    spin_lock_t *entry_lock;               // For entry allocation protection
} __attribute__((aligned(4))) log_management_t;

// Complete shared memory layout
typedef struct {
    // Revision and integrity (at start for easy access)
    volatile uint32_t revision_counter;       // Incremented on each config change
    volatile bool config_change_pending;      // Flag for Core1 to detect config changes
    // TODO: Add SHA256 checksum for integrity validation in future version
    uint32_t reserved[7];                     // Reserved for future integrity features (reduced by 1)
    
    // Configuration structures
    system_config_t config;
    
    // Performance counters  
    performance_counters_t counters;
    
    // Log management
    log_management_t log_mgmt;
    
    // Log entry buffer (fixed-size entries calculated at compile time)
    log_entry_t log_entries[1];       // Flexible array member placeholder
} __attribute__((aligned(4))) shared_memory_layout_t;

// Flash persistence constants - RP2350 Partition Table Approach
#define FLASH_PERSISTENCE_RING_SIZE 4                    // 4 pages in ring buffer
#define FLASH_PERSISTENCE_PAGE_SIZE 4096                 // RP2350 flash sector size
#define FLASH_PERSISTENCE_MAGIC 0xC0FFEEAA               // Page validity marker
#define FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS 30000    // 30 seconds max write frequency
#define FLASH_PERSISTENCE_CONFIG_PARTITION_ID 2          // Configuration Data partition ID
#define FLASH_PERSISTENCE_PARTITION_SIZE (512 * 1024)   // 512KB partition size (rest of 4MB flash)

// Flash page structure for ring buffer persistence
typedef struct {
    uint32_t magic_number;                               // Page validity marker
    uint32_t revision_counter;                           // Write sequence number
    uint8_t  sha256_checksum[32];                        // Page integrity verification
    uint32_t reserved[4];                                // Future use, alignment
    
    // Complete shared memory structure (raw binary copy)
    shared_memory_layout_t shared_memory_data;
    
    // Padding to ensure flash sector alignment
    uint8_t padding[FLASH_PERSISTENCE_PAGE_SIZE - sizeof(uint32_t) * 8 - 32 - sizeof(shared_memory_layout_t)];
} __attribute__((packed, aligned(4096))) flash_persistence_page_t;

// Function declarations - Core shared memory
bool shared_memory_init(void);
bool shared_memory_force_reinit(void);  // For factory reset - forces re-initialization 
uint32_t shared_memory_get_log_buffer_capacity(void);  // Returns number of entries, not bytes
shared_memory_layout_t* shared_memory_get_layout(void);

// Function declarations - Flash persistence
bool flash_persistence_init(void);
bool flash_persistence_load_configuration(void);
bool flash_persistence_save_configuration_if_needed(void);
bool flash_persistence_force_save_configuration(void);
bool flash_persistence_save_needed(void);
void flash_persistence_factory_reset(void);
uint32_t flash_persistence_get_write_count(void);
uint32_t flash_persistence_get_corruption_count(void);
bool flash_persistence_verify_ring_buffer_integrity(void);

#endif // SHARED_MEMORY_H
