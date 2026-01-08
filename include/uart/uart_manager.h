/**
 * @file uart_manager.h
 * @brief UART Manager for UART2ETH - manages 4 UART channels
 */

#ifndef UART_MANAGER_H
#define UART_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "shared_memory.h"  // For channel_id_t

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
struct uart_instance;

#define UART_MANAGER_MAX_CHANNELS 5  // Channels 0-4 (debug + 4 data channels)

typedef enum {
    UART_MANAGER_STATUS_UNINITIALIZED,
    UART_MANAGER_STATUS_INITIALIZING,
    UART_MANAGER_STATUS_READY,
    UART_MANAGER_STATUS_ACTIVE,
    UART_MANAGER_STATUS_ERROR
} uart_manager_status_t;

typedef struct {
    uart_manager_status_t status;
    uint32_t uptime_seconds;
    uint32_t messages_processed;
    uint32_t messages_tcp_to_uart;
    uint32_t messages_uart_to_tcp;
    uint32_t transmission_errors;
    uint32_t reception_errors;
    uint32_t ring_buffer_overflows;
    uint32_t bytes_transmitted;
    uint32_t bytes_received;
} uart_manager_stats_t;

/**
 * Initialize UART Manager - sets up all 4 UART channels
 * @return true if initialization successful
 */
bool uart_manager_init(void);

/**
 * Deinitialize UART Manager
 */
void uart_manager_deinit(void);

/**
 * Check if UART Manager is ready
 */
bool uart_manager_is_ready(void);

/**
 * Get current status
 */
uart_manager_status_t uart_manager_get_status(void);

/**
 * Check if any UART channel has pending work
 */
bool uart_manager_has_incoming_work(void);

/**
 * Process incoming UART data (UART→TCP direction)
 * Reads from all UART channels and creates ring buffer entries
 */
bool uart_manager_process_incoming_data(void);

/**
 * Process outgoing UART data (TCP→UART direction)
 * Retrieves messages from ring buffer and sends via appropriate UART
 */
bool uart_manager_process_outgoing_data(void);

/**
 * Get manager statistics
 */
void uart_manager_get_stats(uart_manager_stats_t* stats);

/**
 * Reset statistics
 */
void uart_manager_reset_stats(void);

/**
 * Enable/disable debug output
 */
void uart_manager_set_debug(bool enable);

/**
 * Get diagnostic information
 */
int uart_manager_get_diagnostic_info(char* info_buffer, size_t buffer_size);

/**
 * Convert status to string
 */
const char* uart_manager_status_to_string(uart_manager_status_t status);

/**
 * Get channel instance for testing/debugging
 * @param channel Channel ID (0-3)
 * @return Pointer to uart_instance_t or NULL if invalid channel
 */
struct uart_instance* uart_manager_get_channel_instance(channel_id_t channel);

#ifdef __cplusplus
}
#endif

#endif // UART_MANAGER_H