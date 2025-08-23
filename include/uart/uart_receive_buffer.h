/**
 * @file uart_receive_buffer.h
 * @brief Simple ring buffer for UART RX data
 */

#ifndef UART_RECEIVE_BUFFER_H
#define UART_RECEIVE_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t* buffer;
    size_t size;
    volatile size_t head;
    volatile size_t tail;
    bool is_full;
} uart_receive_buffer_t;

/**
 * Initialize ring buffer
 * @param rb Ring buffer structure
 * @param buffer Buffer memory
 * @param size Buffer size
 */
void uart_receive_buffer_init(uart_receive_buffer_t* rb, uint8_t* buffer, size_t size);

/**
 * Put byte into ring buffer (used by ISR)
 * @param rb Ring buffer
 * @param byte Byte to store
 * @return true if stored, false if buffer full
 */
bool uart_receive_buffer_put(uart_receive_buffer_t* rb, uint8_t byte);

/**
 * Get available bytes count
 * @param rb Ring buffer
 * @return Number of bytes available for reading
 */
size_t uart_receive_buffer_available(const uart_receive_buffer_t* rb);

/**
 * Read data from ring buffer
 * @param rb Ring buffer
 * @param buffer Output buffer
 * @param max_len Maximum bytes to read
 * @return Actual bytes read
 */
size_t uart_receive_buffer_read(uart_receive_buffer_t* rb, uint8_t* buffer, size_t max_len);

/**
 * Clear ring buffer
 * @param rb Ring buffer
 */
void uart_receive_buffer_clear(uart_receive_buffer_t* rb);

#endif // UART_RECEIVE_BUFFER_H