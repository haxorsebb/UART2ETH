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
#include <stddef.h>
#include <stdbool.h>


// Flash persistence constants - RP2350 Partition Table Approach
// Ring buffer geometry: 64 blocks x 8 KB = 512 KB = full configuration partition (ID=3).
// Using the whole partition with small blocks maximises the erase budget: each
// block is erased only once per 64 saves (see ADR-006).
#define FLASH_PERSISTENCE_RING_SIZE 64                   // 64 blocks in ring buffer
#define FLASH_PERSISTENCE_PAGE_SIZE 4096                 // RP2350 flash sector size
#define FLASH_PERSISTENCE_BLOCK_SIZE (2*4096)            // 8 KB per block (2 x 4096-byte sectors)
#define FLASH_PERSISTENCE_MAGIC 0xC0FFEEAA               // Page validity marker
#define FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS 120000   // 120 seconds min interval between flash writes
#define FLASH_PARTITION_FIRMWARE_A 0
#define FLASH_PARTITION_FIRMWARE_B 1
#define FLASH_PARTITION_FACTORY_DEFAULTS 2
#define FLASH_PARTITION_CONFIGURATION_DATA 3

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

// This function will be called when it's safe to call flash_range_erase
void call_flash_range_erase(void *param);
// This function will be called when it's safe to call flash_range_program
void call_flash_range_program(void *param);

#endif // FLASH_PERSISTENCE_H