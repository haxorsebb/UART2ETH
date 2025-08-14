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

// Import minimum log level from debug configuration
#ifndef LOG_MINIMUM_LEVEL
#define LOG_MINIMUM_LEVEL LOG_LEVEL_INFO  // Default to INFO if not set
#endif

// Constants for better maintainability
#define LOG_MAX_MESSAGE_LENGTH 256
#define LOG_MAX_FORMAT_LENGTH 200
#define EVENT_FORMAT_ARRAY_SIZE (sizeof(event_format_strings) / sizeof(event_format_strings[0]))

// Event type ranges for better code organization and validation
#define SYSTEM_EVENT_BASE    0
#define SYSTEM_EVENT_MAX     99
#define UART_EVENT_BASE      100
#define UART_EVENT_MAX       199
#define NETWORK_EVENT_BASE   200
#define NETWORK_EVENT_MAX    299
#define CONFIG_EVENT_BASE    300
#define CONFIG_EVENT_MAX     399
#define OTA_EVENT_BASE       400
#define OTA_EVENT_MAX        499

// Valid event source ranges
#define EVENT_SOURCE_MIN     EVENT_SOURCE_SYSTEM
#define EVENT_SOURCE_MAX     EVENT_SOURCE_WATCHDOG

// Global error state
static log_error_t g_last_error = LOG_ERROR_NONE;

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
    [LOG_EVENT_CORE1_STARTING] = "UART2ETH Core1 starting",
    [LOG_EVENT_CORE1_LAUNCHED] = "Core1 launched successfully", 
    [LOG_EVENT_CORE0_STARTING] = "Starting Core0 UART processing",
    [LOG_EVENT_SHARED_MEMORY_INIT] = "Shared memory initialized successfully",
    [LOG_EVENT_STATE_MACHINE_INIT] = "Event-driven state machine initialized successfully",
    [LOG_EVENT_LOG_MANAGER_INIT] = "Log manager initialized successfully",
    [LOG_EVENT_SYSTEM_ERROR] = "FATAL ERROR: System initialization failed - code %u",
    [LOG_EVENT_CORE_EXIT_ERROR] = "ERROR: Core%u main loop exited unexpectedly",
    [LOG_EVENT_INIT_PHASE] = "MAIN_STATE_INIT - Initializing system phase %u",
    [LOG_EVENT_CONFIG_PHASE] = "MAIN_STATE_CONFIGURATION - Loading configuration",
    [LOG_EVENT_OPERATIONAL_PHASE] = "MAIN_STATE_OPERATIONAL - Normal operation mode",
    [LOG_EVENT_ERROR_RECOVERY] = "System recovery attempt %u",
    
    // UART events (100-199)
    [LOG_EVENT_UART_INIT] = "UART initialized",
    [LOG_EVENT_UART_HW_INIT] = "UART hardware initialized successfully",
    [LOG_EVENT_UART_DATA_AVAIL] = "UART data available - starting processing",
    [LOG_EVENT_UART_PROCESSING] = "Processing UART data - cycles remaining: %u",
    [LOG_EVENT_UART_COMPLETE] = "UART data processing complete",
    [LOG_EVENT_UART_RECOVERY] = "UART recovery attempt %u",
    [LOG_EVENT_UART_CHANNELS] = "Initializing %u UART channels",
    
    // UART Channel-specific events (single parameter - byte count/error code)
    [LOG_EVENT_UART0_DATA_RX] = "UART channel 0 received %u bytes",
    [LOG_EVENT_UART0_DATA_TX] = "UART channel 0 transmitted %u bytes", 
    [LOG_EVENT_UART0_ERROR] = "UART channel 0 error code %u",
    [LOG_EVENT_UART0_OVERFLOW] = "UART channel 0 buffer overflow - %u bytes lost",
    [LOG_EVENT_UART1_DATA_RX] = "UART channel 1 received %u bytes",
    [LOG_EVENT_UART1_DATA_TX] = "UART channel 1 transmitted %u bytes",
    [LOG_EVENT_UART1_ERROR] = "UART channel 1 error code %u", 
    [LOG_EVENT_UART1_OVERFLOW] = "UART channel 1 buffer overflow - %u bytes lost",
    [LOG_EVENT_UART2_DATA_RX] = "UART channel 2 received %u bytes",
    [LOG_EVENT_UART2_DATA_TX] = "UART channel 2 transmitted %u bytes",
    [LOG_EVENT_UART2_ERROR] = "UART channel 2 error code %u",
    [LOG_EVENT_UART2_OVERFLOW] = "UART channel 2 buffer overflow - %u bytes lost", 
    [LOG_EVENT_UART3_DATA_RX] = "UART channel 3 received %u bytes",
    [LOG_EVENT_UART3_DATA_TX] = "UART channel 3 transmitted %u bytes",
    [LOG_EVENT_UART3_ERROR] = "UART channel 3 error code %u",
    [LOG_EVENT_UART3_OVERFLOW] = "UART channel 3 buffer overflow - %u bytes lost",
    
    // Generic UART events
    [LOG_EVENT_UART_INIT_GENERIC] = "UART channel %u initialized",
    
    // Network events (200-299)
    [LOG_EVENT_TCP_CONNECT] = "TCP connection established on port %u",
    [LOG_EVENT_TCP_DISCONNECT] = "TCP connection closed on port %u",
    [LOG_EVENT_NETWORK_ERROR] = "Network error code %u",
    [LOG_EVENT_TCP_DATA_RX_BYTES] = "TCP received %u bytes",
    [LOG_EVENT_TCP_DATA_RX_PORT] = "TCP data received on port %u",
    [LOG_EVENT_TCP_DATA_TX_BYTES] = "TCP transmitted %u bytes", 
    [LOG_EVENT_TCP_DATA_TX_PORT] = "TCP data transmitted on port %u",
    [LOG_EVENT_NETWORK_INIT] = "Network interface initialized successfully",
    [LOG_EVENT_NETWORK_UP] = "Network interface is now available",
    [LOG_EVENT_NETWORK_DOWN] = "Network interface went down",
    [LOG_EVENT_NETWORK_AVAILABLE] = "Network interface available",
    [LOG_EVENT_NETWORK_STATUS] = "Network status check - available: %u",
    [LOG_EVENT_NETWORK_OPERATIONS] = "Processing network operations (TCP/IP, sockets)",
    [LOG_EVENT_CONNECTION_CHECK] = "Connection check - active connections: %u",
    
    // Configuration events (300-399)
    [LOG_EVENT_CONFIG_CHANGED] = "Configuration parameter %u changed",
    [LOG_EVENT_CONFIG_SAVED] = "Configuration saved to flash - revision %u",
    [LOG_EVENT_CONFIG_LOADED] = "Configuration loaded from flash - revision %u",
    [LOG_EVENT_FACTORY_RESET] = "Factory reset completed - all flash pages invalidated",
    [LOG_EVENT_FLASH_INIT] = "Flash persistence initialized: partition ID=%u",
    [LOG_EVENT_FLASH_PARTITION_ID] = "Found partition ID %u",
    [LOG_EVENT_FLASH_PARTITION_OFFSET] = "Partition offset 0x%x",
    [LOG_EVENT_FLASH_READ] = "Reading page %u from flash",
    [LOG_EVENT_FLASH_WRITE] = "Writing flash page %u",
    [LOG_EVENT_FLASH_PAGE_SCAN] = "Scanning ring buffer for valid config pages",
    [LOG_EVENT_FLASH_PAGE_NUMBER] = "Page %u is valid",
    [LOG_EVENT_FLASH_PAGE_REVISION] = "Page revision %u",
    [LOG_EVENT_FLASH_PAGE_INVALID] = "Page %u: invalid - reason code %u", 
    [LOG_EVENT_FLASH_BEST_PAGE_NUMBER] = "Best page is %u",
    [LOG_EVENT_FLASH_BEST_PAGE_REV] = "Best page revision %u",
    [LOG_EVENT_FLASH_ERASE] = "Invalidating flash page %u",
    [LOG_EVENT_PERSISTENCE_START] = "Starting persistence operation",
    [LOG_EVENT_PERSISTENCE_END] = "Persistence operation completed",
    [LOG_EVENT_PERSISTENCE_NEEDED] = "Persistence needed - configuration changed",
    [LOG_EVENT_SHARED_MEMORY_REINIT] = "Shared memory re-initialized with factory defaults",
    
    // OTA events (400-499)
    [LOG_EVENT_OTA_START] = "OTA update started - size %u bytes",
    [LOG_EVENT_OTA_COMPLETE] = "OTA update completed - version %u",
    [LOG_EVENT_OTA_ERROR] = "OTA update failed - error code %u",

    // PERSISTENCE events (500-599)
    [LOG_EVENT_PERSISTENCE_INIT_SUCCESS] = "Persistence management system initialized.",
    [LOG_EVENT_PERSISTENCE_INIT_FAIL] = "Persistence management system init FAILED!",

    // LOGGING events (600-699)
    [LOG_EVENT_LOGGING_INIT_SUCCESS] = "Log management system initialized.",
    [LOG_EVENT_LOGGING_INIT_FAIL] = "Log management system init FAILED!",
};

/**
 * Validate that shared memory structure is properly initialized
 */
static bool validate_shared_memory(void) {
    if (!g_shared_layout) {
        g_last_error = LOG_ERROR_NOT_INITIALIZED;
        return false;
    }
    
    // Basic sanity checks on shared memory structure
    if (g_shared_layout->log_mgmt.max_entries == 0 || 
        g_shared_layout->log_mgmt.max_entries > 10000) {  // Reasonable upper bound
        g_last_error = LOG_ERROR_SHARED_MEMORY_CORRUPT;
        return false;
    }
    
    if (g_shared_layout->log_mgmt.write_index >= g_shared_layout->log_mgmt.max_entries ||
        g_shared_layout->log_mgmt.read_index >= g_shared_layout->log_mgmt.max_entries) {
        g_last_error = LOG_ERROR_SHARED_MEMORY_CORRUPT;
        return false;
    }
    
    return true;
}

/**
 * Validate event type is within allowed ranges
 */
static bool validate_event_type(event_type_t event_type) {
    // Check if event type is within any valid range
    if ((event_type >= SYSTEM_EVENT_BASE && event_type <= SYSTEM_EVENT_MAX) ||
        (event_type >= UART_EVENT_BASE && event_type <= UART_EVENT_MAX) ||
        (event_type >= NETWORK_EVENT_BASE && event_type <= NETWORK_EVENT_MAX) ||
        (event_type >= CONFIG_EVENT_BASE && event_type <= CONFIG_EVENT_MAX) ||
        (event_type >= OTA_EVENT_BASE && event_type <= OTA_EVENT_MAX)||
        (event_type >= PERSISTENCE_EVENT_BASE && event_type <= PERSISTENCE_EVENT_MAX)||
        (event_type >= LOGGING_EVENT_BASE && event_type <= LOGGING_EVENT_MAX)) {
        
        // Additional check: ensure we have a format string for this type
        if (event_type < EVENT_FORMAT_ARRAY_SIZE && event_format_strings[event_type] != NULL) {
            return true;
        }
    }
    
    g_last_error = LOG_ERROR_INVALID_EVENT_TYPE;
    return false;
}

/**
 * Validate event source is within allowed ranges
 */
static bool validate_event_source(event_source_t event_source) {
    if (event_source >= EVENT_SOURCE_MIN && event_source <= EVENT_SOURCE_MAX) {
        return true;
    }
    
    g_last_error = LOG_ERROR_INVALID_EVENT_SOURCE;
    return false;
}

/**
 * Get system timestamp (simplified for testing)
 */
static uint32_t get_system_timestamp(void) {
    return (uint32_t)(get_absolute_time() / 1000);  // Convert to milliseconds
}

/**
 * Get and increment the next event sequence number for current core
 * Uses actual core ID in production, simulates based on event source in test environment
 */
static uint32_t get_next_event_sequence(event_source_t event_source) {
    if (!g_shared_layout) return 0;
    
    // Thread-safe access to sequence counters
    uint32_t sequence;
    
    // For testing compatibility: Simulate core assignment based on event source
    // In production, this logic would use get_core_num() or actual hardware assignment
    // Network events = Core 1, all others = Core 0 (maintains test compatibility)
    if (get_core_num()==1) {
        sequence = ++g_shared_layout->log_mgmt.core1_sequence;
    } else {
        sequence = ++g_shared_layout->log_mgmt.core0_sequence;
    }
    
    return sequence;
}

/**
 * Write log entry to circular buffer (atomic operation with spinlock)
 * Uses fast conditional check instead of expensive modulo operation
 */
static bool write_log_entry(const log_entry_t* entry) {
    if (!g_shared_layout || !entry) {
        return false;
    }
    
    // Critical section: protect shared buffer state
    uint32_t save = spin_lock_blocking(g_shared_layout->log_mgmt.entry_lock);
    
    // Calculate next write index without expensive modulo
    uint32_t next_write_index = g_shared_layout->log_mgmt.write_index + 1;
    if (next_write_index >= g_shared_layout->log_mgmt.max_entries) {
        next_write_index = 0;
    }
    
    // Check if buffer is full
    if (next_write_index == g_shared_layout->log_mgmt.read_index) {
        // Buffer full - drop oldest entry (move read_index forward)
        g_shared_layout->log_mgmt.read_index += 1;
        if (g_shared_layout->log_mgmt.read_index >= g_shared_layout->log_mgmt.max_entries) {
            g_shared_layout->log_mgmt.read_index = 0;
        }
    }

    // Update write index with fast ring buffer advance
    g_shared_layout->log_mgmt.write_index = next_write_index;
    // Update total count
    g_shared_layout->log_mgmt.total_events_logged++;
    spin_unlock(g_shared_layout->log_mgmt.entry_lock, save);
    
    /*

    uint32_t current_write, next_write;
    do {
        current_write = atomic_load(&g_shared_layout->log_mgmt.write_index);
        next_write = (current_write + 1) % g_shared_layout->log_mgmt.max_entries;
        
        // Check if buffer would be full
        if (next_write == atomic_load(&g_shared_layout->log_mgmt.read_index)) {
            return false;  // Buffer full
        }
    } while (!atomic_compare_exchange_weak(&g_shared_layout->log_mgmt.write_index, 
                                          &current_write, next_write));

    */

    // Write entry to buffer
    g_shared_layout->log_entries[next_write_index] = *entry;
    
    
    
    return true;
}

/**
 * Read next log entry from circular buffer (eventually consistent)
 * Uses fast conditional check instead of expensive modulo operation
 */
static bool read_log_entry(log_entry_t* entry) {
    if (!g_shared_layout || !entry) {
        return false;
    }
    
    
    // Check if buffer is empty
    if (g_shared_layout->log_mgmt.read_index == g_shared_layout->log_mgmt.write_index) {
        return false;  // No entries to read
    }
    
    // Read entry from buffer
    *entry = g_shared_layout->log_entries[g_shared_layout->log_mgmt.read_index];
    
    // Update read index with fast ring buffer advance
    g_shared_layout->log_mgmt.read_index += 1;
    if (g_shared_layout->log_mgmt.read_index >= g_shared_layout->log_mgmt.max_entries) {
        g_shared_layout->log_mgmt.read_index = 0;
    }
    
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
bool log_event(event_source_t event_source, log_level_t log_level, 
               event_type_t event_type, uint32_t extra_value) {
    // Reset error state
    g_last_error = LOG_ERROR_NONE;
    
    return true;


    if (!g_log_initialized) {
        g_last_error = LOG_ERROR_NOT_INITIALIZED;
        return false;
    }
    
    // Check if this log level meets the minimum threshold
    if (log_level < LOG_MINIMUM_LEVEL) {
        // Silently drop logs below minimum level (not an error)
        return true;
    }
    
    // Validate shared memory structure
    if (!validate_shared_memory()) {
        return false;  // Error already set by validate_shared_memory
    }
    
    // Validate input parameters
    if (!validate_event_source(event_source)) {
        return false;  // Error already set by validate_event_source
    }
    
    if (!validate_event_type(event_type)) {
        return false;  // Error already set by validate_event_type
    }
    
    // Validate log level (0-3)
    if (log_level > LOG_LEVEL_ERROR) {
        g_last_error = LOG_ERROR_INVALID_EVENT_TYPE;  // Reuse for simplicity
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
/**
 * Safely format a single log entry with proper validation and sanitization
 */
static bool format_single_log_entry(const log_entry_t* entry, char* output_buffer, size_t buffer_size) {
    if (!entry || !output_buffer || buffer_size == 0) {
        return false;
    }
    
    // Validate event type bounds
    if (entry->event_type >= EVENT_FORMAT_ARRAY_SIZE) {
        snprintf(output_buffer, buffer_size, "[%08u][%u][%u] UNKNOWN_EVENT_TYPE_%u",
                entry->timestamp, entry->event_source, entry->event_number, entry->event_type);
        return true;
    }
    
    const char* format = event_format_strings[entry->event_type];
    if (!format) {
        snprintf(output_buffer, buffer_size, "[%08u][%u][%u] NULL_FORMAT_STRING",
                entry->timestamp, entry->event_source, entry->event_number);
        return true;
    }
    
    // Create the prefix first
    char prefix[64];
    int prefix_len = snprintf(prefix, sizeof(prefix), "[%08u][%u][%u] ",
            entry->timestamp, entry->event_source, entry->event_number);
    
    if (prefix_len < 0 || prefix_len >= sizeof(prefix)) {
        // Prefix creation failed
        return false;
    }
    
    // For security, we'll create a safe version of format strings that only allows %u
    // and validates the parameter count
    char safe_message[LOG_MAX_FORMAT_LENGTH];
    
    // Check if format string contains exactly one %u and no other format specifiers
    const char* first_percent = strchr(format, '%');
    if (first_percent && first_percent[1] == 'u' && strchr(first_percent + 2, '%') == NULL) {
        // Safe: exactly one %u parameter
        snprintf(safe_message, sizeof(safe_message), format, entry->event_extra_value);
    } else if (first_percent == NULL) {
        // Safe: no parameters needed
        strncpy(safe_message, format, sizeof(safe_message) - 1);
        safe_message[sizeof(safe_message) - 1] = '\0';
    } else {
        // Unsafe: multiple format specifiers or unknown format - sanitize
        strncpy(safe_message, format, sizeof(safe_message) - 1);
        safe_message[sizeof(safe_message) - 1] = '\0';
        
        // Replace any % characters with # to prevent format string attacks
        for (char* p = safe_message; *p; p++) {
            if (*p == '%') {
                *p = '#';
            }
        }
    }
    
    // Combine prefix and safe message
    snprintf(output_buffer, buffer_size, "%s%s", prefix, safe_message);
    return true;
}

uint32_t log_manager_format_pending(void) {
    // Reset error state
    g_last_error = LOG_ERROR_NONE;
    
    if (!g_log_initialized) {
        g_last_error = LOG_ERROR_NOT_INITIALIZED;
        return 0;
    }
    
    if (!validate_shared_memory()) {
        return 0;  // Error already set by validate_shared_memory
    }
    
    uint32_t formatted_count = 0;
    log_entry_t entry;
    
    // Process one pending entries
    if (read_log_entry(&entry)) {
        char formatted_msg[LOG_MAX_MESSAGE_LENGTH];
        
        if (format_single_log_entry(&entry, formatted_msg, sizeof(formatted_msg))) {
            // Output to USB-serial stdout
            printf("%s\n", formatted_msg);
            fflush(stdout);
        } else {
            // Fallback output for formatting errors
            printf("[%08u][%u][%u] FORMAT_ERROR\n", 
                   entry.timestamp, entry.event_source, entry.event_number);
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
    
    // Read buffer state eventually consistent
    uint32_t write_idx = g_shared_layout->log_mgmt.write_index;
    uint32_t read_idx = g_shared_layout->log_mgmt.read_index;
    uint32_t max_entries = g_shared_layout->log_mgmt.max_entries;
    
    if (max_entries == 0) return 0;
    
    // Fast ring buffer utilization calculation without modulo
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
    
    // Read buffer state atomically  
    uint32_t write_idx = g_shared_layout->log_mgmt.write_index;
    uint32_t read_idx = g_shared_layout->log_mgmt.read_index;
    uint32_t max_entries = g_shared_layout->log_mgmt.max_entries;
    
    // Fast ring buffer count calculation without modulo
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
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    // Read total count without block, eventually consistent
    uint32_t total = g_shared_layout->log_mgmt.total_events_logged;
    
    return total;
}

/**
 * Get current event sequence number for specific core
 * Used for detecting lost events during debugging
 * 
 * @param core_id Core ID (0 or 1)
 * @return Current sequence number for the specified core
 */
uint32_t log_manager_get_core_sequence(uint8_t core_id) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    uint32_t sequence = 0;
    
    if (core_id == 0) {
        sequence = g_shared_layout->log_mgmt.core0_sequence;
    } else if (core_id == 1) {
        sequence = g_shared_layout->log_mgmt.core1_sequence;
    }
    return sequence;
}

/**
 * Reset log manager state for testing
 * Clears all counters and reinitializes entry buffer
 * 
 * @return true if reset successful, false otherwise
 */
bool log_manager_reset_for_testing(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return false;
    }
    
    // Reset all state atomically
    uint32_t save = spin_lock_blocking(g_shared_layout->log_mgmt.entry_lock);
    
    // Reset counters
    g_shared_layout->log_mgmt.total_events_logged = 0;
    g_shared_layout->log_mgmt.core0_sequence = 0;
    g_shared_layout->log_mgmt.core1_sequence = 0;
    
    // Reset buffer pointers
    g_shared_layout->log_mgmt.write_index = 0;
    g_shared_layout->log_mgmt.read_index = 0;
    
    // Clear buffer
    memset(g_shared_layout->log_entries, 0, g_shared_layout->log_mgmt.max_entries * sizeof(log_entry_t));
    
    spin_unlock(g_shared_layout->log_mgmt.entry_lock, save);
    return true;
}

/**
 * Get human-readable string for event type (for testing/debugging)
 * 
 * @param event_type Event type from log_event_type_t enum
 * @return Format string for the event type, or NULL if invalid
 */
const char* log_manager_get_event_format_string(event_type_t event_type) {
    // Reset error state
    g_last_error = LOG_ERROR_NONE;
    
    if (!validate_event_type(event_type)) {
        return NULL;  // Error already set by validate_event_type
    }
    
    return event_format_strings[event_type];
}

/**
 * Get the last error that occurred in the log manager
 * 
 * @return Last error code
 */
log_error_t log_manager_get_last_error(void) {
    return g_last_error;
}

/**
 * Get human-readable description of error code
 * 
 * @param error Error code
 * @return Error description string
 */
const char* log_manager_get_error_string(log_error_t error) {
    switch (error) {
        case LOG_ERROR_NONE:
            return "No error";
        case LOG_ERROR_NOT_INITIALIZED:
            return "Log manager not initialized";
        case LOG_ERROR_INVALID_EVENT_TYPE:
            return "Invalid event type";
        case LOG_ERROR_INVALID_EVENT_SOURCE:
            return "Invalid event source";
        case LOG_ERROR_BUFFER_FULL:
            return "Log buffer full";
        case LOG_ERROR_SHARED_MEMORY_CORRUPT:
            return "Shared memory corrupted";
        case LOG_ERROR_SPINLOCK_TIMEOUT:
            return "Spinlock timeout";
        default:
            return "Unknown error";
    }
}