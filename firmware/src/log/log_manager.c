/**
 * @file log_manager.c
 * @brief Implementation of log manager with lock-reserve-release-write pattern
 * 
 * Implements dual-core logging with thread-safe message reservation and
 * Core1-only background printing as documented in arc42.
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

// Static variables for log manager state
static bool g_log_manager_initialized = false;
static shared_memory_layout_t* g_shared_memory = NULL;
static uint32_t g_total_messages_logged = 0;

/**
 * Calculate number of bytes used in circular buffer
 */
static uint32_t calculate_bytes_used(void) {
    if (!g_shared_memory) return 0;
    
    uint32_t write_head = g_shared_memory->log_mgmt.write_head;
    uint32_t read_head = g_shared_memory->log_mgmt.read_head;
    uint32_t buffer_size = g_shared_memory->log_mgmt.buffer_size;
    
    if (write_head >= read_head) {
        return write_head - read_head;
    } else {
        return buffer_size - read_head + write_head;
    }
}

/**
 * Count number of pending messages by scanning for \r\n terminators
 */
static uint32_t count_pending_messages(void) {
    if (!g_shared_memory) return 0;
    
    uint32_t count = 0;
    uint32_t read_head = g_shared_memory->log_mgmt.read_head;
    uint32_t write_head = g_shared_memory->log_mgmt.write_head;
    uint32_t buffer_size = g_shared_memory->log_mgmt.buffer_size;
    
    uint32_t pos = read_head;
    while (pos != write_head) {
        if (g_shared_memory->log_buffer[pos] == '\n' && 
            pos > 0 && g_shared_memory->log_buffer[pos-1] == '\r') {
            count++;
        }
        pos = (pos + 1) % buffer_size;
    }
    
    return count;
}

/**
 * Initialize log manager (must be called after shared_memory_init)
 * 
 * @return true if initialization successful, false otherwise
 */
bool log_manager_init(void) {
    if (g_log_manager_initialized) {
        return true;  // Already initialized
    }
    
    // Get shared memory layout
    g_shared_memory = shared_memory_get_layout();
    if (!g_shared_memory) {
        return false;  // Shared memory not initialized
    }
    
    // Initialize counters
    g_total_messages_logged = 0;
    
    g_log_manager_initialized = true;
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
    if (!g_log_manager_initialized || !g_shared_memory) {
        return false;
    }
    
    if (core_id > 1) {
        return false;  // Invalid core ID
    }
    
    // Format message with timestamp and core ID
    char formatted_msg[LOG_FORMATTED_MAX_LENGTH];
    uint32_t timestamp = to_ms_since_boot(get_absolute_time());
    
    // Format: [TTTTTTTT][C] Message\r\n
    int msg_len = snprintf(formatted_msg, sizeof(formatted_msg), 
                          "[%08u][%u] %s\r\n", timestamp, core_id, message);
    
    if (msg_len >= sizeof(formatted_msg)) {
        return false;  // Message too long
    }
    
    // PHASE 1: Lock-Reserve-Release (minimal lock time)
    uint32_t save = spin_lock_blocking(g_shared_memory->log_mgmt.reservation_lock);
    
    uint32_t buffer_size = g_shared_memory->log_mgmt.buffer_size;
    uint32_t bytes_available = buffer_size - calculate_bytes_used();
    
    if (bytes_available < msg_len) {
        spin_unlock(g_shared_memory->log_mgmt.reservation_lock, save);
        return false;  // Buffer full
    }
    
    uint32_t reserved_pos = g_shared_memory->log_mgmt.write_head;
    g_shared_memory->log_mgmt.write_head = (g_shared_memory->log_mgmt.write_head + msg_len) % buffer_size;
    
    spin_unlock(g_shared_memory->log_mgmt.reservation_lock, save);
    
    // PHASE 2: Lock-free Write (space guaranteed)
    for (int i = 0; i < msg_len; i++) {
        g_shared_memory->log_buffer[(reserved_pos + i) % buffer_size] = formatted_msg[i];
    }
    
    // Update total count (atomic increment)
    g_total_messages_logged++;
    
    return true;
}

/**
 * Core1 background task: print all pending messages to USB-serial
 * Should be called periodically (100ms) by Core1 only
 * 
 * @return Number of messages printed
 */
uint32_t log_manager_print_pending(void) {
    if (!g_log_manager_initialized || !g_shared_memory) {
        return 0;
    }
    
    uint32_t messages_printed = 0;
    uint32_t buffer_size = g_shared_memory->log_mgmt.buffer_size;
    char temp_buffer[LOG_FORMATTED_MAX_LENGTH];
    
    while (g_shared_memory->log_mgmt.read_head != g_shared_memory->log_mgmt.write_head) {
        // Find next message (scan until \r\n)
        uint32_t start_pos = g_shared_memory->log_mgmt.read_head;
        uint32_t pos = start_pos;
        uint32_t msg_len = 0;
        bool found_terminator = false;
        
        // Scan for \r\n terminator
        while (pos != g_shared_memory->log_mgmt.write_head && msg_len < sizeof(temp_buffer) - 1) {
            temp_buffer[msg_len] = g_shared_memory->log_buffer[pos];
            msg_len++;
            
            if (msg_len >= 2 && temp_buffer[msg_len-2] == '\r' && temp_buffer[msg_len-1] == '\n') {
                found_terminator = true;
                break;
            }
            
            pos = (pos + 1) % buffer_size;
        }
        
        if (!found_terminator) {
            break;  // No complete message found
        }
        
        // Null-terminate and print
        temp_buffer[msg_len] = '\0';
        printf("%s", temp_buffer);  // Already has \r\n
        fflush(stdout);
        
        // Advance read head
        g_shared_memory->log_mgmt.read_head = (start_pos + msg_len) % buffer_size;
        messages_printed++;
    }
    
    return messages_printed;
}

/**
 * Get current log buffer utilization percentage
 * 
 * @return Percentage (0-100) of log buffer currently used
 */
uint32_t log_manager_get_utilization(void) {
    if (!g_log_manager_initialized || !g_shared_memory) {
        return 0;
    }
    
    uint32_t bytes_used = calculate_bytes_used();
    uint32_t buffer_size = g_shared_memory->log_mgmt.buffer_size;
    
    if (buffer_size == 0) return 0;
    
    return (bytes_used * 100) / buffer_size;
}

/**
 * Get number of messages currently waiting to be printed
 * 
 * @return Number of pending messages
 */
uint32_t log_manager_get_pending_count(void) {
    if (!g_log_manager_initialized || !g_shared_memory) {
        return 0;
    }
    
    return count_pending_messages();
}

/**
 * Get total number of messages logged since startup
 * 
 * @return Total message count
 */
uint32_t log_manager_get_total_count(void) {
    return g_total_messages_logged;
}
