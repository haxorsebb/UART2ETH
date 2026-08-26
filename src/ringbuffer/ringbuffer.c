/**
 * @file ringbuffer.c
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

#include "ringbuffer.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "shared_memory.h"
#include "state_machine.h"
#include <pico/types.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

// Ring buffer configuration
#define RINGBUFFER_CAPACITY 64  // Number of entries (must be power of 2 for efficiency)

// Ring buffer state
typedef struct {
    ring_entry_t entries[RINGBUFFER_CAPACITY];
    volatile uint32_t head;         // Producer index (next write position)
    volatile uint32_t tail;         // Consumer index (next read position)
    mutex_t access_mutex;           // Mutex for thread-safe access
    
    // Statistics
    uint32_t total_enqueued;
    uint32_t total_dequeued;
    uint32_t overflow_count;
    uint32_t error_count;
    uint32_t doorbell_wakeups;
    
    bool initialized;
} ringbuffer_state_t;

// Global ring buffer state
static ringbuffer_state_t g_ringbuffer = {0};

// Internal helper functions
static uint32_t ringbuffer_mask(uint32_t index);
static bool ringbuffer_is_full(void);
static bool ringbuffer_is_empty(void);
static uint32_t ringbuffer_get_used_count(void);
static ring_entry_t* ringbuffer_find_oldest_entry_by_direction(ringbuffer_direction_t direction, channel_id_t channel, entry_status_t status);
static ring_entry_t* ringbuffer_find_oldest_ready_entry(void);
static void ringbuffer_secure_clear_entry(ring_entry_t* entry);

/**
 * @brief Initialize ring buffer system
 * @return true if initialization successful, false otherwise
 */
bool ringbuffer_init(void) {
    if (g_ringbuffer.initialized) {
        return true;  // Already initialized
    }
    
    // Initialize ring buffer state
    memset(&g_ringbuffer, 0, sizeof(ringbuffer_state_t));
    
    // Initialize mutex
    mutex_init(&g_ringbuffer.access_mutex);
    
    // Initialize all entries as FREE
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        g_ringbuffer.entries[i].status = ENTRY_STATUS_FREE;
    }
    
    g_ringbuffer.initialized = true;
    return true;
}

/**
 * @brief Get free ring buffer entry for message production
 * @return Pointer to free entry, NULL if buffer full
 * @note Returned entry has status=FILLING
 */
ring_entry_t* ringbuffer_get_free_entry(ringbuffer_direction_t direction, channel_id_t channel) {
    if (!g_ringbuffer.initialized) {
        return NULL;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // Check if buffer is full
    if (ringbuffer_is_full()) {
        // Implement drop-oldest policy: find oldest READY entry and reuse it
        ring_entry_t* oldest = ringbuffer_find_oldest_ready_entry();
        
        if (oldest) {
            // SECURITY: Validate oldest entry before reuse
            if (!ringbuffer_validate_entry(oldest)) {
                g_ringbuffer.error_count++;
            }
            
            // Reuse oldest entry with secure clearing
            ringbuffer_secure_clear_entry(oldest);
            oldest->status = ENTRY_STATUS_FILLING;
            oldest->channel = channel;
            oldest->direction = direction;
            
            g_ringbuffer.overflow_count++;
            mutex_exit(&g_ringbuffer.access_mutex);
            return oldest;
        } 
    }
    
    // Find next free entry
    uint32_t index = ringbuffer_mask(g_ringbuffer.head);
    ring_entry_t* entry = &g_ringbuffer.entries[index];
    
    // SECURITY: Validate entry is actually free
    if (entry->status != ENTRY_STATUS_FREE) {
        //should never happen
        g_ringbuffer.error_count++;
    }
    
    // Mark as filling with secure clearing
    entry->status = ENTRY_STATUS_FILLING;
    ringbuffer_secure_clear_entry(entry);
    entry->channel = channel;
    entry->direction = direction;
            
    
    // Move head forward (with overflow protection)
    g_ringbuffer.head = (g_ringbuffer.head + 1) & 0x7FFFFFFF;
    
    mutex_exit(&g_ringbuffer.access_mutex);
    return entry;
}

/**
 * @brief Enqueue filled entry to ring buffer (atomically mark as READY)
 * @param entry Pointer to filled entry (must have status=FILLING)
 * @return true if enqueue successful, false on error
 * @note Triggers doorbell wakeup for other core
 */
bool ringbuffer_enqueue_entry(ring_entry_t* entry) {
    if (!g_ringbuffer.initialized || !entry) {
        return false;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // Validate entry state
    if (entry->status != ENTRY_STATUS_FILLING) {
        mutex_exit(&g_ringbuffer.access_mutex);
        return false;
    }
    
    // Validate entry belongs to our ring buffer
    ptrdiff_t offset = entry - g_ringbuffer.entries;
    if (offset < 0 || offset >= RINGBUFFER_CAPACITY) {
        mutex_exit(&g_ringbuffer.access_mutex);
        return false;
    }
    
    // SECURITY: Validate payload length to prevent buffer overflow
    if ((entry->fill_index > RINGBUFFER_PAYLOAD_MAX_SIZE) || (entry->drain_index > RINGBUFFER_PAYLOAD_MAX_SIZE)) {
        mutex_exit(&g_ringbuffer.access_mutex);
        return false;
    }
    
    // SECURITY: Validate direction is within expected range
    if (entry->direction != RX_TCP_TO_UART && entry->direction != RX_UART_TO_TCP) {
        mutex_exit(&g_ringbuffer.access_mutex);
        return false;
    }
    
    // SECURITY: Validate UART channel is within expected range (0-4)
    if (entry->channel > 4) {
        mutex_exit(&g_ringbuffer.access_mutex);
        return false;
    }
    
    // Set timestamp and sequence (with overflow protection)
    entry->timestamp = to_ms_since_boot(get_absolute_time());
    entry->sequence_id = g_ringbuffer.total_enqueued & 0x7FFFFFFF; // Prevent overflow issues
    
    // Mark as ready
    entry->status = ENTRY_STATUS_READY;
    
    // Update statistics
    g_ringbuffer.total_enqueued++;
    
    // The consumer of this entry runs on the other core; the doorbell to ring
    // is fixed by the calling core (ADR-007, "Idle Wait Instruction and
    // Doorbell Selection"), so the entry direction is not needed here.
    wake_other_core();

    mutex_exit(&g_ringbuffer.access_mutex);

    return true;
}

/**
 * @brief Dequeue entry from ring buffer by direction
 * @param direction Message direction (RX_TCP_TO_UART or RX_UART_TO_TCP)
 * @return Pointer to ready entry, NULL if none available
 * @note Returned entry has status=READY, caller must mark CONSUMED when finished
 */
ring_entry_t* ringbuffer_dequeue_entry(ringbuffer_direction_t direction, channel_id_t channel, entry_status_t status) {
    if (!g_ringbuffer.initialized) {
        return NULL;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // Find oldest entry with matching direction and READY status
    ring_entry_t* oldest = ringbuffer_find_oldest_entry_by_direction(direction, channel, status);
    
    if (oldest) {
        // Entry remains READY, caller will mark as CONSUMED when finished
        g_ringbuffer.total_dequeued++;
    }
    
    mutex_exit(&g_ringbuffer.access_mutex);
    return oldest;
}

/**
 * @brief Mark entry as consumed (return to free pool)
 * @param entry Pointer to consumed entry
 * @note Entry status becomes FREE, available for reuse
 */
void ringbuffer_mark_consumed(ring_entry_t* entry) {
    if (!g_ringbuffer.initialized || !entry) {
        return;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // SECURITY: Validate entry belongs to our ring buffer
    ptrdiff_t offset = entry - g_ringbuffer.entries;
    if (offset < 0 || offset >= RINGBUFFER_CAPACITY) {
        mutex_exit(&g_ringbuffer.access_mutex);
        return;
    }
    
    // SECURITY: Only allow consuming READY entries
    if (entry->status != ENTRY_STATUS_READY) {
        mutex_exit(&g_ringbuffer.access_mutex);
        return;
    }
    
    // Mark as free and secure clear all data
    entry->status = ENTRY_STATUS_FREE;
    ringbuffer_secure_clear_entry(entry);
    
    mutex_exit(&g_ringbuffer.access_mutex);
}

/**
 * @brief Get count of messages waiting for processing by direction
 * @param direction Message direction (RX_TCP_TO_UART or RX_UART_TO_TCP)
 * @return Number of entries ready for processing
 */
uint32_t ringbuffer_get_count(ringbuffer_direction_t direction) {
    if (!g_ringbuffer.initialized) {
        return 0;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        ring_entry_t* entry = &g_ringbuffer.entries[i];
        if (entry->status == ENTRY_STATUS_READY && entry->direction == direction) {
            count++;
        }
    }
    
    mutex_exit(&g_ringbuffer.access_mutex);
    return count;
}

/**
 * @brief Get number of free entries available
 * @return Number of entries available for new messages
 */
uint32_t ringbuffer_get_free_count(void) {
    if (!g_ringbuffer.initialized) {
        return 0;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    uint32_t free_count = 0;
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        if (g_ringbuffer.entries[i].status == ENTRY_STATUS_FREE) {
            free_count++;
        }
    }
    
    mutex_exit(&g_ringbuffer.access_mutex);
    return free_count;
}

/**
 * @brief Get overflow count (messages dropped due to buffer full)
 * @return Number of messages dropped since initialization
 */
uint32_t ringbuffer_get_overflow_count(void) {
    if (!g_ringbuffer.initialized) {
        return 0;
    }
    
    return g_ringbuffer.overflow_count;
}

/**
 * @brief Get comprehensive ring buffer statistics
 * @param stats Output structure for statistics (must not be NULL)
 */
void ringbuffer_get_stats(ringbuffer_stats_t* stats) {
    if (!g_ringbuffer.initialized || !stats) {
        return;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // Count entries by type within mutex protection
    uint32_t free_count = 0;
    uint32_t tcp_to_uart_count = 0;
    uint32_t uart_to_tcp_count = 0;
    
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        ring_entry_t* entry = &g_ringbuffer.entries[i];
        if (entry->status == ENTRY_STATUS_FREE) {
            free_count++;
        } else if (entry->status == ENTRY_STATUS_READY) {
            if (entry->direction == RX_TCP_TO_UART) {
                tcp_to_uart_count++;
            } else if (entry->direction == RX_UART_TO_TCP) {
                uart_to_tcp_count++;
            }
        }
    }
    
    stats->total_entries = RINGBUFFER_CAPACITY;
    stats->free_entries = free_count;
    stats->entries_tcp_to_uart = tcp_to_uart_count;
    stats->entries_uart_to_tcp = uart_to_tcp_count;
    stats->total_enqueued = g_ringbuffer.total_enqueued;
    stats->total_dequeued = g_ringbuffer.total_dequeued;
    stats->overflow_count = g_ringbuffer.overflow_count;
    stats->doorbell_wakeups = g_ringbuffer.doorbell_wakeups;
    
    mutex_exit(&g_ringbuffer.access_mutex);
}

/**
 * @brief Reset ring buffer statistics counters
 * @note Does not affect ring buffer contents or configuration
 */
void ringbuffer_reset_statistics(void) {
    if (!g_ringbuffer.initialized) {
        return;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    g_ringbuffer.total_enqueued = 0;
    g_ringbuffer.total_dequeued = 0;
    g_ringbuffer.overflow_count = 0;
    g_ringbuffer.doorbell_wakeups = 0;
    
    mutex_exit(&g_ringbuffer.access_mutex);
}

/**
 * @brief Get ring buffer capacity (total number of entries)
 * @return Total ring buffer capacity
 */
uint32_t ringbuffer_get_capacity(void) {
    return RINGBUFFER_CAPACITY;
}

/**
 * @brief Check if ring buffer is initialized
 * @return true if initialized, false otherwise
 */
bool ringbuffer_is_initialized(void) {
    return g_ringbuffer.initialized;
}

/**
 * @brief Validate ring buffer entry pointer and contents
 * @param entry Pointer to entry to validate
 * @return true if entry is valid, false otherwise
 */
bool ringbuffer_validate_entry(const ring_entry_t* entry) {
    if (!entry || !g_ringbuffer.initialized) {
        return false;
    }
    
    // Check if entry is within our ring buffer array
    ptrdiff_t offset = entry - g_ringbuffer.entries;
    if (offset < 0 || offset >= RINGBUFFER_CAPACITY) {
        return false;
    }
    
    // Check entry status is valid
    if (entry->status >= ENTRY_STATUS_MAX) {
        return false;
    }
    
    // Check direction is valid
    if (entry->direction != RX_TCP_TO_UART && entry->direction != RX_UART_TO_TCP) {
        return false;
    }
    
    // Check payload length is within bounds
    if ((entry->fill_index > RINGBUFFER_PAYLOAD_MAX_SIZE || (entry->drain_index > RINGBUFFER_PAYLOAD_MAX_SIZE))) {
        return false;
    }
    
    return true;
}

// Internal helper function implementations

/**
 * @brief Apply mask to index for circular buffer operation
 * @param index Raw index value
 * @return Masked index within buffer bounds
 */
static uint32_t ringbuffer_mask(uint32_t index) {
    return index & (RINGBUFFER_CAPACITY - 1);
}

/**
 * @brief Check if ring buffer is full
 * @return true if buffer is full, false otherwise
 */
static bool ringbuffer_is_full(void) {
    return ringbuffer_get_used_count() >= RINGBUFFER_CAPACITY;
}

/**
 * @brief Check if ring buffer is empty
 * @return true if buffer is empty, false otherwise
 */
static bool ringbuffer_is_empty(void) {
    return ringbuffer_get_used_count() == 0;
}

/**
 * @brief Get number of used entries
 * @return Number of entries not in FREE status
 */
static uint32_t ringbuffer_get_used_count(void) {
    uint32_t used_count = 0;
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        if (g_ringbuffer.entries[i].status != ENTRY_STATUS_FREE) {
            used_count++;
        }
    }
    return used_count;
}

/**
 * @brief Find oldest entry with matching direction and READY status
 * @param direction Message direction to search for
 * @return Pointer to oldest ready entry, NULL if none found
 */
static ring_entry_t* ringbuffer_find_oldest_entry_by_direction(ringbuffer_direction_t direction, channel_id_t channel, entry_status_t status) {
    ring_entry_t* oldest = NULL;
    uint32_t oldest_sequence = UINT32_MAX;
    
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        ring_entry_t* entry = &g_ringbuffer.entries[i];
        if (entry->status == status && 
            entry->direction == direction && 
            ((entry->channel == channel || channel == CHANNEL_ANY)) && 
            entry->sequence_id < oldest_sequence) {
            oldest = entry;
            oldest_sequence = entry->sequence_id;
        }
    }
    
    return oldest;
}

/**
 * @brief Find oldest ready entry for reuse during overflow
 * @return Pointer to oldest ready entry, NULL if none found
 */
static ring_entry_t* ringbuffer_find_oldest_ready_entry(void) {
    ring_entry_t* oldest = NULL;
    uint32_t oldest_sequence = UINT32_MAX;
    
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        ring_entry_t* entry = &g_ringbuffer.entries[i];
        if (entry->status == ENTRY_STATUS_READY && entry->sequence_id < oldest_sequence) {
            oldest = entry;
            oldest_sequence = entry->sequence_id;
        }
    }
    
    return oldest;
}

/**
 * @brief Securely clear all sensitive data from ring buffer entry
 * @param entry Pointer to entry to clear (must not be NULL)
 * @note Clears payload, reserved fields, and resets all metadata
 */
static void ringbuffer_secure_clear_entry(ring_entry_t* entry) {
    if (!entry) {
        return;
    }
    
    // Clear all payload data
    memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
    
    // SECURITY: Clear reserved fields to prevent information disclosure
    memset(entry->reserved, 0, sizeof(entry->reserved));
    
    // Reset metadata (but preserve status which is set by caller)
    entry->channel = CHANNEL_ANY;
    entry->direction = DIRECTION_NONE;
    entry->fill_index = 0;
    entry->drain_index = 0;
    entry->timestamp = 0;
    entry->sequence_id = 0;
}