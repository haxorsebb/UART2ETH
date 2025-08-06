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

// Strong typing with uint16_t base for proper alignment and type safety
typedef uint16_t log_level_t;
#define LOG_LEVEL_DEBUG ((log_level_t)0)
#define LOG_LEVEL_INFO  ((log_level_t)1)
#define LOG_LEVEL_WARN  ((log_level_t)2)
#define LOG_LEVEL_ERROR ((log_level_t)3)

// Event source definitions with uint16_t base for alignment
typedef uint16_t event_source_t;
#define EVENT_SOURCE_SYSTEM    ((event_source_t)0)
#define EVENT_SOURCE_UART0     ((event_source_t)1)
#define EVENT_SOURCE_UART1     ((event_source_t)2)
#define EVENT_SOURCE_UART2     ((event_source_t)3)
#define EVENT_SOURCE_UART3     ((event_source_t)4)
#define EVENT_SOURCE_NETWORK   ((event_source_t)5)
#define EVENT_SOURCE_CONFIG    ((event_source_t)6)
#define EVENT_SOURCE_OTA       ((event_source_t)7)
#define EVENT_SOURCE_WATCHDOG  ((event_source_t)8)

// Event type definitions with uint16_t base and explicit numbering for version compatibility
typedef uint16_t event_type_t;

// System events (0-99)
#define LOG_EVENT_SYSTEM_BOOT     ((event_type_t)0)
#define LOG_EVENT_SYSTEM_READY    ((event_type_t)1)
#define LOG_EVENT_WATCHDOG_RESET  ((event_type_t)2)
#define LOG_EVENT_MEMORY_INIT     ((event_type_t)3)

// UART events (100-199)
#define LOG_EVENT_UART_INIT       ((event_type_t)100)
#define LOG_EVENT_UART_DATA_RX    ((event_type_t)101)
#define LOG_EVENT_UART_DATA_TX    ((event_type_t)102)
#define LOG_EVENT_UART_ERROR      ((event_type_t)103)
#define LOG_EVENT_UART_OVERFLOW   ((event_type_t)104)

// Network events (200-299)  
#define LOG_EVENT_TCP_CONNECT     ((event_type_t)200)
#define LOG_EVENT_TCP_DISCONNECT  ((event_type_t)201)
#define LOG_EVENT_NETWORK_ERROR   ((event_type_t)202)
#define LOG_EVENT_TCP_DATA_RX     ((event_type_t)203)
#define LOG_EVENT_TCP_DATA_TX     ((event_type_t)204)

// Configuration events (300-399)
#define LOG_EVENT_CONFIG_CHANGED  ((event_type_t)300)
#define LOG_EVENT_CONFIG_SAVED    ((event_type_t)301)
#define LOG_EVENT_CONFIG_LOADED   ((event_type_t)302)
#define LOG_EVENT_FACTORY_RESET   ((event_type_t)303)

// OTA events (400-499)
#define LOG_EVENT_OTA_START       ((event_type_t)400)
#define LOG_EVENT_OTA_COMPLETE    ((event_type_t)401)
#define LOG_EVENT_OTA_ERROR       ((event_type_t)402)

// Fixed-size log entry structure (16 bytes total, 32-bit aligned)
typedef struct {
    uint32_t timestamp;             // System timestamp in milliseconds
    event_source_t event_source;    // Event source (UART0-3, NETWORK, etc.) - uint16_t
    uint16_t event_number;          // Per-core sequence counter
    log_level_t log_level;          // DEBUG, INFO, WARN, ERROR - uint16_t
    event_type_t event_type;        // Explicitly numbered event enum - uint16_t
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
 * @param event_type Event type from event_type_t
 * @param extra_value Context-specific parameter (e.g., port number, baud rate)
 * @return true if event was logged, false if buffer full or error
 */
bool log_event(event_source_t event_source, log_level_t log_level, 
               event_type_t event_type, uint32_t extra_value);

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
 * @param event_type Event type from event_type_t
 * @return Format string for the event type, or NULL if invalid
 */
const char* log_manager_get_event_format_string(event_type_t event_type);

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