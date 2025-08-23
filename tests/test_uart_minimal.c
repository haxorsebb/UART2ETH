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
 * @brief Test basic UART manager initialization
 */
void test_uart_manager_init(void) {
    printf("TEST: test_uart_manager_init\n");
    
    bool init_result = uart_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Manager initialization failed");
    
    bool ready_status = uart_manager_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready_status, "UART Manager not ready after initialization");
    
    uart_manager_status_t status = uart_manager_get_status();
    TEST_ASSERT_EQUAL_MESSAGE(UART_MANAGER_STATUS_READY, status, "UART Manager status incorrect");
    
    printf("TEST: UART Manager initialization test passed\n");
}

/**
 * @brief Test UART manager data processing functions
 */
void test_uart_data_processing(void) {
    printf("TEST: test_uart_data_processing\n");
    
    bool init_result = uart_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Manager initialization failed");
    
    // Test processing functions don't crash
    bool has_work = uart_manager_has_incoming_work();
    bool process_in = uart_manager_process_incoming_data();
    bool process_out = uart_manager_process_outgoing_data();
    
    // These may return false if no data present, but should not crash
    printf("TEST: Has work: %d, Process IN: %d, Process OUT: %d\n", 
           has_work, process_in, process_out);
    
    printf("TEST: Data processing test passed\n");
}

/**
 * @brief Test UART manager statistics and diagnostics
 */
void test_uart_statistics(void) {
    printf("TEST: test_uart_statistics\n");
    
    bool init_result = uart_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Manager initialization failed");
    
    uart_manager_stats_t stats;
    uart_manager_get_stats(&stats);
    
    // Verify stats structure is populated
    TEST_ASSERT_EQUAL_MESSAGE(UART_MANAGER_STATUS_READY, stats.status, "Stats status incorrect");
    
    // Test diagnostic info
    char diag_buffer[512];
    int diag_len = uart_manager_get_diagnostic_info(diag_buffer, sizeof(diag_buffer));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, diag_len, "No diagnostic info returned");
    
    printf("TEST: Diagnostic info: %.*s\n", diag_len > 100 ? 100 : diag_len, diag_buffer);
    printf("TEST: Statistics test passed\n");
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
    RUN_TEST(test_uart_manager_init);
    RUN_TEST(test_uart_data_processing);
    RUN_TEST(test_uart_statistics);
    
    int result = UNITY_END();
    
    // Keep running for embedded environment
    while (true) {
        printf("Minimal tests completed with result: %d\n", result);
        sleep_ms(5000);
    }
    
    return result;
}
