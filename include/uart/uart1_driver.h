/**
 * @file uart1_driver.h
 * @brief UART1 Driver for RP2350 (Issue #76)
 * 
 * Provides low-level hardware abstraction for UART1 peripheral
 * on RP2350. Handles register access, interrupt management,
 * and character transmission/reception.
 * 
 * Hardware Configuration:
 * - UART1 peripheral
 * - RX: GPIO9, TX: GPIO10
 * - 230400 baud, 8N1 configuration
 * - Interrupt-driven operation
 * 
 * Documentation Reference:
 * - Issue #76: Add UART Hardware Manager implementation
 * - arc42 Chapter 5 - UART Hardware Manager Implementation
 * - RP2350 Datasheet - UART peripheral
 */

#ifndef UART1_DRIVER_H
#define UART1_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/regs/intctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

// UART1 Hardware configuration  
#define UART1_INSTANCE      uart1
#define UART1_DEFAULT_RX_GPIO    9
#define UART1_DEFAULT_TX_GPIO    8
#define UART1_DEFAULT_BAUD       230400

// Buffer sizes
#define UART1_RX_BUFFER_SIZE     256
#define UART1_TX_BUFFER_SIZE     256

/**
 * @brief UART1 driver configuration structure
 */
typedef struct {
    uint32_t baud_rate;        // Baud rate (230400 for Issue #76)
    uint8_t data_bits;         // Data bits (5-8, default 8)
    uint8_t stop_bits;         // Stop bits (1-2, default 1)
    uart_parity_t parity;      // Parity (NONE/EVEN/ODD, default NONE) - uses pico-sdk type
    uint16_t rx_gpio;          // RX GPIO pin (default 9)
    uint16_t tx_gpio;          // TX GPIO pin (default 8)
    bool enable_loopback;      // Enable hardware loopback for testing
} uart1_config_t;

/**
 * @brief UART1 driver state structure
 */
typedef struct {
    bool initialized;          // Driver initialization status
    bool ready;               // Driver ready for operation
    uart1_config_t config;    // Current configuration
    uint32_t chars_sent;      // Statistics: characters transmitted
    uint32_t chars_received;  // Statistics: characters received
    uint32_t tx_errors;       // Statistics: transmission errors
    uint32_t rx_errors;       // Statistics: reception errors
    uint32_t overrun_errors;  // Statistics: RX buffer overrun errors
} uart1_state_t;

/**
 * @brief Driver initialization and control functions
 */

/**
 * Initialize UART1 driver with configuration
 * @param config UART1 configuration parameters
 * @return true if initialization successful, false otherwise
 */
bool uart1_driver_init(const uart1_config_t* config);

/**
 * Deinitialize UART1 driver
 */
void uart1_driver_deinit(void);

/**
 * Check if UART1 driver is ready for operation
 * @return true if ready, false otherwise
 */
bool uart1_driver_is_ready(void);

/**
 * Get driver state for diagnostics
 * @return pointer to driver state structure
 */
const uart1_state_t* uart1_driver_get_state(void);

/**
 * @brief Character transmission functions
 */

/**
 * Send single character via UART1
 * @param c Character to send
 * @return true if character queued for transmission, false if TX buffer full
 */
bool uart1_driver_send_char(char c);

/**
 * Send data buffer via UART1
 * @param data Data buffer to send
 * @param length Number of bytes to send
 * @return true if data queued for transmission, false if TX buffer full
 */
bool uart1_driver_send_data(const uint8_t* data, size_t length);

/**
 * Check if UART1 is ready to accept more TX data
 * @return true if TX ready, false if TX buffer full
 */
bool uart1_driver_is_tx_ready(void);

/**
 * Check if UART1 transmission is complete
 * @return true if all data transmitted, false if transmission in progress
 */
bool uart1_driver_is_tx_complete(void);

/**
 * @brief Character reception functions
 */

/**
 * Check if UART1 has received data available
 * @return true if RX data available, false otherwise
 */
bool uart1_driver_has_rx_data(void);

/**
 * Read single character from UART1
 * @return received character, or 0 if no data available
 */
char uart1_driver_read_char(void);

/**
 * Read data buffer from UART1
 * @param buffer Output buffer for received data
 * @param max_length Maximum bytes to read
 * @return Number of bytes actually read
 */
size_t uart1_driver_read_data(uint8_t* buffer, size_t max_length);

/**
 * Get number of characters available in RX buffer
 * @return Number of characters available for reading
 */
size_t uart1_driver_get_rx_count(void);

/**
 * @brief Interrupt control functions
 */

/**
 * Enable UART1 RX interrupt
 */
void uart1_driver_enable_rx_interrupt(void);

/**
 * Disable UART1 RX interrupt
 */
void uart1_driver_disable_rx_interrupt(void);

/**
 * Enable UART1 TX interrupt
 */
void uart1_driver_enable_tx_interrupt(void);

/**
 * Disable UART1 TX interrupt
 */
void uart1_driver_disable_tx_interrupt(void);

/**
 * Check if RX interrupt is enabled
 * @return true if RX interrupt enabled, false otherwise
 */
bool uart1_driver_is_rx_interrupt_enabled(void);

/**
 * Check if TX interrupt is enabled
 * @return true if TX interrupt enabled, false otherwise
 */
bool uart1_driver_is_tx_interrupt_enabled(void);

/**
 * @brief Utility and diagnostic functions
 */

/**
 * Flush UART1 TX buffer (wait for transmission to complete)
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return true if flush completed, false if timeout
 */
bool uart1_driver_flush_tx(uint32_t timeout_ms);

/**
 * Clear UART1 RX buffer
 */
void uart1_driver_clear_rx_buffer(void);

/**
 * Perform UART1 loopback test
 * @param test_char Character to test with
 * @return true if loopback successful, false otherwise
 */
bool uart1_driver_test_loopback(char test_char);

/**
 * Get default UART1 configuration
 * @param config Output configuration structure with defaults
 */
void uart1_driver_get_default_config(uart1_config_t* config);

/**
 * Reset UART1 statistics counters
 */
void uart1_driver_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif // UART1_DRIVER_H