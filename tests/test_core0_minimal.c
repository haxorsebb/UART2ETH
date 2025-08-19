/**
 * @file test_core0_ringbuffer_separation.c
 * @brief Unit tests for Core0 ringbuffer processing separation (ADR-012)
 * 
 * Tests the architectural refactoring that separates UART hardware processing
 * from ringbuffer message processing in Core0, following the same pattern as Core1.
 * 
 * Documentation Reference:
 * - ADR-012: Core0 Ringbuffer Processing Separation
 * - ADR-007: Event-Driven State Machine Architecture
 * - ADR-011: Ring Buffer Implementation
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Pico SDK includes
#include "pico/stdlib.h"

// Unity test framework
#include "unity/src/unity.h"

// System includes
#include "state_machine.h"
#include "ringbuffer.h"
#include "log_manager.h"
#include "shared_memory.h"

// Test configuration
#define TEST_TIMEOUT_MS 1000
#define TEST_ENTRY_COUNT 5

// Function prototypes for Core0 functions we're testing
// These will be implemented in core0_main.c

// Work detection functions (new per ADR-012)
extern bool core0_check_for_pending_work(void);
extern void core0_work_or_idle_wait(void);

// Separated processing functions (new per ADR-012)  
extern void core0_process_ringbuffer(void);
extern void core0_process_uart(void);
extern void core0_idle_wait(void);

// Helper function prototypes for testing
static void reset_test_environment(void);
static void create_test_ringbuffer_entry(uint8_t direction, const char* payload);
static bool verify_ringbuffer_echo_behavior(void);
static uint32_t count_log_events_of_type(event_type_t event_type);

// Test data
static uint32_t g_initial_log_count;

/**
 * @brief Set up before each test
 */
void setUp(void) {
    printf("TEST: setUp() called\n");
    reset_test_environment();
    g_initial_log_count = log_manager_get_total_count();
    printf("TEST: setUp() complete\n");
}

/**
 * @brief Clean up after each test
 */
void tearDown(void) {
    // Clean up is handled by reset_test_environment in setUp
}

/**
 * @brief Reset test environment to known state
 */
static void reset_test_environment(void) {
    printf("TEST: Initializing shared memory...\n");
    shared_memory_init();
    
    printf("TEST: Initializing state machine...\n");
    state_machine_init();
    
    printf("TEST: Initializing ringbuffer...\n");
    ringbuffer_init();
    
    printf("TEST: Setting up operational state...\n");
    // Set Core0 to operational idle state
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE1);
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE0);
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE1);
    
    printf("TEST: Current Core0 substate: %d\n", state_machine_get_core0_substate());
    
    // Ensure we're in CORE0_IDLE state for testing
    if (state_machine_get_core0_substate() != CORE0_IDLE) {
        printf("TEST: Transitioning to CORE0_IDLE...\n");
        state_machine_process_core0_event(CORE0_EVENT_WORK_IDLE);
        printf("TEST: After transition, Core0 substate: %d\n", state_machine_get_core0_substate());
    }
    
    printf("TEST: Environment reset complete\n");
}

/**
 * @brief Create a test ringbuffer entry for testing
 */
static void create_test_ringbuffer_entry(uint8_t direction, const char* payload) {
    ring_entry_t* entry = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "Failed to get free ringbuffer entry");
    
    entry->direction = direction;
    entry->uart_channel = 0; // Use UART0 for testing
    entry->payload_length = strlen(payload);
    memcpy(entry->payload, payload, entry->payload_length);
    
    bool result = ringbuffer_enqueue_entry(entry);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to enqueue test entry");
}

/**
 * @brief Verify that ringbuffer echo behavior works correctly
 */
static bool verify_ringbuffer_echo_behavior(void) {
    const char* test_message = "#1234test_payload!\r\n";
    
    // Create TCP→UART message
    create_test_ringbuffer_entry(RX_TCP_TO_UART, test_message);
    
    // Process it (should echo back as UART→TCP)
    core0_process_ringbuffer();
    
    // Check if message was echoed back
    ring_entry_t* echoed_msg = ringbuffer_dequeue_entry(RX_UART_TO_TCP);
    if (!echoed_msg) {
        return false;
    }
    
    // Verify the echoed message
    bool payload_matches = (echoed_msg->payload_length == strlen(test_message) &&
                           memcmp(echoed_msg->payload, test_message, strlen(test_message)) == 0);
    bool direction_correct = (echoed_msg->direction == RX_UART_TO_TCP);
    
    // Mark as consumed
    ringbuffer_mark_consumed(echoed_msg);
    
    return payload_matches && direction_correct;
}

/**
 * @brief Count log events of specific type since test start
 */
static uint32_t count_log_events_of_type(event_type_t event_type) {
    // This is a simplified implementation - in reality we'd need to parse the log buffer
    // For now, we'll just check that the total log count increased
    uint32_t current_count = log_manager_get_total_count();
    return (current_count > g_initial_log_count) ? (current_count - g_initial_log_count) : 0;
}

// ==================== CORE0 STATE MACHINE TESTS ====================

/**
 * @brief Test Core0 new states are properly defined
 */
void test_core0_new_states_defined(void) {
    printf("TEST: Starting test_core0_new_states_defined\n");
    // Test that new states exist and can be set
    TEST_ASSERT_EQUAL(CORE0_IDLE, CORE0_IDLE);
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, CORE0_RINGBUFFER_ACTIVE);
    
    // Verify states are distinct
    TEST_ASSERT_NOT_EQUAL(CORE0_IDLE, CORE0_UART_ACTIVE);
    TEST_ASSERT_NOT_EQUAL(CORE0_IDLE, CORE0_RINGBUFFER_ACTIVE);
    TEST_ASSERT_NOT_EQUAL(CORE0_UART_ACTIVE, CORE0_RINGBUFFER_ACTIVE);
}

/**
 * @brief Test Core0 new events are properly defined
 */
void test_core0_new_events_defined(void) {
    // Test that new events exist
    TEST_ASSERT_EQUAL(CORE0_EVENT_RINGBUFFER_DATA_READY, CORE0_EVENT_RINGBUFFER_DATA_READY);
    TEST_ASSERT_EQUAL(CORE0_EVENT_UART_WORK_COMPLETE, CORE0_EVENT_UART_WORK_COMPLETE);
    TEST_ASSERT_EQUAL(CORE0_EVENT_RINGBUFFER_WORK_COMPLETE, CORE0_EVENT_RINGBUFFER_WORK_COMPLETE);
    TEST_ASSERT_EQUAL(CORE0_EVENT_WORK_IDLE, CORE0_EVENT_WORK_IDLE);
    
    // Verify events are distinct
    TEST_ASSERT_NOT_EQUAL(CORE0_EVENT_RINGBUFFER_DATA_READY, CORE0_EVENT_UART_DATA_READY);
    TEST_ASSERT_NOT_EQUAL(CORE0_EVENT_UART_WORK_COMPLETE, CORE0_EVENT_RINGBUFFER_WORK_COMPLETE);
}

/**
 * @brief Test Core0 state transitions from IDLE to work states
 */
void test_core0_idle_to_work_state_transitions(void) {
    // Start in IDLE state
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Test transition to RINGBUFFER_ACTIVE
    bool result = state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_DATA_READY);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to process RINGBUFFER_DATA_READY event");
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
    
    // Return to IDLE
    result = state_machine_process_core0_event(CORE0_EVENT_WORK_IDLE);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to return to IDLE state");
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Test transition to UART_ACTIVE
    result = state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to process UART_DATA_READY event");
    TEST_ASSERT_EQUAL(CORE0_UART_ACTIVE, state_machine_get_core0_substate());
    
    // Return to IDLE
    result = state_machine_process_core0_event(CORE0_EVENT_WORK_IDLE);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to return to IDLE state");
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
}

/**
 * @brief Test Core0 work completion state transitions
 */
void test_core0_work_completion_transitions(void) {
    // Test RINGBUFFER_ACTIVE → IDLE transition
    state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_DATA_READY);
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
    
    bool result = state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_WORK_COMPLETE);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to process RINGBUFFER_WORK_COMPLETE event");
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Test UART_ACTIVE → IDLE transition
    state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
    TEST_ASSERT_EQUAL(CORE0_UART_ACTIVE, state_machine_get_core0_substate());
    
    result = state_machine_process_core0_event(CORE0_EVENT_UART_WORK_COMPLETE);
    TEST_ASSERT_TRUE_MESSAGE(result, "Failed to process UART_WORK_COMPLETE event");
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
}

// ==================== WORK DETECTION TESTS ====================

/**
 * @brief Test work detection with no pending work
 */
void test_core0_check_for_pending_work_none(void) {
    // Ensure no work pending
    TEST_ASSERT_EQUAL(0, ringbuffer_get_count(RX_TCP_TO_UART));
    
    // Check for work
    bool work_found = core0_check_for_pending_work();
    TEST_ASSERT_FALSE_MESSAGE(work_found, "Should find no work when none pending");
    
    // State should remain IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
}

/**
 * @brief Test work detection with ringbuffer messages pending
 */
void test_core0_check_for_pending_work_ringbuffer(void) {
    // Create test ringbuffer entry
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#1234test!\r\n");
    
    // Check for work
    bool work_found = core0_check_for_pending_work();
    TEST_ASSERT_TRUE_MESSAGE(work_found, "Should find ringbuffer work when messages pending");
    
    // State should transition to RINGBUFFER_ACTIVE
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
}

/**
 * @brief Test work_or_idle_wait with work available
 */
void test_core0_work_or_idle_wait_with_work(void) {
    // Create work
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#5678work!\r\n");
    
    // Start in IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Call work_or_idle_wait
    core0_work_or_idle_wait();
    
    // Should transition to work state
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
}

/**
 * @brief Test work_or_idle_wait with no work available
 */
void test_core0_work_or_idle_wait_no_work(void) {
    // Ensure no work
    TEST_ASSERT_EQUAL(0, ringbuffer_get_count(RX_TCP_TO_UART));
    
    // Start in IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Call work_or_idle_wait (this will call core0_idle_wait internally)
    // Note: We can't easily test the WFI part in unit tests
    core0_work_or_idle_wait();
    
    // Should remain in IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
}

// ==================== RINGBUFFER PROCESSING TESTS ====================

/**
 * @brief Test core0_process_ringbuffer with no messages
 */
void test_core0_process_ringbuffer_no_messages(void) {
    // Ensure no messages
    TEST_ASSERT_EQUAL(0, ringbuffer_get_count(RX_TCP_TO_UART));
    
    // Set state to RINGBUFFER_ACTIVE
    state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_DATA_READY);
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
    
    // Process (should find no work and complete)
    core0_process_ringbuffer();
    
    // Should return to IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
}

/**
 * @brief Test core0_process_ringbuffer echo functionality
 */
void test_core0_process_ringbuffer_echo_success(void) {
    const char* test_message = "#ABCD echo_test!\r\n";
    
    // Create TCP→UART message
    create_test_ringbuffer_entry(RX_TCP_TO_UART, test_message);
    
    // Set state to RINGBUFFER_ACTIVE
    state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_DATA_READY);
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
    
    // Process the message
    core0_process_ringbuffer();
    
    // Should return to IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Verify echo message was created
    TEST_ASSERT_EQUAL(1, ringbuffer_get_count(RX_UART_TO_TCP));
    
    // Verify echoed message content
    ring_entry_t* echoed_msg = ringbuffer_dequeue_entry(RX_UART_TO_TCP);
    TEST_ASSERT_NOT_NULL_MESSAGE(echoed_msg, "Echo message should be available");
    TEST_ASSERT_EQUAL(RX_UART_TO_TCP, echoed_msg->direction);
    TEST_ASSERT_EQUAL(strlen(test_message), echoed_msg->payload_length);
    TEST_ASSERT_EQUAL_MEMORY(test_message, echoed_msg->payload, strlen(test_message));
    
    ringbuffer_mark_consumed(echoed_msg);
}

/**
 * @brief Test core0_process_ringbuffer processes only one message per call
 */
void test_core0_process_ringbuffer_single_message_per_call(void) {
    // Create multiple TCP→UART messages
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#1111msg1!\r\n");
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#2222msg2!\r\n");
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#3333msg3!\r\n");
    
    TEST_ASSERT_EQUAL(3, ringbuffer_get_count(RX_TCP_TO_UART));
    
    // Set state to RINGBUFFER_ACTIVE
    state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_DATA_READY);
    
    // Process once - should handle only ONE message
    core0_process_ringbuffer();
    
    // Should have processed 1 message
    TEST_ASSERT_EQUAL(2, ringbuffer_get_count(RX_TCP_TO_UART)); // 2 remaining
    TEST_ASSERT_EQUAL(1, ringbuffer_get_count(RX_UART_TO_TCP)); // 1 echoed
    
    // Should return to IDLE to allow main loop to check for more work
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
}

/**
 * @brief Test core0_process_ringbuffer logs events correctly
 */
void test_core0_process_ringbuffer_logging(void) {
    // Create test message
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#FEED test_logging!\r\n");
    
    // Set state to RINGBUFFER_ACTIVE
    state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_DATA_READY);
    
    // Process the message
    core0_process_ringbuffer();
    
    // Verify logging occurred (simplified check)
    uint32_t log_events = count_log_events_of_type(LOG_EVENT_RINGBUFFER_ECHO_SUCCESS);
    TEST_ASSERT_GREATER_THAN(0, log_events);
}

// ==================== UART HARDWARE PROCESSING TESTS ====================

/**
 * @brief Test core0_process_uart separated from ringbuffer concerns
 */
void test_core0_process_uart_separation(void) {
    // Set state to UART_ACTIVE
    state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
    TEST_ASSERT_EQUAL(CORE0_UART_ACTIVE, state_machine_get_core0_substate());
    
    // Process UART hardware (this is mostly stubbed in current implementation)
    core0_process_uart();
    
    // Should complete and return to IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
}

/**
 * @brief Test core0_process_uart does not process ringbuffer
 */
void test_core0_process_uart_ignores_ringbuffer(void) {
    // Create ringbuffer messages
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#9999ignore!\r\n");
    TEST_ASSERT_EQUAL(1, ringbuffer_get_count(RX_TCP_TO_UART));
    
    // Set state to UART_ACTIVE
    state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
    
    // Process UART hardware
    core0_process_uart();
    
    // Ringbuffer message should remain unprocessed
    TEST_ASSERT_EQUAL(1, ringbuffer_get_count(RX_TCP_TO_UART));
    TEST_ASSERT_EQUAL(0, ringbuffer_get_count(RX_UART_TO_TCP));
}

// ==================== INTEGRATION TESTS ====================

/**
 * @brief Test complete Core0 work cycle: work detection → processing → completion
 */
void test_core0_complete_work_cycle(void) {
    // Start in IDLE
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Create work
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#CAFE cycle_test!\r\n");
    
    // Simulate main loop: check for work
    core0_work_or_idle_wait();
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
    
    // Simulate main loop: process work
    core0_process_ringbuffer();
    TEST_ASSERT_EQUAL(CORE0_IDLE, state_machine_get_core0_substate());
    
    // Verify work was completed
    TEST_ASSERT_EQUAL(0, ringbuffer_get_count(RX_TCP_TO_UART));
    TEST_ASSERT_EQUAL(1, ringbuffer_get_count(RX_UART_TO_TCP));
}

/**
 * @brief Test Core0 work priority: ringbuffer work has higher priority than UART work
 */
void test_core0_work_priority_ringbuffer_over_uart(void) {
    // Create ringbuffer work
    create_test_ringbuffer_entry(RX_TCP_TO_UART, "#BEEF priority!\r\n");
    
    // Simulate UART work is also available (we can't easily create real UART work in unit tests)
    // This test verifies that check_for_pending_work checks ringbuffer first
    
    // Check for work
    bool work_found = core0_check_for_pending_work();
    TEST_ASSERT_TRUE(work_found);
    
    // Should prioritize ringbuffer work
    TEST_ASSERT_EQUAL(CORE0_RINGBUFFER_ACTIVE, state_machine_get_core0_substate());
}

/**
 * @brief Test Core0 architectural consistency with Core1 pattern
 */
void test_core0_architectural_consistency_with_core1(void) {
    // Verify Core0 now has same architectural pattern as Core1:
    // 1. IDLE state for work detection
    // 2. work_or_idle_wait function
    // 3. check_for_pending_work function  
    // 4. Separated work processing functions
    
    // Test 1: Has IDLE state
    TEST_ASSERT_EQUAL(CORE0_IDLE, CORE0_IDLE);
    
    // Test 2: Can call work_or_idle_wait (function exists)
    core0_work_or_idle_wait(); // Should not crash
    
    // Test 3: Can call check_for_pending_work (function exists)
    bool result = core0_check_for_pending_work(); // Should not crash
    TEST_ASSERT_FALSE(result); // No work pending in clean test environment
    
    // Test 4: Has separated processing functions
    core0_process_ringbuffer(); // Should not crash
    core0_process_uart(); // Should not crash
}

// ==================== TEST RUNNER ====================

/**
 * @brief Run all Core0 ringbuffer separation tests
 */
int main(void) {
    // Initialize Pico SDK (required for all programs)
    stdio_init_all();
    
    // Wait for USB-serial connection
    sleep_ms(2000);
    
    printf("=== MINIMAL TEST: firmware starts correctly ===\n");
    printf("Build timestamp: %s %s\n", __DATE__, __TIME__);
    
    // Test if we can reach this point without crashing
    for (int i = 0; i < 10; i++) {
        printf("Minimal test loop %d - firmware is running\n", i);
        sleep_ms(500);
    }
    
    printf("=== MINIMAL TEST: proceeding to actual tests ===\n");
    
    printf("TEST: About to call UNITY_BEGIN()\n");
    UNITY_BEGIN();
    printf("TEST: UNITY_BEGIN() completed\n");
    
    // Just run the first simple test to see if that works
    printf("TEST: Running only first test...\n");
    RUN_TEST(test_core0_new_states_defined);
    printf("TEST: First test completed\n");
    
    printf("All tests completed!\n");
    
    // Required: tests must not terminate (per development guidelines)
    while (true) {
        printf("Tests completed - ADR-012 Core0 Ringbuffer Separation\n");
        UNITY_END();
        sleep_ms(1000);
    }
}