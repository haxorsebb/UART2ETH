/**
 * @file pio_uart_channel4_driver.h
 * @brief PIO UART driver header for Channel 4 implementation (PIO2)
 * 
 * Provides software UART functionality using RP2350 PIO2 state machines
 * with DMA acceleration and robust error handling.
 * Copy of Channel 2 driver adapted for PIO2.
 */

#ifndef PIO_UART_CHANNEL4_DRIVER_H
#define PIO_UART_CHANNEL4_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "uart/uart_interface.h"
#include "uart/uart_receive_buffer.h"

// PIO Configuration for Channel 4 - Uses PIO2
#define PIO_UART_CH4_PIO_INSTANCE    pio2
#define PIO_UART_CH4_TX_SM           0      // State machine 0 for TX
#define PIO_UART_CH4_RX_SM           1      // State machine 1 for RX
#define PIO_UART_CH4_DEFAULT_BAUD    230400 // Default baud rate

// GPIO Pin Configuration for Channel 4
#define PIO_UART_CH4_TX_GPIO         24     // Channel 4 TX
#define PIO_UART_CH4_RX_GPIO         25     // Channel 4 RX

// Buffer configuration
#define PIO_UART_CH4_RX_BUFFER_SIZE  1536   // Receive ring buffer size
#define PIO_UART_CH4_DMA_BUFFER_SIZE 1024   // DMA staging buffer size

// Constants for resource management
#define PIO_UART_CH4_PROGRAM_OFFSET_INVALID 0
#define PIO_UART_CH4_INVALID_DMA_CHANNEL ((uint)-1)

/**
 * @brief PIO UART Channel 4 context structure
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
    uint8_t tx_dma_buffer[PIO_UART_CH4_DMA_BUFFER_SIZE];
    uint8_t rx_dma_buffer[PIO_UART_CH4_DMA_BUFFER_SIZE];
    
    // Transfer state
    volatile bool tx_in_progress;
    volatile size_t rx_bytes_ready;
    
    // Receive buffer management
    uart_receive_buffer_t rx_ring;
    uint8_t rx_buffer[PIO_UART_CH4_RX_BUFFER_SIZE];
    
    // State tracking
    uart_state_t state;
    uart_config_t current_config;
    
    // Statistics
    uint32_t tx_dma_completions;
    uint32_t rx_dma_completions;
    uint32_t pio_errors;
    uint32_t dma_errors;
    
} pio_uart_ch4_context_t;

// External interface - matches uart_interface.h pattern
extern const uart_interface_t pio_uart_ch4_interface;

// Context management functions
void* pio_ch4_create_context(uint8_t pio_num, uint8_t sm_num);
void pio_ch4_destroy_context(void* context);

#endif // PIO_UART_CHANNEL4_DRIVER_H
