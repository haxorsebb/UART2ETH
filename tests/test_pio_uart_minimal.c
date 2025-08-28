/**
 * @file test_pio_uart_minimal.c
 * @brief Minimal PIO UART driver test - direct driver testing
 * 
 * Bypasses UART manager to test PIO driver directly
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "unity/src/unity.h"

#include "uart/pio_uart_driver.h"
#include "uart/uart_interface.h"

#define TEST_BAUD_RATE 230400
#define PIO_UART_TX_GPIO 14
#define PIO_UART_RX_GPIO 15
#define PIO_TEST_MESSAGE "Hello PIO!"

static void* pio_context = NULL;

void setUp(void) {
    printf("TEST: Minimal PIO UART setUp\n");
    
    // Initialize GPIO for manual connectivity test
    gpio_init(PIO_UART_TX_GPIO);
    gpio_init(PIO_UART_RX_GPIO);
    
    // Create PIO context
    pio_context = pio_create_context(0, 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(pio_context, "Failed to create PIO context");
}

void tearDown(void) {
    if (pio_context) {
        pio_destroy_context(pio_context);
        pio_context = NULL;
    }
    printf("TEST: Minimal PIO UART tearDown\n");
}

/**
 * @brief Test GPIO loopback connectivity  
 */
void test_minimal_gpio_connectivity(void) {
    printf("\n=== MINIMAL GPIO Connectivity Test ===\n");
    
    gpio_set_dir(PIO_UART_TX_GPIO, GPIO_OUT);
    gpio_set_dir(PIO_UART_RX_GPIO, GPIO_IN);
    
    // Test high
    gpio_put(PIO_UART_TX_GPIO, 1);
    sleep_ms(10);
    bool high_test = gpio_get(PIO_UART_RX_GPIO);
    
    // Test low
    gpio_put(PIO_UART_TX_GPIO, 0);
    sleep_ms(10);
    bool low_test = gpio_get(PIO_UART_RX_GPIO);
    
    printf("GPIO Test: HIGH->%s, LOW->%s\n", 
           high_test ? "HIGH" : "LOW", low_test ? "HIGH" : "LOW");
    
    TEST_ASSERT_TRUE_MESSAGE(high_test && !low_test, "GPIO connectivity failed");
}

/**
 * @brief Test direct PIO UART initialization
 */
void test_minimal_pio_uart_init(void) {
    printf("\n=== MINIMAL PIO UART Init Test ===\n");
    
    uart_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .parity = UART_PARITY_NONE,
        .stop_bits = 1,
        .tx_gpio = PIO_UART_TX_GPIO,
        .rx_gpio = PIO_UART_RX_GPIO
    };
    
    bool init_result = pio_uart_interface.init(pio_context, &config);
    TEST_ASSERT_TRUE_MESSAGE(init_result, "PIO UART init failed");
    
    bool ready = pio_uart_interface.is_ready(pio_context);
    TEST_ASSERT_TRUE_MESSAGE(ready, "PIO UART not ready after init");
    
    printf("PIO UART initialized successfully\n");
}

/**
 * @brief Test direct single byte transmission
 */
void test_minimal_single_byte_tx(void) {
    printf("\n=== MINIMAL Single Byte TX Test ===\n");
    
    uart_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .parity = UART_PARITY_NONE,
        .stop_bits = 1,
        .tx_gpio = PIO_UART_TX_GPIO,
        .rx_gpio = PIO_UART_RX_GPIO
    };
    
    pio_uart_interface.init(pio_context, &config);
    
    // Send single byte
    uint8_t test_byte = 'A';
    printf("Sending byte: 0x%02X ('%c')\n", test_byte, test_byte);
    pio_uart_interface.send_byte(pio_context, test_byte);
    
    // Wait for transmission
    sleep_ms(10);
    
    bool tx_complete = pio_uart_interface.is_tx_complete(pio_context);
    printf("TX Complete: %s\n", tx_complete ? "YES" : "NO");
    
    TEST_ASSERT_TRUE_MESSAGE(tx_complete, "TX not complete");
}

/**
 * @brief Test direct single byte loopback
 */
void test_minimal_single_byte_loopback(void) {
    printf("\n=== MINIMAL Single Byte Loopback Test ===\n");
    
    uart_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .parity = UART_PARITY_NONE,
        .stop_bits = 1,
        .tx_gpio = PIO_UART_TX_GPIO,
        .rx_gpio = PIO_UART_RX_GPIO
    };
    
    pio_uart_interface.init(pio_context, &config);
    
    // Send test byte
    uint8_t test_byte = 'X';
    printf("Sending byte: 0x%02X ('%c')\n", test_byte, test_byte);
    pio_uart_interface.send_byte(pio_context, test_byte);
    
    // Wait for loopback
    sleep_ms(50);
    
    // Check for received data
    bool has_rx_data = pio_uart_interface.has_rx_data(pio_context);
    printf("Has RX data: %s\n", has_rx_data ? "YES" : "NO");
    
    if (has_rx_data) {
        uint8_t received = pio_uart_interface.read_byte(pio_context);
        printf("Received byte: 0x%02X ('%c')\n", received, received);
        TEST_ASSERT_EQUAL_MESSAGE(test_byte, received, "Loopback byte mismatch");
    } else {
        TEST_FAIL_MESSAGE("No data received in loopback test");
    }
}

/**
 * @brief Test multiple byte loopback
 */
void test_minimal_multi_byte_loopback(void) {
    printf("\n=== MINIMAL Multi Byte Loopback Test ===\n");
    
    uart_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .parity = UART_PARITY_NONE,
        .stop_bits = 1,
        .tx_gpio = PIO_UART_TX_GPIO,
        .rx_gpio = PIO_UART_RX_GPIO
    };
    
    pio_uart_interface.init(pio_context, &config);
    
    // Send test message
    const char* test_msg = PIO_TEST_MESSAGE;
    size_t msg_len = strlen(test_msg);
    printf("Sending message: '%s' (%u bytes)\n", test_msg, msg_len);
    
    size_t sent = pio_uart_interface.send_data(pio_context, (uint8_t*)test_msg, msg_len);
    printf("Sent: %u bytes\n", sent);
    TEST_ASSERT_EQUAL_MESSAGE(msg_len, sent, "Not all bytes sent");
    
    // Wait for loopback
    sleep_ms(100);
    
    // Read received data
    uint8_t rx_buffer[64];
    size_t rx_count = 0;
    
    while (pio_uart_interface.has_rx_data(pio_context) && rx_count < sizeof(rx_buffer) - 1) {
        rx_buffer[rx_count++] = pio_uart_interface.read_byte(pio_context);
        sleep_ms(1); // Small delay between reads
    }
    
    rx_buffer[rx_count] = '\0';
    printf("Received: '%s' (%u bytes)\n", rx_buffer, rx_count);
    
    TEST_ASSERT_EQUAL_MESSAGE(msg_len, rx_count, "Received byte count mismatch");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_msg, (char*)rx_buffer, "Received message mismatch");
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n=========================================\n");
    printf("PIO UART Minimal Driver Tests\n");
    printf("Direct driver testing - bypassing UART manager\n");
    printf("GP14<->GP15 loopback required\n");
    printf("=========================================\n\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_minimal_gpio_connectivity);
    RUN_TEST(test_minimal_pio_uart_init);
    RUN_TEST(test_minimal_single_byte_tx);
    RUN_TEST(test_minimal_single_byte_loopback);
    RUN_TEST(test_minimal_multi_byte_loopback);
    
    int result = UNITY_END();
    
    printf("\n=========================================\n");
    if (result == 0) {
        printf("ALL MINIMAL TESTS PASSED!\n");
    } else {
        printf("SOME TESTS FAILED!\n");
    }
    printf("=========================================\n");
    
    return result;
}
