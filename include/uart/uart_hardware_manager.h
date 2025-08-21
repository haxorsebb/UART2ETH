/**
 * @file uart_hardware_manager.h
 * @brief UART Hardware Manager for UART2ETH Core0 UART Subsystem
 * 
 * Provides high-level UART management functions that coordinate
 * between UART1 driver and ring buffer system.
 * Handles initialization, message processing, and Core0 integration.
 * 
 * This is the main interface used by Core0 main loop for UART
 * operations and forms the implementation for Issue #76.
 * 
 * Documentation Reference:  
 * - Issue #76: Add UART Hardware Manager implementation
 * - arc42 Chapter 5 - UART Hardware Manager Implementation
 * - ADR-007: Event-Driven State Machine Architecture
 */

#ifndef UART_HARDWARE_MANAGER_H
#define UART_HARDWARE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UART Hardware Manager status enumeration
 */
typedef enum {
    UART_MANAGER_STATUS_UNINITIALIZED,  // Not initialized
    UART_MANAGER_STATUS_INITIALIZING,   // Initialization in progress
    UART_MANAGER_STATUS_READY,          // Ready for operation
    UART_MANAGER_STATUS_ACTIVE,         // Processing data
    UART_MANAGER_STATUS_ERROR           // Error state
} uart_manager_status_t;

/**
 * @brief UART Hardware Manager statistics structure
 */
typedef struct {
    uart_manager_status_t status;           // Current manager status
    uint32_t uptime_seconds;                // Manager uptime in seconds
    uint32_t messages_processed;            // Total messages processed
    uint32_t messages_tcp_to_uart;          // TCP→UART messages
    uint32_t messages_uart_to_tcp;          // UART→TCP messages
    uint32_t transmission_errors;           // UART transmission errors
    uint32_t reception_errors;              // UART reception errors
    uint32_t ring_buffer_overflows;         // Ring buffer overflow events
    uint32_t bytes_transmitted;             // Total bytes transmitted
    uint32_t bytes_received;                // Total bytes received
} __attribute__((aligned(4))) uart_manager_stats_t;

/**
 * @brief UART Hardware Manager result codes for better error handling
 */
typedef enum {
    UART_MANAGER_SUCCESS = 0,
    UART_MANAGER_ERROR_ALREADY_INITIALIZED,
    UART_MANAGER_ERROR_DRIVER_INIT_FAILED,
    UART_MANAGER_ERROR_BUFFER_OVERFLOW,
    UART_MANAGER_ERROR_INVALID_CONFIG,
    UART_MANAGER_ERROR_NOT_INITIALIZED
} uart_manager_result_t;

/**
 * @brief UART Hardware Manager configuration structure
 */
typedef struct {
    uint32_t baud_rate;          // UART baud rate (default: 230400)
    uint8_t data_bits;           // Data bits (default: 8)
    uint8_t stop_bits;           // Stop bits (default: 1)  
    uint8_t parity;              // Parity setting (default: none)
    uint8_t rx_gpio;             // RX GPIO pin (default: 9)
    uint8_t tx_gpio;             // TX GPIO pin (default: 10)
    bool enable_loopback;        // Enable loopback for testing (default: false)
    bool enable_debug_output;    // Enable debug logging (default: false)
} uart_hardware_manager_config_t;

// Default configuration constants
#define UART_HARDWARE_MANAGER_DEFAULT_BAUD    230400
#define UART_HARDWARE_MANAGER_DEFAULT_RX_GPIO 9
#define UART_HARDWARE_MANAGER_DEFAULT_TX_GPIO 8

/**
 * @brief UART Hardware Manager initialization and control
 */

/**
 * Initialize UART Hardware Manager
 * @return true if initialization successful, false otherwise
 */
bool uart_hardware_manager_init(void);

/**
 * Deinitialize UART Hardware Manager
 */
void uart_hardware_manager_deinit(void);

/**
 * Check if UART Hardware Manager is ready for operation
 * @return true if ready, false otherwise
 */
bool uart_hardware_manager_is_ready(void);

/**
 * Get current UART Hardware Manager status
 * @return Current status enumeration
 */
uart_manager_status_t uart_hardware_manager_get_status(void);

/**
 * @brief Data processing functions for Core0 integration
 */

/**
 * Check if UART Hardware Manager has pending work
 * Used by Core0 work detection mechanism
 * @return true if work pending, false otherwise
 */
bool uart_hardware_manager_has_pending_work(void);

/**
 * Process incoming UART data (UART→TCP direction)
 * Assembles received characters into complete lines and creates ring buffer entries
 * @return true if data processed, false if no data or error
 */
bool uart_hardware_manager_process_incoming_data(void);

/**
 * Process outgoing UART data (TCP→UART direction) 
 * Retrieves messages from ring buffer and transmits via UART
 * @return true if data processed, false if no data or error
 */
bool uart_hardware_manager_process_outgoing_data(void);

/**
 * @brief Status and statistics functions
 */

/**
 * Get current UART Hardware Manager statistics
 * @param stats Output structure for statistics
 */
void uart_hardware_manager_get_stats(uart_manager_stats_t* stats);

/**
 * Reset UART Hardware Manager statistics counters
 */
void uart_hardware_manager_reset_stats(void);

/**
 * Enable or disable debug output for UART Hardware Manager
 * @param enable True to enable debug output, false to disable
 */
void uart_hardware_manager_set_debug(bool enable);

/**
 * @brief Diagnostic and testing functions
 */

/**
 * Test UART Hardware Manager connectivity (loopback test)
 * @return true if loopback test passes, false otherwise
 */
bool uart_hardware_manager_test_loopback(void);

/**
 * Get detailed diagnostic information for testing
 * @param info_buffer Output buffer for diagnostic information
 * @param buffer_size Size of output buffer
 * @return Number of bytes written to buffer
 */
int uart_hardware_manager_get_diagnostic_info(char* info_buffer, size_t buffer_size);

/**
 * Convert UART manager status to human-readable string
 * @param status UART manager status enumeration
 * @return String representation of status
 */
const char* uart_hardware_manager_status_to_string(uart_manager_status_t status);

#ifdef __cplusplus
}
#endif

#endif // UART_HARDWARE_MANAGER_H