/**
 * @file pio_uart_channel3_driver.h
 * @brief PIO UART driver header for Channel 3 (GPIO 16,17)
 * 
 * Dedicated PIO UART implementation for Channel 3 testing.
 * Uses GPIO 16 (TX) and GPIO 17 (RX) with PIO0 state machines 2,3.
 * 
 * Based on the working Channel 2 implementation but configured for Channel 3.
 * 
 * Documentation Reference:
 * - Expansion for Channel 3 support
 * - arc42 Chapter 5 - PIO UART Channel 3 Implementation
 */

#ifndef PIO_UART_CHANNEL3_DRIVER_H
#define PIO_UART_CHANNEL3_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "uart/uart_interface.h"
#include "uart/uart_receive_buffer.h"

// PIO Configuration for Channel 3 - Use PIO0 with state machines 2,3
#define PIO_UART_CH3_PIO_INSTANCE    pio0
#define PIO_UART_CH3_TX_SM          2      // State machine 2 for TX
#define PIO_UART_CH3_RX_SM          3      // State machine 3 for RX
#define PIO_UART_CH3_DEFAULT_BAUD   115200 // Default baud rate

// GPIO Pin Configuration for Channel 3
#define PIO_UART_CH3_TX_GPIO        16     // Channel 3 TX
#define PIO_UART_CH3_RX_GPIO        17     // Channel 3 RX

// Buffer configuration
#define PIO_UART_CH3_RX_BUFFER_SIZE 1536   // Receive ring buffer size
#define PIO_UART_CH3_DMA_BUFFER_SIZE 1024  // DMA staging buffer size

/**
 * @brief PIO UART Channel 3 context structure
 * 
 * Contains all state and configuration for Channel 3 PIO UART instance
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
    uint8_t tx_dma_buffer[PIO_UART_CH3_DMA_BUFFER_SIZE];
    uint8_t rx_dma_buffer[PIO_UART_CH3_DMA_BUFFER_SIZE];
    
    // Transfer state
    volatile bool tx_in_progress;
    volatile size_t rx_bytes_ready;
    
    // Receive buffer management
    uart_receive_buffer_t rx_ring;
    uint8_t rx_buffer[PIO_UART_CH3_RX_BUFFER_SIZE];
    
    // State tracking
    uart_state_t state;
    uart_config_t current_config;
    
    // Statistics
    uint32_t tx_dma_completions;
    uint32_t rx_dma_completions;
    uint32_t pio_errors;
    uint32_t dma_errors;
    
} pio_uart_ch3_context_t;

// External interface - matches uart_interface.h pattern
extern const uart_interface_t pio_uart_ch3_interface;

// Context management functions
void* pio_ch3_create_context(uint8_t pio_num, uint8_t sm_num);
void pio_ch3_destroy_context(void* context);

#endif // PIO_UART_CHANNEL3_DRIVER_H
