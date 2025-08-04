/**
 * @file test_log_manager.c
 * @brief Unit tests for log manager lock-reserve-release-write pattern
 * 
 * Tests the logging functionality according to the design documented
 * in arc42 Chapter 5 and Chapter 6 runtime view.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Log Manager Whitebox
 * - arc42 Chapter 6 - Runtime View - Log Synchronization Pattern
 */

#include "unity.h"
#include "log_manager.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

void setUp(void) {
    // Initialize shared memory before each test
    shared_memory_init();
    log_manager_init();
}

void tearDown(void) {
    // Called after each test
}

/**
 * Test: Log manager should initialize successfully after shared memory init
 * 
 * This tests the most atomic condition: that the log manager can be
 * initialized after shared memory is set up.
 */
void test_log_manager_initialization(void) {
    // ARRANGE: Shared memory is initialized in setUp()
    
    // ACT: Initialize log manager (already done in setUp, test re-init)
    bool result = log_manager_init();
    
    // ASSERT: Re-initialization should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "Log manager re-initialization should succeed");
}

/**
 * Test: Single log message should be queued successfully
 * 
 * Tests the basic logging functionality with lock-reserve-release-write pattern.
 */
void test_single_log_message_queued(void) {
    // ARRANGE: Log manager initialized in setUp()
    
    // ACT: Log a simple message from core 0
    bool result = log_message(0, LOG_LEVEL_INFO, "Test message");
    
    // ASSERT: Message should be queued successfully
    TEST_ASSERT_TRUE_MESSAGE(result, "Single log message should be queued successfully");
    
    // ASSERT: Pending count should be 1
    uint32_t pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL_MESSAGE(1, pending, "Pending count should be 1 after logging one message");
    
    // ASSERT: Total count should be 1
    uint32_t total = log_manager_get_total_count();
    TEST_ASSERT_EQUAL_MESSAGE(1, total, "Total count should be 1 after logging one message");
}

/**
 * Test: Log buffer utilization should increase after logging
 * 
 * Tests that buffer utilization tracking works correctly.
 */
void test_log_buffer_utilization(void) {
    // ARRANGE: Log manager initialized, get initial utilization
    uint32_t initial_utilization = log_manager_get_utilization();
    
    // ACT: Log a message 
    bool result = log_message(1, LOG_LEVEL_ERROR, "Error message for utilization test");
    TEST_ASSERT_TRUE(result);
    
    // ASSERT: Utilization should increase
    uint32_t after_utilization = log_manager_get_utilization();
    TEST_ASSERT_GREATER_THAN_MESSAGE(initial_utilization, after_utilization,
        "Buffer utilization should increase after logging message");
    
    // ASSERT: Utilization should be reasonable (not over 100%)
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(100, after_utilization,
        "Buffer utilization should not exceed 100%");
}

/**
 * Test: Multiple messages from different cores should be queued
 * 
 * Tests concurrent logging from both cores (simulated sequentially in test).
 */
void test_multiple_messages_different_cores(void) {
    // ARRANGE: Log manager initialized
    
    // ACT: Log messages from both cores
    bool result1 = log_message(0, LOG_LEVEL_DEBUG, "Core 0 debug message");
    bool result2 = log_message(1, LOG_LEVEL_INFO, "Core 1 info message");  
    bool result3 = log_message(0, LOG_LEVEL_WARN, "Core 0 warning message");
    
    // ASSERT: All messages should be queued
    TEST_ASSERT_TRUE_MESSAGE(result1, "Core 0 message 1 should be queued");
    TEST_ASSERT_TRUE_MESSAGE(result2, "Core 1 message should be queued");
    TEST_ASSERT_TRUE_MESSAGE(result3, "Core 0 message 2 should be queued");
    
    // ASSERT: Pending count should be 3
    uint32_t pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL_MESSAGE(3, pending, "Pending count should be 3 after logging three messages");
    
    // ASSERT: Total count should be 3
    uint32_t total = log_manager_get_total_count();
    TEST_ASSERT_EQUAL_MESSAGE(3, total, "Total count should be 3 after logging three messages");
}

/**
 * Test: Core1 printer should consume pending messages
 * 
 * Tests the Core1 background printing functionality.
 */
void test_core1_printer_consumes_messages(void) {
    // ARRANGE: Log some messages first
    log_message(0, LOG_LEVEL_INFO, "Message 1");
    log_message(1, LOG_LEVEL_INFO, "Message 2");
    uint32_t initial_pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL(2, initial_pending);
    
    // ACT: Call Core1 printer task
    uint32_t printed_count = log_manager_print_pending();
    
    // ASSERT: Printer should report printing messages
    TEST_ASSERT_EQUAL_MESSAGE(2, printed_count, 
        "Printer should report printing 2 messages");
    
    // ASSERT: Pending count should decrease
    uint32_t after_pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL_MESSAGE(0, after_pending,
        "Pending count should be 0 after printing all messages");
    
    // ASSERT: Total count should remain the same
    uint32_t total = log_manager_get_total_count();
    TEST_ASSERT_EQUAL_MESSAGE(2, total,
        "Total count should remain 2 after printing (not reset)");
}

/**
 * Test: Long message should be handled correctly
 * 
 * Tests that messages at the maximum length are handled properly.
 */
void test_long_message_handling(void) {
    // ARRANGE: Create a message at max length
    char long_message[LOG_MESSAGE_MAX_LENGTH + 1];
    memset(long_message, 'A', LOG_MESSAGE_MAX_LENGTH - 1);
    long_message[LOG_MESSAGE_MAX_LENGTH - 1] = '\0';
    
    // ACT: Log the long message
    bool result = log_message(0, LOG_LEVEL_INFO, long_message);
    
    // ASSERT: Long message should be queued successfully
    TEST_ASSERT_TRUE_MESSAGE(result, "Long message should be queued successfully");
    
    // ASSERT: Pending count should be 1
    uint32_t pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL_MESSAGE(1, pending, "Long message should count as 1 pending message");
}

// Test runner
int main() {
    // Initialize Pico SDK
    stdio_init_all();
    
    // Wait for USB-serial connection
    sleep_ms(2000);
    
    printf("Starting Log Manager Tests...\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_log_manager_initialization);
    RUN_TEST(test_single_log_message_queued);
    RUN_TEST(test_log_buffer_utilization);
    RUN_TEST(test_multiple_messages_different_cores);
    RUN_TEST(test_core1_printer_consumes_messages);
    RUN_TEST(test_long_message_handling);
    
    return UNITY_END();
}
