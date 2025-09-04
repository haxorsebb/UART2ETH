/**
 * @file pio_uart_channel1_driver.h
 * @brief PIO UART driver header for Channel 1 (GPIO 4,5)
 * 
 * Dedicated PIO UART implementation for Channel 1 testing.
 * Uses GPIO 4 (TX) and GPIO 5 (RX) with PIO0 state machines 2,3.
 * 
 * Based on the working Channel 2 implementation but configured for Channel 1.
 */

#ifndef PIO_UART_CHANNEL1_DRIVER_H
#define PIO_UART_CHANNEL1_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "uart/uart_interface.h"
#include "uart/uart_receive_buffer.h"

// PIO Configuration for Channel 1
#define PIO_UART_CH1_PIO_INSTANCE    pio0
#define PIO_UART_CH1_TX_SM          2      // State machine 2 for TX
#define PIO_UART_CH1_RX_SM          3      // State machine 3 for RX
#define PIO_UART_CH1_DEFAULT_BAUD   115200 // Default baud rate

// GPIO Pin Configuration for Channel 1
#define PIO_UART_CH1_TX_GPIO        4      // Channel 1 TX
#define PIO_UART_CH1_RX_GPIO        5      // Channel 1 RX

// Buffer configuration
#define PIO_UART_CH1_RX_BUFFER_SIZE 1536   // Receive ring buffer size
#define PIO_UART_CH1_DMA_BUFFER_SIZE 1024  // DMA staging buffer size

/**
 * @brief PIO UART Channel 1 context structure
 * 
 * Contains all state and configuration for Channel 1 PIO UART instance
 */
typedef struct {
    // PIO hardware references
    PIO pio_instance;
    uint tx_sm;
    uint rx_sm;
    uint tx_offset;
    uint rx_offset;
    
    // GPIO configuration
    uint tx_gpio;
    uint rx_gpio;
    
    // DMA channels
    uint tx_dma_chan;
    uint rx_dma_chan;
    bool dma_channels_claimed;
    
    // DMA buffers
    uint8_t tx_dma_buffer[PIO_UART_CH1_DMA_BUFFER_SIZE];
    uint8_t rx_dma_buffer[PIO_UART_CH1_DMA_BUFFER_SIZE];
    
    // Transfer state
    volatile bool tx_in_progress;
    volatile size_t rx_bytes_ready;
    
    // Receive buffer management
    uart_receive_buffer_t rx_ring;
    uint8_t rx_buffer[PIO_UART_CH1_RX_BUFFER_SIZE];
    
    // State tracking
    uart_state_t state;
    uart_config_t current_config;
    
    // Statistics
    uint32_t tx_dma_completions;
    uint32_t rx_dma_completions;
    uint32_t pio_errors;
    uint32_t dma_errors;
    
} pio_uart_context_t;

// External interface - matches uart_interface.h pattern
extern const uart_interface_t pio_uart_ch1_interface;

// Context management functions
void* pio_ch1_create_context(uint8_t pio_num, uint8_t sm_num);
void pio_ch1_destroy_context(void* context);

#endif // PIO_UART_CHANNEL1_DRIVER_H
