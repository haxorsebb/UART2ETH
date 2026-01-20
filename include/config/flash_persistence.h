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

// Utility functions for partition table access (shared with factory_defaults)

/**
 * @brief Find partition information by partition ID
 * 
 * Uses bootrom APIs to locate partition in flash partition table.
 * 
 * @param partition_id Partition ID to search for
 * @param start_addr Output: partition start address (flash offset)
 * @param size Output: partition size in bytes
 * @return true if partition found, false otherwise
 */
bool flash_find_partition_info(uint32_t partition_id, uint32_t* start_addr, uint32_t* size);

/**
 * @brief Calculate SHA256 checksum for arbitrary data
 * 
 * Uses RP2350 hardware SHA-256 accelerator for fast hashing.
 * 
 * @param data Pointer to data to hash
 * @param size Size of data in bytes
 * @param checksum_out Output buffer for 32-byte SHA256 hash
 * @return 0 on success, -1 on error
 */
int flash_calculate_sha256(const void* data, size_t size, uint8_t* checksum_out);

#endif // FLASH_PERSISTENCE_H