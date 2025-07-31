/**
 * @file log_manager.h
 * @brief Log manager interface for dual-core logging with lock-reserve-release-write pattern
 * 
 * Implements the logging synchronization pattern documented in arc42:
 * - Lock-Reserve-Release-Write for thread safety
 * - Core1 background printer for USB-serial output
 * - Fixed-length message format: [TTTTTTTT][C] Message\r\n
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Log Manager Whitebox
 * - arc42 Chapter 6 - Runtime View - Log Synchronization Pattern
 */

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// Maximum log message length (excluding timestamp and core ID prefix)
#define LOG_MESSAGE_MAX_LENGTH  200
#define LOG_TIMESTAMP_LENGTH    8   // [12345678]
#define LOG_CORE_ID_LENGTH      3   // [0] or [1]
#define LOG_TERMINATOR_LENGTH   2   // \r\n

// Total formatted message length
#define LOG_FORMATTED_MAX_LENGTH (LOG_TIMESTAMP_LENGTH + LOG_CORE_ID_LENGTH + LOG_MESSAGE_MAX_LENGTH + LOG_TERMINATOR_LENGTH + 1)

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

// Function declarations

/**
 * Initialize log manager (must be called after shared_memory_init)
 * 
 * @return true if initialization successful, false otherwise
 */
bool log_manager_init(void);

/**
 * Log a message using lock-reserve-release-write pattern
 * Thread-safe for both cores
 * 
 * @param core_id Core ID (0 or 1)
 * @param level Log level
 * @param message Message text (max LOG_MESSAGE_MAX_LENGTH chars)
 * @return true if message was queued, false if buffer full or error
 */
bool log_message(uint8_t core_id, log_level_t level, const char* message);

/**
 * Core1 background task: print all pending messages to USB-serial
 * Should be called periodically (100ms) by Core1 only
 * 
 * @return Number of messages printed
 */
uint32_t log_manager_print_pending(void);

/**
 * Get current log buffer utilization percentage
 * 
 * @return Percentage (0-100) of log buffer currently used
 */
uint32_t log_manager_get_utilization(void);

/**
 * Get number of messages currently waiting to be printed
 * 
 * @return Number of pending messages
 */
uint32_t log_manager_get_pending_count(void);

/**
 * Get total number of messages logged since startup
 * 
 * @return Total message count
 */
uint32_t log_manager_get_total_count(void);

#endif // LOG_MANAGER_H
