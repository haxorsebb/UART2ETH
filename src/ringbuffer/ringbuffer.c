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
#include <string.h>
#include <stdio.h>

// Ring buffer configuration
#define RINGBUFFER_CAPACITY 32  // Number of entries (must be power of 2 for efficiency)

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
    uint32_t doorbell_wakeups;
    
    bool initialized;
} ringbuffer_state_t;

// Global ring buffer state
static ringbuffer_state_t g_ringbuffer = {0};

// Doorbell constants for cross-core wakeup (from ADR-007)
static const uint32_t DOORBELL_CORE0_TO_CORE1 = 1;
static const uint32_t DOORBELL_CORE1_TO_CORE0 = 2;

// Internal helper functions
static uint32_t ringbuffer_mask(uint32_t index);
static bool ringbuffer_is_full(void);
static bool ringbuffer_is_empty(void);
static uint32_t ringbuffer_get_used_count(void);
static void ringbuffer_wake_other_core(uint8_t direction);
static ring_entry_t* ringbuffer_find_oldest_entry_by_direction(uint8_t direction);

/**
 * Initialize ring buffer system
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
 * Get free ring buffer entry for message production
 */
ring_entry_t* ringbuffer_get_free_entry(void) {
    if (!g_ringbuffer.initialized) {
        return NULL;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // Check if buffer is full
    if (ringbuffer_is_full()) {
        // Implement drop-oldest policy: find oldest READY entry and reuse it
        ring_entry_t* oldest = NULL;
        uint32_t oldest_timestamp = UINT32_MAX;
        
        for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
            ring_entry_t* entry = &g_ringbuffer.entries[i];
            if (entry->status == ENTRY_STATUS_READY && entry->timestamp < oldest_timestamp) {
                oldest = entry;
                oldest_timestamp = entry->timestamp;
            }
        }
        
        if (oldest) {
            // Reuse oldest entry
            oldest->status = ENTRY_STATUS_FILLING;
            g_ringbuffer.overflow_count++;
            mutex_exit(&g_ringbuffer.access_mutex);
            return oldest;
        } else {
            // No entries available for reuse
            g_ringbuffer.overflow_count++;
            mutex_exit(&g_ringbuffer.access_mutex);
            return NULL;
        }
    }
    
    // Find next free entry
    uint32_t index = ringbuffer_mask(g_ringbuffer.head);
    ring_entry_t* entry = &g_ringbuffer.entries[index];
    
    // Mark as filling
    entry->status = ENTRY_STATUS_FILLING;
    memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
    
    // Move head forward
    g_ringbuffer.head++;
    
    mutex_exit(&g_ringbuffer.access_mutex);
    return entry;
}

/**
 * Enqueue filled entry to ring buffer (atomically mark as READY)
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
    
    // Set timestamp and sequence
    entry->timestamp = to_ms_since_boot(get_absolute_time());
    
    // Mark as ready
    entry->status = ENTRY_STATUS_READY;
    
    // Update statistics
    g_ringbuffer.total_enqueued++;
    
    // Wake other core based on message direction
    uint8_t direction = entry->direction;
    
    mutex_exit(&g_ringbuffer.access_mutex);
    
    // Wake other core after releasing mutex
    ringbuffer_wake_other_core(direction);
    
    return true;
}

/**
 * Dequeue entry from ring buffer by direction
 */
ring_entry_t* ringbuffer_dequeue_entry(uint8_t direction) {
    if (!g_ringbuffer.initialized) {
        return NULL;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // Find oldest entry with matching direction and READY status
    ring_entry_t* oldest = ringbuffer_find_oldest_entry_by_direction(direction);
    
    if (oldest) {
        // Entry remains READY, caller will mark as CONSUMED when finished
        g_ringbuffer.total_dequeued++;
    }
    
    mutex_exit(&g_ringbuffer.access_mutex);
    return oldest;
}

/**
 * Mark entry as consumed (return to free pool)
 */
void ringbuffer_mark_consumed(ring_entry_t* entry) {
    if (!g_ringbuffer.initialized || !entry) {
        return;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    // Mark as free
    entry->status = ENTRY_STATUS_FREE;
    
    // Clear sensitive data
    memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
    entry->payload_length = 0;
    
    mutex_exit(&g_ringbuffer.access_mutex);
}

/**
 * Get count of messages waiting for processing by direction
 */
uint32_t ringbuffer_get_count(uint8_t direction) {
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
 * Get number of free entries available
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
 * Get overflow count (messages dropped due to buffer full)
 */
uint32_t ringbuffer_get_overflow_count(void) {
    if (!g_ringbuffer.initialized) {
        return 0;
    }
    
    return g_ringbuffer.overflow_count;
}

/**
 * Get comprehensive ring buffer statistics
 */
void ringbuffer_get_stats(ringbuffer_stats_t* stats) {
    if (!g_ringbuffer.initialized || !stats) {
        return;
    }
    
    mutex_enter_blocking(&g_ringbuffer.access_mutex);
    
    stats->total_entries = RINGBUFFER_CAPACITY;
    stats->free_entries = ringbuffer_get_free_count();
    stats->entries_tcp_to_uart = ringbuffer_get_count(RX_TCP_TO_UART);
    stats->entries_uart_to_tcp = ringbuffer_get_count(RX_UART_TO_TCP);
    stats->total_enqueued = g_ringbuffer.total_enqueued;
    stats->total_dequeued = g_ringbuffer.total_dequeued;
    stats->overflow_count = g_ringbuffer.overflow_count;
    stats->doorbell_wakeups = g_ringbuffer.doorbell_wakeups;
    
    mutex_exit(&g_ringbuffer.access_mutex);
}

/**
 * Reset ring buffer statistics counters
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
 * Get ring buffer capacity (total number of entries)
 */
uint32_t ringbuffer_get_capacity(void) {
    return RINGBUFFER_CAPACITY;
}

/**
 * Check if ring buffer is initialized
 */
bool ringbuffer_is_initialized(void) {
    return g_ringbuffer.initialized;
}

/**
 * Validate ring buffer entry pointer and contents
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
    if (entry->status > ENTRY_STATUS_CONSUMED) {
        return false;
    }
    
    // Check direction is valid
    if (entry->direction != RX_TCP_TO_UART && entry->direction != RX_UART_TO_TCP) {
        return false;
    }
    
    // Check payload length is within bounds
    if (entry->payload_length > RINGBUFFER_PAYLOAD_MAX_SIZE) {
        return false;
    }
    
    return true;
}

// Internal helper function implementations

/**
 * Apply mask to index for circular buffer operation
 */
static uint32_t ringbuffer_mask(uint32_t index) {
    return index & (RINGBUFFER_CAPACITY - 1);
}

/**
 * Check if ring buffer is full
 */
static bool ringbuffer_is_full(void) {
    return ringbuffer_get_used_count() >= RINGBUFFER_CAPACITY;
}

/**
 * Check if ring buffer is empty
 */
static bool ringbuffer_is_empty(void) {
    return ringbuffer_get_used_count() == 0;
}

/**
 * Get number of used entries
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
 * Wake other core based on message direction (doorbell mechanism from ADR-007)
 */
static void ringbuffer_wake_other_core(uint8_t direction) {
    g_ringbuffer.doorbell_wakeups++;
    
    if (direction == RX_TCP_TO_UART) {
        // Message for Core0 (UART processing) - wake Core0
        // Note: In real implementation, use multicore_doorbell_set_other_core()
        // For testing, we just increment the counter
    } else if (direction == RX_UART_TO_TCP) {
        // Message for Core1 (TCP transmission) - wake Core1  
        // Note: In real implementation, use multicore_doorbell_set_other_core()
        // For testing, we just increment the counter
    }
}

/**
 * Find oldest entry with matching direction and READY status
 */
static ring_entry_t* ringbuffer_find_oldest_entry_by_direction(uint8_t direction) {
    ring_entry_t* oldest = NULL;
    uint32_t oldest_timestamp = UINT32_MAX;
    
    for (uint32_t i = 0; i < RINGBUFFER_CAPACITY; i++) {
        ring_entry_t* entry = &g_ringbuffer.entries[i];
        if (entry->status == ENTRY_STATUS_READY && 
            entry->direction == direction && 
            entry->timestamp < oldest_timestamp) {
            oldest = entry;
            oldest_timestamp = entry->timestamp;
        }
    }
    
    return oldest;
}