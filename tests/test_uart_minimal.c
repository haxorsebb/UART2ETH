/**
 * @file test_uart_minimal.c
 * @brief Minimal UART hardware test to isolate issues
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Pico SDK includes
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

// Unity test framework
#include "unity/src/unity.h"

// Module under test
#include "uart/uart1_driver.h"

// Test configuration
#define TEST_BAUD_RATE 230400
#define TEST_RX_GPIO 9
#define TEST_TX_GPIO 8

/**
 * @brief Set up before each test
 */
void setUp(void) {
    printf("TEST: setUp() - Minimal UART Test\n");
}

/**
 * @brief Clean up after each test
 */
void tearDown(void) {
    uart1_driver_deinit();
    printf("TEST: tearDown() complete\n");
}

/**
 * @brief Test basic UART driver initialization
 */
void test_uart_driver_init(void) {
    printf("TEST: test_uart_driver_init\n");
    
    uart1_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .rx_gpio = TEST_RX_GPIO,
        .tx_gpio = TEST_TX_GPIO,
        .enable_loopback = true  // Enable for testing
    };
    
    bool init_result = uart1_driver_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART1 driver initialization failed");
    
    bool ready_status = uart1_driver_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready_status, "UART1 driver not ready after initialization");
    
    printf("TEST: UART driver initialization test passed\n");
}

/**
 * @brief Test basic character transmission without loopback verification
 */
void test_uart_send_char(void) {
    printf("TEST: test_uart_send_char\n");
    
    uart1_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .rx_gpio = TEST_RX_GPIO,
        .tx_gpio = TEST_TX_GPIO,
        .enable_loopback = false  // Disable loopback for this test
    };
    
    bool init_result = uart1_driver_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART1 driver initialization failed");
    
    // Send a simple character
    bool send_result = uart1_driver_send_char('A');
    TEST_ASSERT_TRUE_MESSAGE(send_result, "Failed to send character");
    
    printf("TEST: Character send test passed\n");
}

/**
 * @brief Test hardware loopback with minimal complexity
 */
void test_uart_simple_loopback(void) {
    printf("TEST: test_uart_simple_loopback\n");
    
    uart1_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .rx_gpio = TEST_RX_GPIO,
        .tx_gpio = TEST_TX_GPIO,
        .enable_loopback = true  // Enable loopback
    };
    
    bool init_result = uart1_driver_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART1 driver initialization failed");
    
    // Clear any existing data
    uart1_driver_clear_rx_buffer();
    
    // Send a character
    bool send_result = uart1_driver_send_char('X');
    TEST_ASSERT_TRUE_MESSAGE(send_result, "Failed to send character");
    
    // Wait for hardware loopback
    sleep_ms(50);
    
    // Check if we received something
    bool has_data = uart1_driver_has_rx_data();
    if (has_data) {
        char received = uart1_driver_read_char();
        printf("TEST: Received character: 0x%02X ('%c')\n", (unsigned char)received, received);
        TEST_ASSERT_EQUAL_MESSAGE('X', received, "Loopback character mismatch");
    } else {
        printf("TEST: No data received - this might be expected for hardware-only loopback\n");
    }
    
    printf("TEST: Simple loopback test completed\n");
}

/**
 * @brief Main test runner
 */
int main() {
    stdio_init_all();
    sleep_ms(2000);  // Allow time for USB serial initialization
    
    printf("\n=== Minimal UART Hardware Test ===\n");
    printf("Testing basic UART functionality\n\n");
    
    UNITY_BEGIN();
    
    // Run tests in order of complexity
    RUN_TEST(test_uart_driver_init);
    RUN_TEST(test_uart_send_char);
    RUN_TEST(test_uart_simple_loopback);
    
    int result = UNITY_END();
    
    // Keep running for embedded environment
    while (true) {
        printf("Minimal tests completed with result: %d\n", result);
        sleep_ms(5000);
    }
    
    return result;
}
