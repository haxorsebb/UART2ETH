/**
 * @file test_pio_uart_integration.c
 * @brief Test PIO UART driver through UART manager interface
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "uart/uart_manager.h"
#include "uart/uart_interface.h"
#include "log_manager.h"
#include "shared_memory.h"
#include "state_machine.h"
#include "ringbuffer.h"

#define TX_GPIO 14
#define RX_GPIO 15

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("=== PIO UART Manager Integration Test ===\n");
    
    // Initialize subsystems
    if (!shared_memory_init()) {
        printf("ERROR: shared_memory_init failed\n");
        return -1;
    }
    
    if (!log_manager_init()) {
        printf("ERROR: log_manager_init failed\n");
        return -1;
    }
    
    if (!state_machine_init()) {
        printf("ERROR: state_machine_init failed\n");
        return -1;
    }
    
    if (!ringbuffer_init()) {
        printf("ERROR: ringbuffer_init failed\n");
        return -1;
    }
    
    printf("Subsystems initialized\n");
    
    // Initialize UART manager
    if (!uart_manager_init()) {
        printf("ERROR: uart_manager_init failed\n");
        return -1;
    }
    
    printf("UART Manager initialized\n");
    sleep_ms(100); // Let it settle
    
    // Check UART manager status
    if (!uart_manager_is_ready()) {
        printf("ERROR: UART manager not ready\n");
        return -1;
    }
    
    printf("UART Manager ready\n");
    
    // Get Channel 2 instance directly
    uart_instance_t* channel_2 = uart_manager_get_channel_instance(CHANNEL_2);
    if (!channel_2) {
        printf("ERROR: Channel 2 instance not found\n");
        return -1;
    }
    
    printf("Channel 2 instance: %p\n", channel_2);
    printf("Channel 2 ops: %p\n", channel_2->ops);
    printf("Channel 2 context: %p\n", channel_2->driver_context);
    
    // Test direct driver interface
    if (!channel_2->ops->is_ready(channel_2->driver_context)) {
        printf("ERROR: Channel 2 driver not ready\n");
        return -1;
    }
    
    printf("Channel 2 driver ready\n");
    
    // Test TX ready
    bool tx_ready = channel_2->ops->is_tx_ready(channel_2->driver_context);
    printf("TX Ready: %s\n", tx_ready ? "Yes" : "No");
    
    // Send single byte directly through driver
    uint8_t test_byte = 'X';
    printf("Sending byte 0x%02X ('%c') directly...\n", test_byte, test_byte);
    channel_2->ops->send_byte(channel_2->driver_context, test_byte);
    
    // Wait for TX complete
    int timeout = 1000;
    while (!channel_2->ops->is_tx_complete(channel_2->driver_context) && timeout-- > 0) {
        sleep_ms(1);
    }
    
    if (timeout <= 0) {
        printf("ERROR: TX timeout\n");
        return -1;
    }
    
    printf("TX complete\n");
    
    // Check for RX data
    timeout = 1000;
    while (!channel_2->ops->has_rx_data(channel_2->driver_context) && timeout-- > 0) {
        sleep_ms(1);
    }
    
    if (timeout <= 0) {
        printf("ERROR: No RX data received\n");
        return -1;
    }
    
    printf("RX data available\n");
    
    // Read received data
    uint8_t received = channel_2->ops->read_byte(channel_2->driver_context);
    printf("Received: 0x%02X ('%c')\n", received, received);
    
    if (received == test_byte) {
        printf("SUCCESS: Direct driver loopback working!\n");
    } else {
        printf("ERROR: Data mismatch\n");
        return -1;
    }
    
    // Test string through driver
    const char* test_string = "TEST123";
    printf("Sending string '%s'...\n", test_string);
    
    size_t sent = channel_2->ops->send_data(channel_2->driver_context, 
                                           (const uint8_t*)test_string, 
                                           strlen(test_string));
    
    printf("Sent %zu bytes\n", sent);
    
    // Wait for TX complete
    timeout = 1000;
    while (!channel_2->ops->is_tx_complete(channel_2->driver_context) && timeout-- > 0) {
        sleep_ms(1);
    }
    
    printf("TX complete\n");
    
    // Read back data
    char recv_buffer[20] = {0};
    size_t total_received = 0;
    
    timeout = 2000; // Longer timeout for string
    while (total_received < strlen(test_string) && timeout-- > 0) {
        if (channel_2->ops->has_rx_data(channel_2->driver_context)) {
            recv_buffer[total_received] = channel_2->ops->read_byte(channel_2->driver_context);
            total_received++;
        } else {
            sleep_ms(1);
        }
    }
    
    printf("Received %zu bytes: '%s'\n", total_received, recv_buffer);
    
    if (strcmp(test_string, recv_buffer) == 0) {
        printf("SUCCESS: String loopback through UART manager working!\n");
    } else {
        printf("ERROR: String mismatch\n");
    }
    
    printf("Integration test complete\n");
    
    while (true) {
        sleep_ms(1000);
    }
    
    return 0;
}
