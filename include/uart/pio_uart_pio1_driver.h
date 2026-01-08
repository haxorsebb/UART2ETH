/**
 * @file pio_uart_pio1_driver.h
 * @brief PIO UART driver header for Channel 3 using PIO1
 * 
 * Copy of PIO0 driver adapted for PIO1 SM0/SM1.
 * Used for UART Channel 3 in PRIMARY/SECONDARY device modes.
 */

#ifndef PIO_UART_PIO1_DRIVER_H
#define PIO_UART_PIO1_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "uart/uart_interface.h"
#include "uart/uart_receive_buffer.h"

// PIO1 Configuration
#define PIO1_UART_PIO_INSTANCE   pio1
#define PIO1_UART_TX_SM          0      // State machine 0 for TX
#define PIO1_UART_RX_SM          1      // State machine 1 for RX

// Buffer configuration (same as PIO0 driver)
#define PIO1_UART_RX_BUFFER_SIZE 1536
#define PIO1_UART_DMA_BUFFER_SIZE 1024

/**
 * @brief PIO1 UART context structure
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
    uint8_t tx_dma_buffer[PIO1_UART_DMA_BUFFER_SIZE];
    uint8_t rx_dma_buffer[PIO1_UART_DMA_BUFFER_SIZE];
    
    // Transfer state
    volatile bool tx_in_progress;
    volatile size_t rx_bytes_ready;
    
    // Receive buffer management
    uart_receive_buffer_t rx_ring;
    uint8_t rx_buffer[PIO1_UART_RX_BUFFER_SIZE];
    
    // State tracking
    uart_state_t state;
    uart_config_t current_config;
    
    // Statistics
    uint32_t tx_dma_completions;
    uint32_t rx_dma_completions;
    uint32_t pio_errors;
    uint32_t dma_errors;
    
} pio1_uart_context_t;

// External interface
extern const uart_interface_t pio1_uart_interface;

// Context management functions
void* pio1_create_context(uint8_t pio_num, uint8_t sm_num);
void pio1_destroy_context(void* context);

#endif // PIO_UART_PIO1_DRIVER_H
