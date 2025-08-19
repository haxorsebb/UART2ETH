/**
 * @file ringbuffer.h
 * @brief Ring buffer implementation for UART-TCP message bridging
 * 
 * Implements bidirectional ring buffer communication between Core0 (UART) 
 * and Core1 (TCP/network) with doorbell-based synchronization.
 * 
 * Documentation Reference:
 * - ADR-011: Ring Buffer Implementation for UART-TCP Message Bridging
 * - ADR-005: Ring Buffer Memory Allocation Strategy  
 * - ADR-007: State Machine Architecture (doorbell mechanism)
 * - Issue #68: Add ringbuffer implementation
 */

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>     // For memset, memcpy

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Message direction enumeration
 */
typedef enum {
    RX_TCP_TO_UART = 0,  // Message from TCP client to UART (to be echoed)
    RX_UART_TO_TCP = 1   // Message from UART to TCP client (echo response)
} ringbuffer_direction_t;

/**
 * @brief Entry status enumeration
 */
typedef enum {
    ENTRY_STATUS_FREE = 0,      // Entry available for use
    ENTRY_STATUS_FILLING = 1,   // Entry being filled by producer
    ENTRY_STATUS_READY = 2,     // Entry ready for consumer
    ENTRY_STATUS_CONSUMED = 3   // Entry consumed, ready for cleanup
} entry_status_t;

#define ENTRY_STATUS_FREE      0  // Entry available for use
#define ENTRY_STATUS_FILLING   1  // Entry being filled by producer
#define ENTRY_STATUS_READY     2  // Entry ready for consumer
#define ENTRY_STATUS_CONSUMED  3  // Entry consumed, ready for cleanup

// Ring buffer configuration
#define RINGBUFFER_ENTRY_SIZE        1088  // 64-byte header + 1024-byte payload
#define RINGBUFFER_PAYLOAD_MAX_SIZE  1024  // Maximum payload size per ADR-005
#define RINGBUFFER_HEADER_SIZE       64    // Cache-aligned header size

/**
 * @brief Ring buffer entry structure
 * 
 * Fixed-size entry with cache-aligned header and 1024-byte payload capacity.
 * Implements static worst-case allocation per ADR-005.
 */
typedef struct {
    // Management header
    uint8_t  uart_channel;     // 0-3 for UART channels 
    uint8_t  direction;        // RX_TCP_TO_UART, RX_UART_TO_TCP
    uint8_t  status;           // Entry status (FREE, FILLING, READY, CONSUMED)
    uint32_t  payload_length;   // Actual data length (≤1024)
    uint32_t timestamp;        // Message timestamp (milliseconds since boot)
    uint32_t sequence_id;      // For ordering/debugging
    uint32_t reserved[11];     
    
    // Payload data (1024 bytes fixed)
    uint8_t  payload[RINGBUFFER_PAYLOAD_MAX_SIZE];    // Protocol message data
} __attribute__((aligned(64))) ring_entry_t;

/**
 * @brief Ring buffer statistics structure
 */
typedef struct {
    uint32_t total_entries;         // Total ring buffer capacity
    uint32_t free_entries;          // Currently available entries
    uint32_t entries_tcp_to_uart;   // Entries waiting for Core0 processing
    uint32_t entries_uart_to_tcp;   // Entries waiting for Core1 transmission
    uint32_t total_enqueued;        // Total messages enqueued since init
    uint32_t total_dequeued;        // Total messages dequeued since init  
    uint32_t overflow_count;        // Messages dropped due to buffer full
    uint32_t doorbell_wakeups;      // Cross-core doorbell activations
} ringbuffer_stats_t;

/**
 * @brief Ring Buffer Core API
 */

/**
 * Initialize ring buffer system
 * @return true if initialization successful, false otherwise
 */
bool ringbuffer_init(void);

/**
 * Get free ring buffer entry for message production
 * @return Pointer to free entry, NULL if buffer full
 * @note Returned entry has status=FILLING, caller must set direction and payload
 */
ring_entry_t* ringbuffer_get_free_entry(void);

/**
 * Enqueue filled entry to ring buffer (atomically mark as READY)
 * @param entry Pointer to filled entry (must have status=FILLING)
 * @return true if enqueue successful, false on error
 * @note Triggers doorbell wakeup for other core
 */
bool ringbuffer_enqueue_entry(ring_entry_t* entry);

/**
 * Dequeue entry from ring buffer by direction
 * @param direction Message direction (RX_TCP_TO_UART or RX_UART_TO_TCP)
 * @return Pointer to ready entry, NULL if none available
 * @note Returned entry has status=READY, caller must mark CONSUMED when finished
 */
ring_entry_t* ringbuffer_dequeue_entry(uint8_t direction);

/**
 * Mark entry as consumed (return to free pool)
 * @param entry Pointer to consumed entry
 * @note Entry status becomes FREE, available for reuse
 */
void ringbuffer_mark_consumed(ring_entry_t* entry);

/**
 * @brief Ring Buffer Statistics and Monitoring
 */

/**
 * Get count of messages waiting for processing by direction
 * @param direction Message direction (RX_TCP_TO_UART or RX_UART_TO_TCP)
 * @return Number of entries ready for processing
 */
uint32_t ringbuffer_get_count(uint8_t direction);

/**
 * Get number of free entries available
 * @return Number of entries available for new messages
 */
uint32_t ringbuffer_get_free_count(void);

/**
 * Get overflow count (messages dropped due to buffer full)
 * @return Number of messages dropped since initialization
 */
uint32_t ringbuffer_get_overflow_count(void);

/**
 * Get comprehensive ring buffer statistics
 * @param stats Output structure for statistics
 */
void ringbuffer_get_stats(ringbuffer_stats_t* stats);

/**
 * Reset ring buffer statistics counters
 * @note Does not affect ring buffer contents or configuration
 */
void ringbuffer_reset_statistics(void);

/**
 * @brief Ring Buffer Internal Functions (for testing)
 */

/**
 * Get ring buffer capacity (total number of entries)
 * @return Total ring buffer capacity
 */
uint32_t ringbuffer_get_capacity(void);

/**
 * Check if ring buffer is initialized
 * @return true if initialized, false otherwise
 */
bool ringbuffer_is_initialized(void);

/**
 * Validate ring buffer entry pointer and contents
 * @param entry Pointer to entry to validate
 * @return true if entry is valid, false otherwise
 */
bool ringbuffer_validate_entry(const ring_entry_t* entry);

#ifdef __cplusplus
}
#endif

#endif // RINGBUFFER_H