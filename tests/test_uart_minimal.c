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
#include "uart/uart_manager.h"

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
    uart_manager_deinit();
    printf("TEST: tearDown() complete\n");
}

/**
 * @brief Test UART manager status without initialization (safe)
 */
void test_uart_manager_status_only(void) {
    printf("TEST: test_uart_manager_status_only\n");
    
    // Test uninitialized state first
    uart_manager_status_t status = uart_manager_get_status();
    printf("TEST: Uninitialized status: %d\n", status);
    
    bool ready = uart_manager_is_ready();
    printf("TEST: Uninitialized ready: %d\n", ready);
    
    printf("TEST: Status-only test passed\n");
}

/**
 * @brief Test UART manager initialization (may cause hang)
 */
void test_uart_manager_init_careful(void) {
    printf("TEST: test_uart_manager_init_careful - attempting init...\n");
    
    bool init_result = uart_manager_init();
    if (init_result) {
        printf("TEST: Init succeeded\n");
        uart_manager_status_t status = uart_manager_get_status();
        printf("TEST: Status after init: %d\n", status);
    } else {
        printf("TEST: Init failed\n");
    }
    
    printf("TEST: Init test completed\n");
}

/**
 * @brief Test UART manager status functions only (no data processing)
 */
void test_uart_status_safe(void) {
    printf("TEST: test_uart_status_safe\n");
    
    bool init_result = uart_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Manager initialization failed");
    
    // Test safe status functions only
    uart_manager_status_t status = uart_manager_get_status();
    printf("TEST: Manager status: %d\n", status);
    
    // Test has_incoming_work (should be safe read-only)
    bool has_work = uart_manager_has_incoming_work();
    printf("TEST: Has work: %d\n", has_work);
    
    printf("TEST: Status test passed\n");
}

/**
 * @brief Test UART manager statistics (avoiding data processing)
 */
void test_uart_statistics_safe(void) {
    printf("TEST: test_uart_statistics_safe\n");
    
    bool init_result = uart_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Manager initialization failed");
    
    uart_manager_stats_t stats;
    uart_manager_get_stats(&stats);
    
    printf("TEST: Stats - status: %d, uptime: %lu, msgs: %lu\n", 
           stats.status, stats.uptime_seconds, stats.messages_processed);
    
    // Simple validation without deep diagnostics
    TEST_ASSERT_EQUAL_MESSAGE(UART_MANAGER_STATUS_READY, stats.status, "Stats status incorrect");
    
    printf("TEST: Safe statistics test passed\n");
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
    
    // Run tests in order of safety - isolating hang issues
    RUN_TEST(test_uart_manager_status_only);
    RUN_TEST(test_uart_manager_init_careful);
    RUN_TEST(test_uart_status_safe);
    
    int result = UNITY_END();
    
    // Keep running for embedded environment
    while (true) {
        printf("Minimal tests completed with result: %d\n", result);
        sleep_ms(5000);
    }
    
    return result;
}
