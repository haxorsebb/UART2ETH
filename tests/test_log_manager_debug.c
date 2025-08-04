/**
 * @file test_log_manager_debug.c
 * @brief Debug version of log manager tests with verbose output
 */

// Explicitly enable USB reset interface - CRITICAL for autonomous flashing
#define PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_FLASH_BOOT 1

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
 * Debug test: Single log message with detailed output
 */
void test_single_log_message_debug(void) {
    printf("=== Debug: Single Log Message Test ===\n");
    
    // Get initial state
    uint32_t initial_pending = log_manager_get_pending_count();
    uint32_t initial_total = log_manager_get_total_count();
    uint32_t initial_util = log_manager_get_utilization();
    
    printf("Initial state: pending=%lu, total=%lu, util=%lu%%\n", 
           initial_pending, initial_total, initial_util);
    
    // Log a message
    bool result = log_message(0, LOG_LEVEL_INFO, "Debug test message");
    printf("Log result: %s\n", result ? "SUCCESS" : "FAILED");
    
    // Get state after logging
    uint32_t after_pending = log_manager_get_pending_count();
    uint32_t after_total = log_manager_get_total_count();
    uint32_t after_util = log_manager_get_utilization();
    
    printf("After logging: pending=%lu, total=%lu, util=%lu%%\n", 
           after_pending, after_total, after_util);
    
    // Get raw shared memory state
    shared_memory_layout_t* layout = shared_memory_get_layout();
    printf("Raw state: write_head=%lu, read_head=%lu, buffer_size=%lu\n",
           layout->log_mgmt.write_head, layout->log_mgmt.read_head, 
           layout->log_mgmt.buffer_size);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, after_pending);
    TEST_ASSERT_EQUAL(1, after_total);
}

/**
 * Debug test: Buffer utilization calculation
 */
void test_buffer_utilization_debug(void) {
    printf("=== Debug: Buffer Utilization Test ===\n");
    
    // Log a message and check utilization calculation
    bool result = log_message(1, LOG_LEVEL_ERROR, "Utilization test message");
    printf("Log result: %s\n", result ? "SUCCESS" : "FAILED");
    
    uint32_t pending = log_manager_get_pending_count();
    uint32_t util = log_manager_get_utilization();
    
    printf("After logging: pending=%lu, utilization=%lu%%\n", pending, util);
    
    // Check shared memory state
    shared_memory_layout_t* layout = shared_memory_get_layout();
    uint32_t used_bytes = (layout->log_mgmt.write_head >= layout->log_mgmt.read_head) ?
        (layout->log_mgmt.write_head - layout->log_mgmt.read_head) :
        (layout->log_mgmt.buffer_size - layout->log_mgmt.read_head + layout->log_mgmt.write_head);
    
    printf("Calculated used bytes: %lu / %lu\n", used_bytes, layout->log_mgmt.buffer_size);
    printf("Manual utilization: %lu%%\n", (used_bytes * 100) / layout->log_mgmt.buffer_size);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_GREATER_THAN(0, util);
}

/**
 * Debug test: Long message handling
 */
void test_long_message_debug(void) {
    printf("=== Debug: Long Message Test ===\n");
    
    // Create exactly max length message
    char long_message[LOG_MESSAGE_MAX_LENGTH + 1];
    memset(long_message, 'X', LOG_MESSAGE_MAX_LENGTH - 1);
    long_message[LOG_MESSAGE_MAX_LENGTH - 1] = '\0';
    
    printf("Message length: %lu (max allowed: %d)\n", 
           strlen(long_message), LOG_MESSAGE_MAX_LENGTH);
    
    bool result = log_message(0, LOG_LEVEL_INFO, long_message);
    printf("Long message log result: %s\n", result ? "SUCCESS" : "FAILED");
    
    if (!result) {
        // Try with shorter message
        char shorter[100];
        memset(shorter, 'Y', 99);
        shorter[99] = '\0';
        
        printf("Trying shorter message (length %lu)...\n", strlen(shorter));
        bool shorter_result = log_message(0, LOG_LEVEL_INFO, shorter);
        printf("Shorter message result: %s\n", shorter_result ? "SUCCESS" : "FAILED");
    }
    
    TEST_ASSERT_TRUE_MESSAGE(result, "Long message should be accepted");
}

// Test runner
int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("Starting Log Manager Debug Tests...\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_single_log_message_debug);
    RUN_TEST(test_buffer_utilization_debug);
    RUN_TEST(test_long_message_debug);
    
    while (true) {
        printf("Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}
