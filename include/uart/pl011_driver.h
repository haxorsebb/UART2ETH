/**
 * @file pl011_driver.h
 * @brief PL011 UART driver implementation
 */

#ifndef PL011_DRIVER_DRIVER_H
#define PL011_DRIVER_DRIVER_H

#include <stdint.h>
#include "uart/uart_interface.h"
#include "uart/uart_receive_buffer.h"

#define RX_BUFFER_SIZE 1024

typedef struct {
    uint8_t hw_uart_num;              // 0 or 1
    uart_inst_t* hw_uart;             // uart0 or uart1
    uint32_t irq_num;                 // UART0_IRQ or UART1_IRQ
    
    // RX ring buffer
    uart_receive_buffer_t rx_ring;
    uint8_t rx_buffer[RX_BUFFER_SIZE];
    
    // TX buffer pointer (managed by application)
    const uint8_t* tx_buffer_ptr;     // Current TX buffer
    size_t tx_buffer_remaining;      // Bytes remaining to send
    
    // State
    uart_state_t state;
    uint32_t init_time;
} pl011_context_t;

void* pl011_create_context(uint8_t hw_uart_num);  // 0 or 1 for uart0/uart1
void pl011_destroy_context(void* context);

#endif