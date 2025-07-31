/**
 * @file log_manager.c
 * @brief Implementation of log manager with lock-reserve-release-write pattern
 * 
 * Implements dual-core logging as documented in arc42 architecture.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Log Manager Implementation
 * - arc42 Chapter 6 - Runtime View - Log Synchronization Pattern
 */

#include "log_manager.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// Static variables for log manager
static bool g_log_initialized = false;
static shared_memory_layout_t* g_shared_layout = NULL;
static uint32_t g_total_messages = 0;

/**
 * Get system timestamp (simplified for testing)
 */
static uint32_t get_system_timestamp(void) {
    return (uint32_t)(get_absolute_time() / 1000);  // Convert to milliseconds
}

/**
 * Calculate number of messages currently pending
 */
static uint32_t calculate_pending_messages(void) {
    if (!g_shared_layout) return 0;
    
    uint32_t write_head = g_shared_layout->log_mgmt.write_head;
    uint32_t read_head = g_shared_layout->log_mgmt.read_head;
    uint32_t buffer_size = g_shared_layout->log_mgmt.buffer_size;
    
    if (write_head >= read_head) {
        return write_head - read_head;
    } else {
        return (buffer_size - read_head) + write_head;
    }
}

/**
 * Initialize log manager (must be called after shared_memory_init)
 * 
 * @return true if initialization successful, false otherwise
 */
bool log_manager_init(void) {
    if (g_log_initialized) {
        return true;  // Already initialized
    }
    
    // Get shared memory layout
    g_shared_layout = shared_memory_get_layout();
    if (!g_shared_layout) {
        return false;  // Shared memory not initialized
    }
    
    // Reset counters
    g_total_messages = 0;
    
    g_log_initialized = true;
    return true;
}

/**
 * Log a message using lock-reserve-release-write pattern
 * Thread-safe for both cores
 * 
 * @param core_id Core ID (0 or 1)
 * @param level Log level
 * @param message Message text (max LOG_MESSAGE_MAX_LENGTH chars)
 * @return true if message was queued, false if buffer full or error
 */
bool log_message(uint8_t core_id, log_level_t level, const char* message) {
    if (!g_log_initialized || !g_shared_layout || !message) {
        return false;
    }
    
    // Format message with timestamp and core ID
    char formatted_msg[LOG_FORMATTED_MAX_LENGTH];
    uint32_t timestamp = get_system_timestamp();
    
    int msg_len = snprintf(formatted_msg, sizeof(formatted_msg), 
                          "[%08u][%u] %s\r\n", timestamp, core_id, message);
    
    if (msg_len <= 0 || msg_len >= LOG_FORMATTED_MAX_LENGTH) {
        return false;  // Formatting error or message too long
    }
    
    // PHASE 1: Lock-Reserve-Release (minimal lock time)
    spin_lock_unsafe_blocking(g_shared_layout->log_mgmt.reservation_lock);
    
    uint32_t buffer_size = g_shared_layout->log_mgmt.buffer_size;
    uint32_t write_head = g_shared_layout->log_mgmt.write_head;
    uint32_t read_head = g_shared_layout->log_mgmt.read_head;
    
    // Check if we have enough space (corrected ring buffer calculation)
    uint32_t available_space;
    if (write_head >= read_head) {
        // Space from write_head to end + space from start to read_head - 1 (keep one byte free)
        available_space = (buffer_size - write_head) + read_head - 1;
    } else {
        // Space from write_head to read_head - 1 (keep one byte free)
        available_space = read_head - write_head - 1;
    }
    
    if (available_space < (uint32_t)msg_len) {
        spin_unlock_unsafe(g_shared_layout->log_mgmt.reservation_lock);
        return false;  // Buffer full
    }
    
    // Reserve space
    uint32_t reserved_pos = write_head;
    uint32_t new_write_head = (write_head + msg_len) % buffer_size;
    g_shared_layout->log_mgmt.write_head = new_write_head;
    
    spin_unlock_unsafe(g_shared_layout->log_mgmt.reservation_lock);
    
    // PHASE 2: Lock-free Write (space guaranteed)
    if (reserved_pos + msg_len <= buffer_size) {
        // Simple case: no wrap-around
        memcpy(&g_shared_layout->log_buffer[reserved_pos], formatted_msg, msg_len);
    } else {
        // Wrap-around case: split the write
        uint32_t first_part = buffer_size - reserved_pos;
        uint32_t second_part = msg_len - first_part;
        
        memcpy(&g_shared_layout->log_buffer[reserved_pos], formatted_msg, first_part);
        memcpy(&g_shared_layout->log_buffer[0], formatted_msg + first_part, second_part);
    }
    
    // Update total count
    g_total_messages++;
    
    return true;
}

/**
 * Core1 background task: print all pending messages to USB-serial
 * Should be called periodically (100ms) by Core1 only
 * 
 * @return Number of messages printed
 */
uint32_t log_manager_print_pending(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    uint32_t printed_count = 0;
    uint32_t buffer_size = g_shared_layout->log_mgmt.buffer_size;
    
    while (g_shared_layout->log_mgmt.read_head != g_shared_layout->log_mgmt.write_head) {
        uint32_t read_pos = g_shared_layout->log_mgmt.read_head;
        
        // Find the end of the current message (look for \r\n)
        uint32_t msg_end = read_pos;
        bool found_end = false;
        
        for (uint32_t i = 0; i < 300; i++) {  // Max message scan length
            char c = g_shared_layout->log_buffer[(read_pos + i) % buffer_size];
            if (c == '\n' && i > 0) {
                char prev_c = g_shared_layout->log_buffer[(read_pos + i - 1) % buffer_size];
                if (prev_c == '\r') {
                    msg_end = (read_pos + i + 1) % buffer_size;
                    found_end = true;
                    break;
                }
            }
        }
        
        if (!found_end) {
            break;  // Corrupted message or incomplete message
        }
        
        // Extract and print message
        char message_buf[LOG_FORMATTED_MAX_LENGTH];
        uint32_t msg_len = (msg_end >= read_pos) ? (msg_end - read_pos) : 
                          (buffer_size - read_pos + msg_end);
        
        if (msg_len < LOG_FORMATTED_MAX_LENGTH) {
            // Extract message
            if (msg_end > read_pos) {
                // No wrap-around
                memcpy(message_buf, &g_shared_layout->log_buffer[read_pos], msg_len);
            } else {
                // Wrap-around
                uint32_t first_part = buffer_size - read_pos;
                memcpy(message_buf, &g_shared_layout->log_buffer[read_pos], first_part);
                memcpy(message_buf + first_part, &g_shared_layout->log_buffer[0], msg_end);
            }
            
            message_buf[msg_len] = '\0';
            
            // Print to USB-serial stdout
            printf("%s", message_buf);
            fflush(stdout);
            
            printed_count++;
        }
        
        // Update read head
        g_shared_layout->log_mgmt.read_head = msg_end;
    }
    
    return printed_count;
}

/**
 * Get current log buffer utilization percentage
 * 
 * @return Percentage (0-100) of log buffer currently used
 */
uint32_t log_manager_get_utilization(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    uint32_t pending_bytes = calculate_pending_messages();
    uint32_t buffer_size = g_shared_layout->log_mgmt.buffer_size;
    
    if (buffer_size == 0) return 0;
    
    return (pending_bytes * 100) / buffer_size;
}

/**
 * Get number of messages currently waiting to be printed
 * 
 * @return Number of pending messages
 */
uint32_t log_manager_get_pending_count(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    // For simplicity, approximate by counting \n characters in pending data
    uint32_t write_head = g_shared_layout->log_mgmt.write_head;
    uint32_t read_head = g_shared_layout->log_mgmt.read_head;
    uint32_t buffer_size = g_shared_layout->log_mgmt.buffer_size;
    
    if (read_head == write_head) return 0;
    
    uint32_t message_count = 0;
    uint32_t pos = read_head;
    
    while (pos != write_head) {
        if (g_shared_layout->log_buffer[pos] == '\n') {
            message_count++;
        }
        pos = (pos + 1) % buffer_size;
    }
    
    return message_count;
}

/**
 * Get total number of messages logged since startup
 * 
 * @return Total message count
 */
uint32_t log_manager_get_total_count(void) {
    return g_total_messages;
}
