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

#include <stdint.h>
#include <stdbool.h>
#include "pico/sync.h"

// SRAM Bank 4 base address and size (RP2350)
#define SRAM_BANK4_BASE     0x20040000
#define SRAM_BANK4_SIZE     (64 * 1024)  // 64KB

// System configuration structures
typedef struct {
    struct {
        uint32_t baud_rate;        // 300-500000 bps
        uint8_t  data_bits;        // 5-8 bits
        uint8_t  stop_bits;        // 1-2 bits  
        uint8_t  parity;           // 0=NONE, 1=ODD, 2=EVEN
        bool     enabled;          // Channel enabled
    } uart_channels[4];
    
    struct {
        char ip_address[16];       // "192.168.1.100"
        char subnet_mask[16];      // "255.255.255.0" 
        uint16_t tcp_ports[4];     // 4001-4004 default
        bool use_dhcp;
    } network;
    
    uint8_t log_level;             // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
    uint32_t watchdog_timeout_ms;  // Default 200ms
} system_config_t;

// Performance counters  
typedef struct {
    struct {
        uint64_t bytes_transmitted;
        uint64_t bytes_received;
        uint32_t messages_transmitted;
        uint32_t messages_received;
        uint32_t error_count;
    } uart_stats[4];
    
    uint32_t uptime_seconds;
    uint32_t ring_buffer_utilization_percent;
    uint32_t cpu_usage_percent;
} performance_counters_t;

// Log management variables
typedef struct {
    volatile uint32_t write_head;    // Next write position
    volatile uint32_t read_head;     // Core1 print position  
    uint32_t buffer_size;            // Calculated at compile time
    spin_lock_t *reservation_lock;   // For pointer updates only
} log_management_t;

// Complete shared memory layout
typedef struct {
    // Revision and integrity (at start for easy access)
    uint32_t revision_counter;       // Incremented on each config change
    uint8_t sha256_checksum[32];     // SHA256 of config + counters
    
    // Configuration structures
    system_config_t config;
    
    // Performance counters  
    performance_counters_t counters;
    
    // Log management
    log_management_t log_mgmt;
    
    // Ring buffer data (remainder of SRAM bank)
    char log_buffer[];               // Variable size, \r\n terminated strings
} shared_memory_layout_t;

// Function declarations
bool shared_memory_init(void);
uint32_t shared_memory_get_log_buffer_size(void);
shared_memory_layout_t* shared_memory_get_layout(void);

#endif // SHARED_MEMORY_H
