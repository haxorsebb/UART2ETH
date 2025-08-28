/**
 * @file pio_uart_driver.h
 * @brief PIO UART driver header for Channel 2 implementation
 * 
 * Provides software UART functionality using RP2350 PIO0 state machines
 * with DMA acceleration and robust error handling.
 * 
 * Documentation Reference:
 * - Issue #82: PIO UART Driver Implementation
 * - ADR-013: PIO UART Implementation for Channel 2
 * - arc42 Chapter 5 - PIO UART Driver Implementation
 */

#ifndef PIO_UART_DRIVER_H
#define PIO_UART_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "uart/uart_interface.h"
#include "uart/uart_receive_buffer.h"

// PIO Configuration (as per ADR-013)
#define PIO_UART_PIO_INSTANCE    pio0
#define PIO_UART_TX_SM          0      // State machine 0 for TX
#define PIO_UART_RX_SM          1      // State machine 1 for RX
#define PIO_UART_DEFAULT_BAUD   230400 // Default test baud rate

// GPIO Pin Configuration (as per ADR-008)
#define PIO_UART_TX_GPIO        14     // Channel 2 TX
#define PIO_UART_RX_GPIO        15     // Channel 2 RX

// Buffer configuration
#define PIO_UART_RX_BUFFER_SIZE 512    // Receive ring buffer size
#define PIO_UART_DMA_BUFFER_SIZE 256   // DMA staging buffer size

/**
 * @brief PIO UART context structure
 * 
 * Contains all state and configuration for a PIO UART instance
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
    uint8_t tx_dma_buffer[PIO_UART_DMA_BUFFER_SIZE];
    uint8_t rx_dma_buffer[PIO_UART_DMA_BUFFER_SIZE];
    
    // Transfer state
    volatile bool tx_in_progress;
    volatile size_t rx_bytes_ready;
    
    // Receive buffer management
    uart_receive_buffer_t rx_ring;
    uint8_t rx_buffer[PIO_UART_RX_BUFFER_SIZE];
    
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
extern const uart_interface_t pio_uart_interface;

// Context management functions
void* pio_create_context(uint8_t pio_num, uint8_t sm_num);
void pio_destroy_context(void* context);

// Internal function declarations (for testing)
bool pio_uart_setup_programs(pio_uart_context_t* ctx);
void pio_uart_cleanup_programs(pio_uart_context_t* ctx);
bool pio_uart_setup_dma(pio_uart_context_t* ctx);
void pio_uart_cleanup_dma(pio_uart_context_t* ctx);
void pio_uart_tx_dma_handler(pio_uart_context_t* ctx);
void pio_uart_rx_dma_handler(pio_uart_context_t* ctx);
void pio_uart_rx_pio_handler(pio_uart_context_t* ctx);

#endif // PIO_UART_DRIVER_H