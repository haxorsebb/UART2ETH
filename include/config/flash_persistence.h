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

#ifndef FLASH_PERSISTENCE_H
#define FLASH_PERSISTENCE_H

#include <stdint.h>
#include <stdbool.h>

// Flash persistence state management

typedef struct {
    uint8_t  current_write_block;              // next page to write  
    uint8_t  last_valid_block;                 // last successfully read
    uint32_t total_writes_lifetime;           // Total write operations
    uint32_t corruption_events;               // Detected corruption count
    uint32_t last_written_revision;           // Change detection state
    uint32_t last_write_timestamp_ms;         // Write frequency control    
    uint8_t  active_config_partition_id;      // Current config partition (2 or 3)
    uint32_t partition_start_offset;          // Partition start address
    uint32_t partition_size;                  // Partition size in bytes
    bool     initialized;                     // Initialization status
    bool     write_in_progress;               // defer writes at least until the current write is finished
} flash_persistence_state_t;


flash_persistence_state_t* get_persistence_state(void);


#endif // FLASH_PERSISTENCE_H