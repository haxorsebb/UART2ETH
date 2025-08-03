/**
 * @file log_manager.h
 * @brief Log manager interface for fixed-size event logging with enumerated types
 * 
 * Implements fixed-size log entries for predictable memory usage and elimination
 * of variable-length message parsing complexity. Uses enumerated event types
 * with string lookup tables for efficient storage and human-readable output.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Log Manager Whitebox (Fixed-Size Entry System)
 * - arc42 Chapter 6 - Runtime View - Fixed-Size Entry Pattern
 */

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// Constants for buffer sizing and event ranges
#define LOG_MAX_MESSAGE_LENGTH 256
#define LOG_MAX_FORMAT_LENGTH 200

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

// Error codes for detailed error reporting
typedef enum {
    LOG_ERROR_NONE = 0,
    LOG_ERROR_NOT_INITIALIZED,
    LOG_ERROR_INVALID_EVENT_TYPE,
    LOG_ERROR_INVALID_EVENT_SOURCE,
    LOG_ERROR_BUFFER_FULL,
    LOG_ERROR_SHARED_MEMORY_CORRUPT,
    LOG_ERROR_SPINLOCK_TIMEOUT
} log_error_t;

// Log levels (maintained for compatibility)
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

// Event source definitions
typedef enum {
    EVENT_SOURCE_SYSTEM = 0,
    EVENT_SOURCE_UART0 = 1,
    EVENT_SOURCE_UART1 = 2,
    EVENT_SOURCE_UART2 = 3,
    EVENT_SOURCE_UART3 = 4,
    EVENT_SOURCE_NETWORK = 5,
    EVENT_SOURCE_CONFIG = 6,
    EVENT_SOURCE_OTA = 7,
    EVENT_SOURCE_WATCHDOG = 8,
} event_source_t;

// Event type definitions with explicit numbering for version compatibility
typedef enum {
    // System events (0-99)
    LOG_EVENT_SYSTEM_BOOT = 0,
    LOG_EVENT_SYSTEM_READY = 1,
    LOG_EVENT_WATCHDOG_RESET = 2,
    LOG_EVENT_MEMORY_INIT = 3,
    
    // UART events (100-199)
    LOG_EVENT_UART_INIT = 100,
    LOG_EVENT_UART_DATA_RX = 101,
    LOG_EVENT_UART_DATA_TX = 102,
    LOG_EVENT_UART_ERROR = 103,
    LOG_EVENT_UART_OVERFLOW = 104,
    
    // Network events (200-299)  
    LOG_EVENT_TCP_CONNECT = 200,
    LOG_EVENT_TCP_DISCONNECT = 201,
    LOG_EVENT_NETWORK_ERROR = 202,
    LOG_EVENT_TCP_DATA_RX = 203,
    LOG_EVENT_TCP_DATA_TX = 204,
    
    // Configuration events (300-399)
    LOG_EVENT_CONFIG_CHANGED = 300,
    LOG_EVENT_CONFIG_SAVED = 301,
    LOG_EVENT_CONFIG_LOADED = 302,
    
    // OTA events (400-499)
    LOG_EVENT_OTA_START = 400,
    LOG_EVENT_OTA_COMPLETE = 401,
    LOG_EVENT_OTA_ERROR = 402,
} log_event_type_t;

// Fixed-size log entry structure (16 bytes total, 32-bit aligned)
typedef struct {
    uint32_t timestamp;             // System timestamp in milliseconds
    uint16_t event_source;          // Event source (UART0-3, NETWORK, etc.)
    uint16_t event_number;          // Per-core sequence counter
    uint16_t log_level;             // DEBUG, INFO, WARN, ERROR
    uint16_t event_type;            // Explicitly numbered event enum
    uint32_t event_extra_value;     // Context-specific parameter
} __attribute__((packed, aligned(4))) log_entry_t;

// Function declarations

/**
 * Initialize log manager (must be called after shared_memory_init)
 * 
 * @return true if initialization successful, false otherwise
 */
bool log_manager_init(void);

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
               uint16_t event_type, uint32_t extra_value);

/**
 * Core1 background task: format and print all pending log entries
 * Should be called periodically (100ms) by Core1 only
 * 
 * @return Number of entries processed and formatted
 */
uint32_t log_manager_format_pending(void);

/**
 * Get current log buffer utilization percentage
 * 
 * @return Percentage (0-100) of log entry buffer currently used
 */
uint32_t log_manager_get_utilization(void);

/**
 * Get number of log entries currently waiting to be processed
 * 
 * @return Number of pending log entries
 */
uint32_t log_manager_get_pending_count(void);

/**
 * Get total number of events logged since startup
 * 
 * @return Total event count across all cores
 */
uint32_t log_manager_get_total_count(void);

/**
 * Get current event sequence number for specific core
 * Used for detecting lost events during debugging
 * 
 * @param core_id Core ID (0 or 1)
 * @return Current sequence number for the specified core
 */
uint32_t log_manager_get_core_sequence(uint8_t core_id);

/**
 * Reset log manager state for testing
 * Clears all counters and reinitializes entry buffer
 * 
 * @return true if reset successful, false otherwise
 */
bool log_manager_reset_for_testing(void);

/**
 * Get human-readable string for event type (for testing/debugging)
 * 
 * @param event_type Event type from log_event_type_t enum
 * @return Format string for the event type, or NULL if invalid
 */
const char* log_manager_get_event_format_string(uint16_t event_type);

/**
 * Get the last error that occurred in the log manager
 * 
 * @return Last error code
 */
log_error_t log_manager_get_last_error(void);

/**
 * Get human-readable description of error code
 * 
 * @param error Error code
 * @return Error description string
 */
const char* log_manager_get_error_string(log_error_t error);

#endif // LOG_MANAGER_H