/**
 * @file test_pio_direct.c
 * @brief Direct PIO UART loopback test (minimal)
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

// Import PIO programs
#include "pio_uart_tx.pio.h"
#include "pio_uart_rx.pio.h"

#define TX_GPIO 14
#define RX_GPIO 15
#define BAUD_RATE 230400

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("=== Direct PIO UART Loopback Test ===\n");
    
    // Test GPIO loopback first
    gpio_init(TX_GPIO);
    gpio_init(RX_GPIO);
    gpio_set_dir(TX_GPIO, GPIO_OUT);
    gpio_set_dir(RX_GPIO, GPIO_IN);
    
    gpio_put(TX_GPIO, 1);
    sleep_ms(10);
    bool high_test = gpio_get(RX_GPIO);
    
    gpio_put(TX_GPIO, 0);
    sleep_ms(10);
    bool low_test = gpio_get(RX_GPIO);
    
    printf("GPIO Loopback: HIGH->%s, LOW->%s\n", 
           high_test ? "HIGH" : "LOW", 
           low_test ? "HIGH" : "LOW");
    
    if (!high_test || low_test) {
        printf("ERROR: GPIO loopback failed\n");
        return -1;
    }
    
    // Initialize PIO UART
    PIO pio = pio0;
    uint tx_sm = 0;
    uint rx_sm = 1;
    
    // Load programs
    uint tx_offset = pio_add_program(pio, &pio_uart_tx_program);
    uint rx_offset = pio_add_program(pio, &pio_uart_rx_program);
    
    printf("PIO Programs loaded: TX offset %u, RX offset %u\n", tx_offset, rx_offset);
    
    // Initialize TX
    pio_uart_tx_program_init(pio, tx_sm, tx_offset, TX_GPIO, BAUD_RATE);
    printf("TX initialized\n");
    
    // Initialize RX  
    pio_uart_rx_program_init(pio, rx_sm, rx_offset, RX_GPIO, BAUD_RATE);
    printf("RX initialized\n");
    
    sleep_ms(100); // Let things settle
    
    // Test simple loopback
    char test_byte = 'A';
    printf("Sending byte: 0x%02X ('%c')\n", test_byte, test_byte);
    
    // Send byte
    pio_uart_tx_program_putc(pio, tx_sm, test_byte);
    printf("Byte sent to TX FIFO\n");
    
    // Wait for receive (with timeout)
    int timeout = 1000; // 1 second
    while (pio_sm_is_rx_fifo_empty(pio, rx_sm) && timeout-- > 0) {
        sleep_ms(1);
    }
    
    if (timeout <= 0) {
        printf("ERROR: No data received (timeout)\n");
        return -1;
    }
    
    // Read received byte
    char received = pio_uart_rx_program_getc(pio, rx_sm);
    printf("Received byte: 0x%02X ('%c')\n", received, received);
    
    if (received == test_byte) {
        printf("SUCCESS: PIO UART loopback working!\n");
    } else {
        printf("ERROR: Data mismatch (sent 0x%02X, received 0x%02X)\n", 
               test_byte, received);
        return -1;
    }
    
    // Test string
    const char* test_string = "HELLO";
    printf("Sending string: '%s'\n", test_string);
    
    pio_uart_tx_program_puts(pio, tx_sm, test_string);
    
    // Receive string
    char recv_buffer[10] = {0};
    for (int i = 0; i < strlen(test_string); i++) {
        timeout = 1000;
        while (pio_sm_is_rx_fifo_empty(pio, rx_sm) && timeout-- > 0) {
            sleep_ms(1);
        }
        if (timeout > 0) {
            recv_buffer[i] = pio_uart_rx_program_getc(pio, rx_sm);
        } else {
            printf("ERROR: Timeout on char %d\n", i);
            break;
        }
    }
    
    printf("Received string: '%s'\n", recv_buffer);
    
    if (strcmp(test_string, recv_buffer) == 0) {
        printf("SUCCESS: String loopback working!\n");
    } else {
        printf("ERROR: String mismatch\n");
    }
    
    printf("Test complete. Target responsive.\n");
    while (true) {
        sleep_ms(1000);
    }
    
    return 0;
}
