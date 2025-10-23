/**
 * @file test_state_machine.c
 * @brief Unit tests for event-driven state machine implementation
 * 
 * Tests the three independent event-driven state machines including atomic main state,
 * ISR-safe sub-states, and event processing with condition validation
 * according to ADR-007 and arc42 Chapter 5.
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Global State Machine Whitebox
 */

#include "unity.h"
#include "state_machine.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdatomic.h>

// Access to initialization flag for proper test reinitialization
extern _Atomic bool g_initialized;

void setUp(void) {
    // Called before each test
    // Reset initialization flag to ensure fresh state machine initialization
    atomic_store(&g_initialized, false);
    
    // Reinitialize state machine for clean test environment
    state_machine_init();
}

void tearDown(void) {
    // Called after each test
}

/**
 * Test: State machine should initialize successfully
 * 
 * This tests the most atomic condition: that our state machine
 * can be initialized and basic functionality is accessible.
 */
void test_state_machine_initialization(void) {
    // ARRANGE: Nothing needed
    
    // ACT: Initialize state machine
    bool result = state_machine_init();
    
    // ASSERT: Initialization should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "State machine initialization should succeed");
}

/**
 * Test: State machine should start in correct initial states
 * 
 * After initialization, all three state machines should be in their initial states.
 */
void test_state_machine_initial_states(void) {
    // ARRANGE: Initialize state machine
    bool init_result = state_machine_init();
    TEST_ASSERT_TRUE(init_result);
    
    // ACT: Get initial states
    main_state_t main_state = state_machine_get_main_state();
    core0_substate_t core0_state = state_machine_get_core0_substate();
    core1_substate_t core1_state = state_machine_get_core1_substate();
    
    // ASSERT: Main state should be INIT
    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_INIT, main_state, 
        "Main state should initialize to INIT");
    
    // ASSERT: Core0 sub-state should be INIT_UART
    TEST_ASSERT_EQUAL_MESSAGE(CORE0_INIT_UART, core0_state,
        "Core0 sub-state should initialize to INIT_UART");
    
    // ASSERT: Core1 sub-state should be INIT_PERISTENCE
    TEST_ASSERT_EQUAL_MESSAGE(CORE1_INIT_PERISTENCE, core1_state,
        "Core1 sub-state should initialize to INIT_PERISTENCE");
}

/**
 * Test: Main state machine should process INIT_COMPLETE event
 * 
 * When system initialization is complete, MAIN_EVENT_INIT_COMPLETE
 * should transition from INIT to CONFIGURATION state.
 */
void test_main_state_init_complete_event(void) {
    // ARRANGE: Initialize state machine (should start in INIT state)
    state_machine_init();
    TEST_ASSERT_EQUAL(MAIN_STATE_INIT, state_machine_get_main_state());
    
    // ACT: Process INIT_COMPLETE event
    bool result = state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    
    // ASSERT: Event processing should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "INIT_COMPLETE event processing should succeed");
    
    // ASSERT: Main state should transition to CONFIGURATION
    main_state_t new_state = state_machine_get_main_state();
    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_CONFIGURATION, new_state,
        "Main state should transition from INIT to CONFIGURATION on INIT_COMPLETE event");
}

/**
 * Test: Main state machine should process CONFIG_LOADED event
 * 
 * When configuration is loaded, MAIN_EVENT_CONFIG_LOADED should
 * transition from CONFIGURATION to OPERATIONAL state.
 */
void test_main_state_config_loaded_event(void) {
    // ARRANGE: Get to CONFIGURATION state first
    state_machine_init();
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    TEST_ASSERT_EQUAL(MAIN_STATE_CONFIGURATION, state_machine_get_main_state());
    
    // ACT: Process CONFIG_LOADED event
    bool result = state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE0);
    
    // ASSERT: Event processing should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "CONFIG_LOADED event processing should succeed");
    
    // ASSERT: Main state should transition to OPERATIONAL
    main_state_t new_state = state_machine_get_main_state();
    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_OPERATIONAL, new_state,
        "Main state should transition from CONFIGURATION to OPERATIONAL on CONFIG_LOADED event");
}

/**
 * Test: Main state machine should process SYSTEM_ERROR event from any state
 * 
 * MAIN_EVENT_SYSTEM_ERROR should transition to ERROR state from any current state.
 */
void test_main_state_system_error_event(void) {
    // ARRANGE: Get to OPERATIONAL state
    state_machine_init();
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE0);
    TEST_ASSERT_EQUAL(MAIN_STATE_OPERATIONAL, state_machine_get_main_state());
    
    // ACT: Process SYSTEM_ERROR event
    bool result = state_machine_process_main_event(MAIN_EVENT_SYSTEM_ERROR);
    
    // ASSERT: Event processing should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "SYSTEM_ERROR event processing should succeed");
    
    // ASSERT: Main state should transition to ERROR
    main_state_t new_state = state_machine_get_main_state();
    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_ERROR, new_state,
        "Main state should transition to ERROR on SYSTEM_ERROR event");
}

/**
 * Test: Core0 state machine should process UART_DATA_READY event
 * 
 * When UART data is available, CORE0_EVENT_UART_DATA_READY should
 * transition from UART_IDLE to UART_ACTIVE.
 */
void test_core0_state_uart_data_ready_event(void) {
    // ARRANGE: Initialize and ensure Core0 is in UART_IDLE
    state_machine_init();
    TEST_ASSERT_EQUAL(CORE0_INIT_UART, state_machine_get_core0_substate());
    
    // ACT: Process UART_DATA_READY event
    bool result = state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
    
    // ASSERT: Event processing should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "UART_DATA_READY event processing should succeed");
    
    // ASSERT: Core0 sub-state should transition to UART_ACTIVE
    core0_substate_t new_state = state_machine_get_core0_substate();
    TEST_ASSERT_EQUAL_MESSAGE(CORE0_UART_ACTIVE, new_state,
        "Core0 sub-state should transition from UART_IDLE to UART_ACTIVE on UART_DATA_READY event");
}

/**
 * Test: Core0 state machine should process UART_IDLE event
 * 
 * When UART processing is complete, CORE0_EVENT_WORK_IDLE should
 * transition from UART_ACTIVE back to UART_IDLE.
 */
void test_core0_state_uart_idle_event(void) {
    // ARRANGE: Get Core0 to UART_ACTIVE state first
    state_machine_init();
    state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
    TEST_ASSERT_EQUAL(CORE0_UART_ACTIVE, state_machine_get_core0_substate());
    
    // ACT: Process UART_IDLE event
    bool result = state_machine_process_core0_event(CORE0_EVENT_WORK_IDLE);
    
    // ASSERT: Event processing should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "UART_IDLE event processing should succeed");
    
    // ASSERT: Core0 sub-state should transition back to UART_IDLE
    core0_substate_t new_state = state_machine_get_core0_substate();
    TEST_ASSERT_EQUAL_MESSAGE(CORE0_INIT_UART, new_state,
        "Core0 sub-state should transition from UART_ACTIVE to UART_IDLE on UART_IDLE event");
}

/**
 * Test: Core1 state machine should process NETWORK_UP event
 * 
 * When network interface comes up, CORE1_EVENT_NETWORK_UP should
 * transition from NET_DISCONNECTED to NET_IDLE.
 */
void test_core1_state_network_up_event(void) {
    // ARRANGE: Initialize and ensure Core1 is in NET_DISCONNECTED
    state_machine_init();
    TEST_ASSERT_EQUAL(CORE1_INIT_PERISTENCE, state_machine_get_core1_substate());
    
    // ACT: Process NETWORK_UP event
    bool result = state_machine_process_core1_event(CORE1_EVENT_NETWORK_UP);
    
    // ASSERT: Event processing should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "NETWORK_UP event processing should succeed");
    
    // ASSERT: Core1 sub-state should transition to NET_IDLE
    core1_substate_t new_state = state_machine_get_core1_substate();
    TEST_ASSERT_EQUAL_MESSAGE(CORE1_NET_IDLE, new_state,
        "Core1 sub-state should transition from NET_DISCONNECTED to NET_IDLE on NETWORK_UP event");
}

/**
 * Test: Core1 state machine should process PERSISTENCE_START event
 * 
 * When persistence operation starts, CORE1_EVENT_PERSISTENCE_START should
 * transition to PERSISTENCE_ACTIVE (from any other non-persistence state).
 */
void test_core1_state_persistence_start_event(void) {
    // ARRANGE: Get Core1 to NET_IDLE state first
    state_machine_init();
    state_machine_process_core1_event(CORE1_EVENT_NETWORK_UP);
    TEST_ASSERT_EQUAL(CORE1_NET_IDLE, state_machine_get_core1_substate());
    
    // ACT: Process PERSISTENCE_START event
    bool result = state_machine_process_core1_event(CORE1_EVENT_PERSISTENCE_START);
    
    // ASSERT: Event processing should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "PERSISTENCE_START event processing should succeed");
    
    // ASSERT: Core1 sub-state should transition to PERSISTENCE_ACTIVE
    core1_substate_t new_state = state_machine_get_core1_substate();
    TEST_ASSERT_EQUAL_MESSAGE(CORE1_PERSISTENCE_ACTIVE, new_state,
        "Core1 sub-state should transition to PERSISTENCE_ACTIVE on PERSISTENCE_START event");
}

/**
 * Test: Invalid event should not change state
 * 
 * Events that don't make sense for current state should be ignored
 * and not change the state.
 */
void test_invalid_event_ignored(void) {
    // ARRANGE: Initialize state machine (INIT state)
    state_machine_init();
    main_state_t initial_state = state_machine_get_main_state();
    TEST_ASSERT_EQUAL(MAIN_STATE_INIT, initial_state);
    
    // ACT: Send invalid event (CONFIG_LOADED while in INIT state - should be ignored)
    bool result = state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE0);
    
    // ASSERT: Event processing should still succeed (just ignored)
    TEST_ASSERT_TRUE_MESSAGE(result, "Invalid event processing should succeed but be ignored");
    
    // ASSERT: Main state should remain unchanged
    main_state_t final_state = state_machine_get_main_state();
    TEST_ASSERT_EQUAL_MESSAGE(initial_state, final_state,
        "State should not change when invalid event is processed");
}

/**
 * Test: State query functions should be non-blocking
 * 
 * State query functions should return immediately without blocking.
 */
void test_state_queries_non_blocking(void) {
    // ARRANGE: Initialize state machine
    state_machine_init();
    
    // ACT & ASSERT: All query functions should return valid states immediately
    main_state_t main_state = state_machine_get_main_state();
    core0_substate_t core0_state = state_machine_get_core0_substate();
    core1_substate_t core1_state = state_machine_get_core1_substate();
    
    // These should be valid enum values
    TEST_ASSERT_TRUE(main_state >= MAIN_STATE_INIT && main_state <= MAIN_STATE_ERROR);
    TEST_ASSERT_TRUE(core0_state >= CORE0_INIT_UART && core0_state <= CORE0_UART_ERROR);
    TEST_ASSERT_TRUE(core1_state >= CORE1_INIT_PERISTENCE && core1_state <= CORE1_SHUTDOWN);
}

// Test runner
int main() {
    // Initialize Pico SDK
    stdio_init_all();
    
    // Wait for USB-serial connection
    sleep_ms(2000);
    
    printf("Starting Event-Driven State Machine Tests...\n");
    
    UNITY_BEGIN();
    
    // Basic functionality tests
    RUN_TEST(test_state_machine_initialization);
    RUN_TEST(test_state_machine_initial_states);
    
    // Main state machine event processing tests
    RUN_TEST(test_main_state_init_complete_event);
    RUN_TEST(test_main_state_config_loaded_event);
    RUN_TEST(test_main_state_system_error_event);
    
    // Core0 state machine event processing tests
    RUN_TEST(test_core0_state_uart_data_ready_event);
    RUN_TEST(test_core0_state_uart_idle_event);
    
    // Core1 state machine event processing tests
    RUN_TEST(test_core1_state_network_up_event);
    RUN_TEST(test_core1_state_persistence_start_event);
    
    // Edge case and validation tests
    RUN_TEST(test_invalid_event_ignored);
    RUN_TEST(test_state_queries_non_blocking);
    
    while (true) {
        printf("Event-Driven State Machine Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}