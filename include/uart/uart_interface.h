/**
 * @file uart_interface.h
 * @brief Common UART interface for multiple UART types (PL011 hardware, PIO)
 */

#ifndef UART_INTERFACE_H
#define UART_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/uart.h"
#include "shared_memory.h"

typedef enum {
    UART_TYPE_PL011,    // Hardware UART (uart0, uart1)
    UART_TYPE_PIO       // PIO-based UART
} uart_type_t;

typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uart_parity_t parity;
    uint8_t tx_gpio;
    uint8_t rx_gpio;
    bool enable_flow_control;
} uart_config_t;

typedef struct {
    bool initialized;
    bool ready;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t overrun_errors;
    uint32_t uptime_ms;
} uart_state_t;

/**
 * @brief Common UART interface - all UART implementations must provide these
 */
typedef struct uart_interface {
    // Lifecycle
    bool (*init)(void* context, const uart_config_t* config);
    void (*deinit)(void* context);
    bool (*is_ready)(void* context);
    
    // Transmission
    void (*send_byte)(void* context, uint8_t byte);
    size_t (*send_data)(void* context, const uint8_t* data, size_t len);
    bool (*is_tx_ready)(void* context);
    bool (*is_tx_complete)(void* context);
    
    // Reception
    bool (*has_rx_data)(void* context);
    uint8_t (*read_byte)(void* context);
    size_t (*read_data)(void* context, uint8_t* buffer, size_t max_len);
    size_t (*get_rx_count)(void* context);
    
    // Control
    void (*clear_rx_buffer)(void* context);
    
    // Status/Diagnostics
    const uart_state_t* (*get_state)(void* context);
    void (*reset_stats)(void* context);
    
} uart_interface_t;

/**
 * @brief UART instance combining interface with implementation context
 */
typedef struct uart_instance {
    channel_id_t channel_id;              // 0-3
    uart_type_t type;                // UART_TYPE_PL011 or UART_TYPE_PIO
    const uart_interface_t* ops;     // Function table
    void* driver_context;            // Driver-specific data
    uart_config_t config;            // Current configuration
    uart_state_t common_state;       // Common state tracking
} uart_instance_t;

// External interfaces provided by concrete drivers
extern const uart_interface_t pl011_uart_interface;
extern const uart_interface_t pio_uart_interface;

// Context creation functions for each driver type
void* pio_create_context(uint8_t pio_num, uint8_t sm_num);  // PIO0/1, SM 0-3
void pio_destroy_context(void* context);

#endif // UART_INTERFACE_H