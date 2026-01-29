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

// Import minimum log level from debug configuration
#ifndef LOG_MINIMUM_LEVEL
#define LOG_MINIMUM_LEVEL LOG_LEVEL_INFO  // Default to INFO if not set
#endif

// Constants for better maintainability
#define EVENT_FORMAT_ARRAY_SIZE (sizeof(event_format_strings) / sizeof(event_format_strings[0]))

// Constants for buffer sizing and event ranges
#define LOG_MAX_MESSAGE_LENGTH 256
#define LOG_MAX_FORMAT_LENGTH 200
#define LOG_MAX_FORMATTED_ENTRIES_PER_CALL 2

// Event type ranges for better code organization and validation
#define SYSTEM_EVENT_BASE       0
#define SYSTEM_EVENT_MAX        99
#define UART_EVENT_BASE         100
#define UART_EVENT_MAX          199
#define NETWORK_EVENT_BASE      200
#define NETWORK_EVENT_MAX       299
#define CONFIG_EVENT_BASE       300
#define CONFIG_EVENT_MAX        399
#define OTA_EVENT_BASE          400
#define OTA_EVENT_MAX           499
#define PERSISTENCE_EVENT_BASE  500
#define PERSISTENCE_EVENT_MAX   599
#define LOGGING_EVENT_BASE      600
#define LOGGING_EVENT_MAX       699
#define STATE_CHANGE_EVENT_BASE 700
#define STATE_CHANGE_EVENT_MAX  799

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
#define LOG_LEVEL_TRACE ((log_level_t)0)
#define LOG_LEVEL_DEBUG ((log_level_t)1)
#define LOG_LEVEL_INFO  ((log_level_t)2)
#define LOG_LEVEL_WARN  ((log_level_t)3)
#define LOG_LEVEL_ERROR ((log_level_t)4)

// Event source definitions with uint16_t base for alignment
typedef uint16_t event_source_t;
#define EVENT_SOURCE_SYSTEM     ((event_source_t)0)
#define EVENT_SOURCE_UART0      ((event_source_t)1)
#define EVENT_SOURCE_UART1      ((event_source_t)2)
#define EVENT_SOURCE_UART2      ((event_source_t)3)
#define EVENT_SOURCE_UART3      ((event_source_t)4)
#define EVENT_SOURCE_NETWORK    ((event_source_t)5)
#define EVENT_SOURCE_CONFIG     ((event_source_t)6)
#define EVENT_SOURCE_OTA        ((event_source_t)7)
#define EVENT_SOURCE_WATCHDOG   ((event_source_t)8)
#define EVENT_SOURCE_PERSISTENCE ((event_source_t)9)
#define EVENT_SOURCE_LOGGING    ((event_source_t)10)
#define EVENT_SOURCE_UI         ((event_source_t)11)
#define EVENT_SOURCE_ANE        ((event_source_t)12)
#define EVENT_SOURCE_IMOUTO     ((event_source_t)13)
#define EVENT_SOURCE_MAIN_STATE      ((event_source_t)14)
#define EVENT_SOURCE_CORE0_SUBSTATE  ((event_source_t)15)
#define EVENT_SOURCE_CORE1_SUBSTATE  ((event_source_t)16)
#define EVENT_SOURCE_RINGBUFFER      ((event_source_t)17)

// Valid event source ranges
#define EVENT_SOURCE_MIN     EVENT_SOURCE_SYSTEM
#define EVENT_SOURCE_MAX     EVENT_SOURCE_RINGBUFFER


// Event type definitions with enum for automatic numbering and type safety
typedef enum {
    // System events (0-99)
    LOG_EVENT_SYSTEM_BOOT = 0,
    LOG_EVENT_SYSTEM_CLOCK,
    LOG_EVENT_SYSTEM_READY,
    LOG_EVENT_WATCHDOG_RESET,
    LOG_EVENT_MEMORY_INIT,
    LOG_EVENT_CORE1_STARTING,
    LOG_EVENT_CORE1_LAUNCHED,
    LOG_EVENT_CORE0_STARTING,
    LOG_EVENT_SHARED_MEMORY_INIT,
    LOG_EVENT_STATE_MACHINE_INIT,
    LOG_EVENT_LOG_MANAGER_INIT,
    LOG_EVENT_SYSTEM_ERROR,
    LOG_EVENT_CORE_EXIT_ERROR,
    LOG_EVENT_INIT_PHASE,
    LOG_EVENT_CONFIG_PHASE,
    LOG_EVENT_OPERATIONAL_PHASE,
    LOG_EVENT_ERROR_RECOVERY,

    // UART events (100-199)
    LOG_EVENT_UART_INIT = 100,
    LOG_EVENT_UART_HW_INIT = 105,
    LOG_EVENT_UART_DATA_AVAIL,
    LOG_EVENT_UART_PROCESSING,
    LOG_EVENT_UART_COMPLETE,
    LOG_EVENT_UART_RECOVERY,
    LOG_EVENT_UART_CHANNELS,

    // UART Channel-specific events
    LOG_EVENT_UART0_DATA_RX,
    LOG_EVENT_UART0_DATA_TX,
    LOG_EVENT_UART0_ERROR,
    LOG_EVENT_UART0_OVERFLOW,
    LOG_EVENT_UART1_DATA_RX,
    LOG_EVENT_UART1_DATA_TX,
    LOG_EVENT_UART1_ERROR,
    LOG_EVENT_UART1_OVERFLOW,
    LOG_EVENT_UART2_DATA_RX,
    LOG_EVENT_UART2_DATA_TX,
    LOG_EVENT_UART2_ERROR,
    LOG_EVENT_UART2_OVERFLOW,
    LOG_EVENT_UART3_DATA_RX,
    LOG_EVENT_UART3_DATA_TX,
    LOG_EVENT_UART3_ERROR,
    LOG_EVENT_UART3_OVERFLOW,

    // Generic UART events
    LOG_EVENT_UART_INIT_GENERIC,

    // Network events (200-299)  
    LOG_EVENT_TCP_CONNECT = 200,
    LOG_EVENT_TCP_DISCONNECT,
    LOG_EVENT_NETWORK_ERROR,
    LOG_EVENT_TCP_DATA_RX_BYTES,
    LOG_EVENT_TCP_DATA_RX_PORT,
    LOG_EVENT_TCP_DATA_TX_BYTES,
    LOG_EVENT_TCP_DATA_TX_PORT,
    LOG_EVENT_NETWORK_INIT,
    LOG_EVENT_NETWORK_UP,
    LOG_EVENT_NETWORK_DOWN,
    LOG_EVENT_NETWORK_AVAILABLE,
    LOG_EVENT_NETWORK_STATUS,
    LOG_EVENT_NETWORK_IP_CONFIGURED_1,
    LOG_EVENT_NETWORK_IP_CONFIGURED_2,
    LOG_EVENT_NETWORK_IP_CONFIGURED_3,
    LOG_EVENT_NETWORK_IP_CONFIGURED_4,
    LOG_EVENT_NETWORK_OPERATIONS,
    LOG_EVENT_CONNECTION_CHECK,
    LOG_EVENT_NETWORK_DEINIT,
    LOG_EVENT_NETWORK_TX,
    LOG_EVENT_NETWORK_RX,
    LOG_EVENT_NETWORK_RESET,
    LOG_EVENT_NETWORK_CONFIG,

    // Configuration events (300-399)
    LOG_EVENT_CONFIG_CHANGED = 300,
    LOG_EVENT_CONFIG_SAVED,
    LOG_EVENT_CONFIG_LOADED,
    LOG_EVENT_FACTORY_RESET,
    LOG_EVENT_FLASH_INIT,
    LOG_EVENT_FLASH_PARTITION_ID,
    LOG_EVENT_FLASH_PARTITION_OFFSET,
    LOG_EVENT_FLASH_READ,
    LOG_EVENT_FLASH_WRITE,
    LOG_EVENT_FLASH_BLOCK_SCAN,
    LOG_EVENT_FLASH_BLOCK_NUMBER,
    LOG_EVENT_FLASH_BLOCK_REVISION,
    LOG_EVENT_FLASH_BLOCK_INVALID,
    LOG_EVENT_FLASH_BEST_BLOCK_NUMBER,
    LOG_EVENT_FLASH_BEST_BLOCK_REV,
    LOG_EVENT_FLASH_ERASE,
    LOG_EVENT_FLASH_ERASE_FAILED,
    LOG_EVENT_FLASH_READ_FAILED,
    LOG_EVENT_PERSISTENCE_START,
    LOG_EVENT_PERSISTENCE_END,
    LOG_EVENT_PERSISTENCE_NEEDED,
    LOG_EVENT_SHARED_MEMORY_REINIT,
    LOG_EVENT_FACTORY_PARTITION_NOT_FOUND,
    LOG_EVENT_FACTORY_PARTITION_TOO_SMALL,
    LOG_EVENT_FACTORY_DEFAULTS_CHECKSUM_FAILED,
    LOG_EVENT_FACTORY_DEFAULTS_LOADED,
    LOG_EVENT_FACTORY_DEFAULTS_INVALID,
    LOG_EVENT_FACTORY_DEFAULTS_APPLIED,
    LOG_EVENT_FACTORY_DEFAULTS_APPLY_FAILED,

    // OTA/Update events (400-499)
    LOG_EVENT_OTA_START = 400,
    LOG_EVENT_OTA_COMPLETE,
    LOG_EVENT_OTA_ERROR,
    
    // Firmware update events (ADR-017)
    LOG_EVENT_FIRMWARE_UPDATE_START = 410,
    LOG_EVENT_FIRMWARE_UPDATE_COMPLETE,
    LOG_EVENT_FIRMWARE_UPDATE_FAILED,
    LOG_EVENT_FIRMWARE_BUY_SUCCESS,
    LOG_EVENT_FIRMWARE_BUY_FAILED,
    LOG_EVENT_SYSTEM_REBOOT,
    
    // Ringbuffer events (410-419) per ADR-012
    LOG_EVENT_RINGBUFFER_WORK_START = 410,      // Ringbuffer processing started
    LOG_EVENT_RINGBUFFER_WORK_COMPLETE = 411,   // Ringbuffer processing completed
    
    // Core0 work events (420-429) per ADR-012
    LOG_EVENT_CORE0_WORK_CHECK = 420,           // Core0 checking for pending work
    LOG_EVENT_CORE0_IDLE_WAIT = 421,            // Core0 entering idle wait
    LOG_EVENT_CORE0_UART_WORK_START = 422,      // UART hardware work started
    LOG_EVENT_CORE0_UART_WORK_COMPLETE = 423,   // UART hardware work completed

    // PERSISTENCE events (500-599)
    LOG_EVENT_PERSISTENCE_INIT_SUCCESS = 500,
    LOG_EVENT_PERSISTENCE_INIT_FAIL,

    // LOGGING events (600-699)
    LOG_EVENT_LOGGING_INIT_SUCCESS = 600,
    LOG_EVENT_LOGGING_INIT_FAIL,

    // Main state change events (700-703)
    // IMPORTANT: These MUST match main_state_t enum order exactly and be complete
    LOG_MAIN_STATE_INIT = 700,
    LOG_MAIN_STATE_CONFIGURATION,
    LOG_MAIN_STATE_OPERATIONAL,
    LOG_MAIN_STATE_ERROR,

    // Core0 substate change events (720-732) per ADR-012
    // IMPORTANT: These MUST match core0_substate_t enum order exactly and be complete
    LOG_CORE0_INIT_UART = 720,
    LOG_CORE0_INIT_COMPLETE,
    LOG_CORE0_INIT_IDLE,
    LOG_CORE0_INIT_ERROR,
    LOG_CORE0_CONFIG_UART,
    LOG_CORE0_CONFIG_COMPLETE,
    LOG_CORE0_CONFIG_IDLE,
    LOG_CORE0_CONFIG_ERROR,
    LOG_CORE0_IDLE,
    LOG_CORE0_UART_ACTIVE,
    LOG_CORE0_RINGBUFFER_ACTIVE,
    LOG_CORE0_UART_ERROR,

    // Core1 substate change events (750-773)
    // IMPORTANT: These MUST match core1_substate_t enum order exactly and be complete
    LOG_CORE1_INIT_PERISTENCE = 750,
    LOG_CORE1_INIT_LOGGING,
    LOG_CORE1_INIT_NET,
    LOG_CORE1_INIT_WAIT_FOR_LINK,
    LOG_CORE1_INIT_COMPLETE,
    LOG_CORE1_INIT_IDLE,
    LOG_CORE1_CONFIG_NET,
    LOG_CORE1_CONFIG_COMPLETE,
    LOG_CORE1_CONFIG_NET_WAIT_FOR_DHCP,
    LOG_CORE1_CONFIG_NET_CHECK_DHCP,
    LOG_CORE1_CONFIG_IDLE,
    LOG_CORE1_CONFIG_LOG_ACTIVE,
    LOG_CORE1_CONFIG_ERROR,
    LOG_CORE1_NET_LINK_CHANGE,
    LOG_CORE1_NET_CONNECTED,
    LOG_CORE1_NET_DISCONNECTED,
    LOG_CORE1_NET_IDLE,
    LOG_CORE1_NET_ACTIVE_RECEIVE,
    LOG_CORE1_NET_ACTIVE_SEND,
    LOG_CORE1_PERSISTENCE_ACTIVE,
    LOG_CORE1_LOG_ACTIVE,
    LOG_CORE1_RINGBUFFER_ACTIVE,
    LOG_CORE1_IDLE,
    LOG_CORE1_INIT_ERROR,
    LOG_CORE1_SHUTDOWN,
    
    // New Core1 substates for update module (ADR-017)
    LOG_CORE1_BUY_UPDATE,
    LOG_CORE1_REBOOT_FLUSH,
    LOG_CORE1_REBOOT_EXECUTE,
    
    // New Core0 substates for reboot (ADR-017)
    LOG_CORE0_REBOOT_IDLE,
    
    // New Main state for reboot (ADR-017)
    LOG_MAIN_STATE_REBOOT
} event_type_t;

// Base constants for state change logging
#define MAIN_STATE_CHANGE_STRING_BASE   LOG_MAIN_STATE_INIT
#define CORE0_STATE_CHANGE_STRING_BASE  LOG_CORE0_INIT_UART
#define CORE1_STATE_CHANGE_STRING_BASE  LOG_CORE1_INIT_PERISTENCE


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