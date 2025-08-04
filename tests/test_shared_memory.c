/**
 * @file test_shared_memory.c
 * @brief Unit tests for shared memory layout and configuration manager
 * 
 * Tests the basic shared memory initialization and size calculations
 * according to the design documented in arc42 Chapter 5.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Configuration Manager Whitebox  
 * - arc42 Chapter 5 - Log Manager Whitebox
 */

#include "unity.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include <stdio.h>

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

/**
 * Test: Shared memory layout should initialize successfully
 * 
 * This tests the most atomic condition: that our shared memory
 * can be initialized and basic structure is accessible.
 */
void test_shared_memory_initialization(void) {
    // ARRANGE: Nothing needed
    
    // ACT: Initialize shared memory
    bool result = shared_memory_init();
    
    // ASSERT: Initialization should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "Shared memory initialization should succeed");
}

/**
 * Test: Log buffer size calculation should be correct
 * 
 * Tests that the log buffer size is calculated correctly as:
 * Buffer Size = SRAM Bank Size - (config + counters + log_mgmt + overhead)
 */
void test_log_buffer_size_calculation(void) {
    // ARRANGE: Initialize shared memory first
    bool init_result = shared_memory_init();
    TEST_ASSERT_TRUE(init_result);
    
    // ACT: Get calculated buffer size
    uint32_t buffer_size = shared_memory_get_log_buffer_size();
    
    // ASSERT: Buffer size should be reasonable (at least 32KB available for logs)
    TEST_ASSERT_GREATER_THAN_MESSAGE(32 * 1024, buffer_size, 
        "Log buffer should have at least 32KB available");
    
    // ASSERT: Buffer size should not exceed SRAM bank size
    TEST_ASSERT_LESS_THAN_MESSAGE(SRAM_BANK4_SIZE, buffer_size,
        "Log buffer size should be less than total SRAM bank size");
        
}

/**
 * Test: Shared memory layout pointer should be valid
 * 
 * Tests that we can access the shared memory layout structure
 * and it's located at the expected SRAM bank address.
 */
void test_shared_memory_layout_access(void) {
    // ARRANGE: Initialize shared memory
    
    bool init_result = shared_memory_init();
    
    TEST_ASSERT_TRUE(init_result);
    
    // ACT: Get layout pointer
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    // ASSERT: Layout pointer should not be NULL
    TEST_ASSERT_NOT_NULL_MESSAGE(layout, "Shared memory layout pointer should not be NULL");
    
    // ASSERT: Layout should be located in SRAM Bank 4
    uintptr_t layout_addr = (uintptr_t)layout;
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(SRAM_BANK4_BASE, layout_addr,
        "Layout should be at or after SRAM Bank 4 base address");
    TEST_ASSERT_LESS_THAN_MESSAGE(SRAM_BANK4_BASE + SRAM_BANK4_SIZE, layout_addr,
        "Layout should be within SRAM Bank 4 bounds");
    
}

/**
 * Test: Log management structure should be properly initialized
 * 
 * Tests that the log management variables are initialized to correct values.
 */
void test_log_management_initialization(void) {
    
    // ARRANGE: Initialize shared memory
    bool init_result = shared_memory_init();
    TEST_ASSERT_TRUE(init_result);
    
    // ACT: Get layout and examine log management
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    // ASSERT: Write head should start at 0
    TEST_ASSERT_EQUAL_MESSAGE(0, layout->log_mgmt.write_head,
        "Log write_head should initialize to 0");
    
    // ASSERT: Read head should start at 0  
    TEST_ASSERT_EQUAL_MESSAGE(0, layout->log_mgmt.read_head,
        "Log read_head should initialize to 0");
    
    // ASSERT: Buffer size should match calculated size
    uint32_t expected_size = shared_memory_get_log_buffer_size();
    TEST_ASSERT_EQUAL_MESSAGE(expected_size, layout->log_mgmt.buffer_size,
        "Log buffer_size should match calculated size");
    
    // ASSERT: Spinlock should be initialized
    TEST_ASSERT_NOT_NULL_MESSAGE(layout->log_mgmt.reservation_lock,
        "Reservation spinlock should be initialized");
    
}

// Test runner
int main() {
    // Initialize Pico SDK
    stdio_init_all();
    
    // Wait for USB-serial connection
    sleep_ms(2000);
    
    printf("Starting Shared Memory Tests...\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_shared_memory_initialization);
    RUN_TEST(test_log_buffer_size_calculation);
    RUN_TEST(test_shared_memory_layout_access);
    RUN_TEST(test_log_management_initialization);
    
    while (true) {
        printf("Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}
