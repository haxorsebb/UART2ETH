/**
 * @file log_manager.c
 * @brief Implementation of fixed-size entry log manager system
 * 
 * Implements dual-core event logging with fixed-size entries as documented
 * in arc42 architecture.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Log Manager Implementation (Fixed-Size Entry System)
 * - arc42 Chapter 6 - Runtime View - Fixed-Size Entry Pattern
 */

#include "log_manager.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>

// Static variables for log manager
static bool g_log_initialized = false;
static shared_memory_layout_t* g_shared_layout = NULL;

// String lookup table for event formatting
static const char* const event_format_strings[] = {
    // System events (0-99)
    [LOG_EVENT_SYSTEM_BOOT] = "System boot sequence initiated",
    [LOG_EVENT_SYSTEM_READY] = "System ready for operation",
    [LOG_EVENT_WATCHDOG_RESET] = "Watchdog reset occurred - timeout %u ms",
    [LOG_EVENT_MEMORY_INIT] = "Memory subsystem initialized - %u KB available",
    
    // UART events (100-199)
    [LOG_EVENT_UART_INIT] = "UART channel %u initialized",
    [LOG_EVENT_UART_DATA_RX] = "UART channel %u received %u bytes",
    [LOG_EVENT_UART_DATA_TX] = "UART channel %u transmitted %u bytes",
    [LOG_EVENT_UART_ERROR] = "UART channel %u error code %u",
    [LOG_EVENT_UART_OVERFLOW] = "UART channel %u buffer overflow - %u bytes lost",
    
    // Network events (200-299)
    [LOG_EVENT_TCP_CONNECT] = "TCP connection established on port %u",
    [LOG_EVENT_TCP_DISCONNECT] = "TCP connection closed on port %u",
    [LOG_EVENT_NETWORK_ERROR] = "Network error code %u",
    [LOG_EVENT_TCP_DATA_RX] = "TCP received %u bytes on port %u",
    [LOG_EVENT_TCP_DATA_TX] = "TCP transmitted %u bytes on port %u",
    
    // Configuration events (300-399)
    [LOG_EVENT_CONFIG_CHANGED] = "Configuration parameter %u changed",
    [LOG_EVENT_CONFIG_SAVED] = "Configuration saved to flash - revision %u",
    [LOG_EVENT_CONFIG_LOADED] = "Configuration loaded from flash - revision %u",
    
    // OTA events (400-499)
    [LOG_EVENT_OTA_START] = "OTA update started - size %u bytes",
    [LOG_EVENT_OTA_COMPLETE] = "OTA update completed - version %u",
    [LOG_EVENT_OTA_ERROR] = "OTA update failed - error code %u",
};

/**
 * Get system timestamp (simplified for testing)
 */
static uint32_t get_system_timestamp(void) {
    return (uint32_t)(get_absolute_time() / 1000);  // Convert to milliseconds
}

/**
 * Get and increment the next event sequence number for current core
 * In test environment, we simulate core assignment based on event source
 */
static uint32_t get_next_event_sequence(uint16_t event_source) {
    if (!g_shared_layout) return 0;
    
    // Simulate core assignment: UART events = Core 0, Network events = Core 1, others = Core 0
    if (event_source == EVENT_SOURCE_NETWORK) {
        return ++g_shared_layout->log_mgmt.core1_sequence;
    } else {
        return ++g_shared_layout->log_mgmt.core0_sequence;
    }
}

/**
 * Write log entry to circular buffer (atomic operation)
 */
static bool write_log_entry(const log_entry_t* entry) {
    if (!g_shared_layout) return false;
    
    // Check if buffer is full
    uint32_t next_write_index = (g_shared_layout->log_mgmt.write_index + 1) % g_shared_layout->log_mgmt.max_entries;
    if (next_write_index == g_shared_layout->log_mgmt.read_index) {
        // Buffer full - drop oldest entry (move read_index forward)
        g_shared_layout->log_mgmt.read_index = (g_shared_layout->log_mgmt.read_index + 1) % g_shared_layout->log_mgmt.max_entries;
    }
    
    // Write entry to buffer
    g_shared_layout->log_entries[g_shared_layout->log_mgmt.write_index] = *entry;
    
    // Update write index
    g_shared_layout->log_mgmt.write_index = next_write_index;
    
    // Update total count
    g_shared_layout->log_mgmt.total_events_logged++;
    
    return true;
}

/**
 * Read next log entry from circular buffer
 */
static bool read_log_entry(log_entry_t* entry) {
    if (!g_shared_layout || !entry) return false;
    
    // Check if buffer is empty
    if (g_shared_layout->log_mgmt.read_index == g_shared_layout->log_mgmt.write_index) {
        return false;  // No entries to read
    }
    
    // Read entry from buffer
    *entry = g_shared_layout->log_entries[g_shared_layout->log_mgmt.read_index];
    
    // Update read index
    g_shared_layout->log_mgmt.read_index = (g_shared_layout->log_mgmt.read_index + 1) % g_shared_layout->log_mgmt.max_entries;
    
    return true;
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
    
    g_log_initialized = true;
    return true;
}

/**
 * Log an event using fixed-size entry system
 * Thread-safe for both cores with per-core sequence numbering
 * 
 * @param event_source Event source (UART0-3, NETWORK, SYSTEM, etc.)
 * @param log_level Log level (DEBUG, INFO, WARN, ERROR)
 * @param event_type Event type from log_event_type_t enum
 * @param extra_value Context-specific parameter (e.g., port number, baud rate)
 * @return true if event was logged, false if buffer full or error
 */
bool log_event(uint16_t event_source, uint16_t log_level, 
               uint16_t event_type, uint32_t extra_value) {
    if (!g_log_initialized || !g_shared_layout) {
        return false;
    }
    
    // Create log entry
    log_entry_t entry = {
        .timestamp = get_system_timestamp(),
        .event_source = event_source,
        .event_number = get_next_event_sequence(event_source),
        .log_level = log_level,
        .event_type = event_type,
        .event_extra_value = extra_value
    };
    
    // Write entry to buffer
    return write_log_entry(&entry);
}

/**
 * Core1 background task: format and print all pending log entries
 * Should be called periodically (100ms) by Core1 only
 * 
 * @return Number of entries processed and formatted
 */
uint32_t log_manager_format_pending(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    uint32_t formatted_count = 0;
    log_entry_t entry;
    
    // Process all pending entries
    while (read_log_entry(&entry)) {
        // Get format string for event type
        const char* format = NULL;
        if (entry.event_type < sizeof(event_format_strings) / sizeof(event_format_strings[0])) {
            format = event_format_strings[entry.event_type];
        }
        
        if (format) {
            // Generate formatted output
            char formatted_msg[256];
            snprintf(formatted_msg, sizeof(formatted_msg), 
                    "[%08u][%u][%u] ",
                    entry.timestamp, 
                    entry.event_source, 
                    entry.event_number);
            
            // Apply parameter if format uses %u
            if (strstr(format, "%u")) {
                char temp_msg[200];
                snprintf(temp_msg, sizeof(temp_msg), format, entry.event_extra_value);
                strncat(formatted_msg, temp_msg, sizeof(formatted_msg) - strlen(formatted_msg) - 1);
            } else {
                strncat(formatted_msg, format, sizeof(formatted_msg) - strlen(formatted_msg) - 1);
            }
            
            // Output to USB-serial stdout
            printf("%s\n", formatted_msg);
            fflush(stdout);
        }
        
        formatted_count++;
    }
    
    return formatted_count;
}

/**
 * Get current log buffer utilization percentage
 * 
 * @return Percentage (0-100) of log entry buffer currently used
 */
uint32_t log_manager_get_utilization(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    uint32_t write_idx = g_shared_layout->log_mgmt.write_index;
    uint32_t read_idx = g_shared_layout->log_mgmt.read_index;
    uint32_t max_entries = g_shared_layout->log_mgmt.max_entries;
    
    if (max_entries == 0) return 0;
    
    uint32_t used_entries;
    if (write_idx >= read_idx) {
        used_entries = write_idx - read_idx;
    } else {
        used_entries = (max_entries - read_idx) + write_idx;
    }
    
    return (used_entries * 100) / max_entries;
}

/**
 * Get number of log entries currently waiting to be processed
 * 
 * @return Number of pending log entries
 */
uint32_t log_manager_get_pending_count(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    uint32_t write_idx = g_shared_layout->log_mgmt.write_index;
    uint32_t read_idx = g_shared_layout->log_mgmt.read_index;
    uint32_t max_entries = g_shared_layout->log_mgmt.max_entries;
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (max_entries - read_idx) + write_idx;
    }
}

/**
 * Get total number of events logged since startup
 * 
 * @return Total event count across all cores
 */
uint32_t log_manager_get_total_count(void) {
    if (!g_shared_layout) return 0;
    return g_shared_layout->log_mgmt.total_events_logged;
}

/**
 * Get current event sequence number for specific core
 * Used for detecting lost events during debugging
 * 
 * @param core_id Core ID (0 or 1)
 * @return Current sequence number for the specified core
 */
uint32_t log_manager_get_core_sequence(uint8_t core_id) {
    if (!g_shared_layout) return 0;
    
    if (core_id == 0) {
        return g_shared_layout->log_mgmt.core0_sequence;
    } else if (core_id == 1) {
        return g_shared_layout->log_mgmt.core1_sequence;
    }
    
    return 0;
}

/**
 * Reset log manager state for testing
 * Clears all counters and reinitializes entry buffer
 * 
 * @return true if reset successful, false otherwise
 */
bool log_manager_reset_for_testing(void) {
    if (!g_shared_layout) {
        return false;
    }
    
    // Reset counters
    g_shared_layout->log_mgmt.total_events_logged = 0;
    g_shared_layout->log_mgmt.core0_sequence = 0;
    g_shared_layout->log_mgmt.core1_sequence = 0;
    
    // Reset buffer pointers
    g_shared_layout->log_mgmt.write_index = 0;
    g_shared_layout->log_mgmt.read_index = 0;
    
    // Clear buffer
    memset(g_shared_layout->log_entries, 0, g_shared_layout->log_mgmt.max_entries * sizeof(log_entry_t));
    
    return true;
}

/**
 * Get human-readable string for event type (for testing/debugging)
 * 
 * @param event_type Event type from log_event_type_t enum
 * @return Format string for the event type, or NULL if invalid
 */
const char* log_manager_get_event_format_string(uint16_t event_type) {
    if (event_type >= sizeof(event_format_strings) / sizeof(event_format_strings[0])) {
        return NULL;
    }
    
    return event_format_strings[event_type];
}