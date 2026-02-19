/**
 * @file uart_receive_buffer.c
 * @brief Simple ring buffer implementation for UART RX data
 */

#include "uart/uart_receive_buffer.h"
#include <stdio.h>

void uart_receive_buffer_init(uart_receive_buffer_t* rb, uint8_t* buffer, size_t size) {
    rb->buffer = buffer;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->is_full = false;
}

// Note: This function may be called from IRQ context.
// No printf() or blocking calls allowed.
bool uart_receive_buffer_put(uart_receive_buffer_t* rb, uint8_t byte) {

    size_t next_head = rb->head + 1;
    if (next_head >= rb->size) {
        next_head = 0;
    }

    if (rb->is_full) {
        return false; // Buffer full
    }
        
    
    rb->buffer[rb->head] = byte;
    rb->head = next_head;

    if(rb->head == rb->tail) {
        rb->is_full=true;
    }

    return true;
}

size_t uart_receive_buffer_available(const uart_receive_buffer_t* rb) {
    if ((rb->head >= rb->tail) && !rb->is_full) {
        return rb->head - rb->tail;
    } else {
        return (rb->size - rb->tail) + rb->head;
    }
}

size_t uart_receive_buffer_read(uart_receive_buffer_t* rb, uint8_t* buffer, size_t max_len) {
    size_t available = uart_receive_buffer_available(rb);
    size_t to_read = (available < max_len) ? available : max_len;
    
    for (size_t i = 0; i < to_read; i++) {
        buffer[i] = rb->buffer[rb->tail];
        rb->tail++;
        rb->is_full = false;

        if(rb->tail >= rb->size) {
            rb->tail=0;
        }
    }
    
    return to_read;
}

void uart_receive_buffer_clear(uart_receive_buffer_t* rb) {
    rb->head = 0;
    rb->tail = 0;
    rb->is_full = false;
}