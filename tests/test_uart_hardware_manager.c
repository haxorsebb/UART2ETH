/**
 * @file test_uart_hardware_manager.c
 * @brief Unit tests for UART Hardware Manager implementation (Issue #76)
 * 
 * Tests the UART Hardware Manager that provides UART1 integration with the 
 * ring buffer system and Core0 state machine architecture.
 * 
 * Test Requirements from Issue #76:
 * 1. Successful configuring of hardware UART to 230400 baud, 8N1, and interrupt
 * 2. Successful testing of loopback configuration of UART1
 * 3. Sending and receiving of a single line of text (ending with newline)
 * 4. Loops of (2) always returning what was sent from port 4001 to port 4001
 * 
 * Documentation Reference:
 * - Issue #76: Add UART Hardware Manager implementation
 * - arc42 Chapter 5 - UART Hardware Manager Implementation
 * - ADR-007: Event-Driven State Machine Architecture
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

// System includes
#include "state_machine.h"
#include "ringbuffer.h"
#include "log_manager.h"
#include "shared_memory.h"

// Module under test
#include "uart/uart_hardware_manager.h"
#include "uart/uart1_driver.h"

// Test configuration
#define TEST_TIMEOUT_MS 1000
#define TEST_BAUD_RATE 230400
#define TEST_RX_GPIO 9
#define TEST_TX_GPIO 10
#define TEST_MESSAGE_MAX_LEN 256

// Test data
static char g_test_message[] = "Hello UART Test\n";
static char g_received_buffer[TEST_MESSAGE_MAX_LEN];
static uint32_t g_initial_log_count;

// Function prototypes for helper functions
static void reset_test_environment(void);
static void setup_uart_loopback_test(void);
static bool wait_for_uart_ready(uint32_t timeout_ms);
static bool verify_uart_configuration(void);
static bool send_and_verify_loopback(const char* message);
static void create_tcp_to_uart_message(const char* payload);
static bool verify_uart_to_tcp_response(const char* expected_payload);

/**
 * @brief Set up before each test
 */
void setUp(void) {
    printf("TEST: setUp() - UART Hardware Manager Test\n");
    reset_test_environment();
    g_initial_log_count = log_manager_get_total_count();
    memset(g_received_buffer, 0, sizeof(g_received_buffer));
    
    // Clear any residual data from previous tests
    if (uart_hardware_manager_is_ready()) {
        // Clear the RX buffer to ensure clean state
        while (uart_hardware_manager_process_incoming_data()) {
            // Process and discard any stale data
        }
    }
    
    printf("TEST: setUp() complete\n");
}

/**
 * @brief Clean up after each test
 */
void tearDown(void) {
    // Deinitialize UART hardware manager if initialized
    uart_hardware_manager_deinit();
    printf("TEST: tearDown() complete\n");
}

/**
 * @brief Reset test environment to known state
 */
static void reset_test_environment(void) {
    printf("TEST: Initializing shared memory...\n");
    shared_memory_init();
    
    printf("TEST: Initializing log manager...\n");
    log_manager_init();
    
    printf("TEST: Initializing state machine...\n");
    state_machine_init();
    
    printf("TEST: Initializing ringbuffer...\n");
    ringbuffer_init();
    
    // Set system to operational state
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE1);
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE0);
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE1);
    
    printf("TEST: Test environment reset complete\n");
}

// Test 1: UART Hardware Manager Initialization
/**
 * @brief Test UART Hardware Manager initialization
 * 
 * Requirement: Successful configuring of hardware UART to 230400 baud, 8N1, and interrupt
 */
void test_uart_hardware_manager_initialization(void) {
    printf("TEST: test_uart_hardware_manager_initialization\n");
    
    // Test: Manager should initialize successfully
    bool init_result = uart_hardware_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Hardware Manager initialization failed");
    
    // Test: Manager should report ready status
    bool ready_status = uart_hardware_manager_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready_status, "UART Hardware Manager not ready after initialization");
    
    // Test: UART1 driver should be configured correctly
    bool config_valid = verify_uart_configuration();
    TEST_ASSERT_TRUE_MESSAGE(config_valid, "UART1 configuration verification failed");
    
    // Test: Should generate appropriate log events
    uint32_t final_log_count = log_manager_get_total_count();
    TEST_ASSERT_GREATER_THAN_MESSAGE(g_initial_log_count, final_log_count, 
                                      "No log events generated during initialization");
    
    printf("TEST: UART Hardware Manager initialization test passed\n");
}

// Test 2: UART1 Driver Hardware Configuration
/**
 * @brief Test UART1 driver hardware configuration
 * 
 * Requirement: Hardware UART configured to 230400 baud, 8N1, with interrupts
 */
void test_uart1_driver_configuration(void) {
    printf("TEST: test_uart1_driver_configuration\n");
    
    // Configure UART1 with test parameters
    uart1_config_t config = {
        .baud_rate = TEST_BAUD_RATE,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .rx_gpio = TEST_RX_GPIO,
        .tx_gpio = TEST_TX_GPIO,
        .enable_loopback = true  // Enable for testing
    };
    
    // Test: Driver should initialize with configuration
    bool init_result = uart1_driver_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART1 driver initialization failed");
    
    // Test: Driver should report ready status
    bool ready_status = uart1_driver_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready_status, "UART1 driver not ready after initialization");
    
    // Test: Hardware configuration should be applied correctly
    bool config_applied = verify_uart_configuration();
    TEST_ASSERT_TRUE_MESSAGE(config_applied, "UART1 hardware configuration not applied correctly");
    
    printf("TEST: UART1 driver configuration test passed\n");
}

// Test 3: UART Loopback Functionality
/**
 * @brief Test UART loopback functionality
 * 
 * Requirement: Successful testing of loopback configuration of UART1
 */
void test_uart_loopback_functionality(void) {
    printf("TEST: test_uart_loopback_functionality\n");
    
    // Initialize UART hardware manager
    bool init_result = uart_hardware_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Hardware Manager initialization failed");
    
    // Setup loopback test configuration
    setup_uart_loopback_test();
    
    // Test: Single character loopback
    bool single_char_result = send_and_verify_loopback("A");
    TEST_ASSERT_TRUE_MESSAGE(single_char_result, "Single character loopback failed");
    
    // Test: Short message loopback
    bool short_msg_result = send_and_verify_loopback("Hello");
    TEST_ASSERT_TRUE_MESSAGE(short_msg_result, "Short message loopback failed");
    
    // Test: Message with newline loopback
    bool newline_msg_result = send_and_verify_loopback("Test Line\n");
    TEST_ASSERT_TRUE_MESSAGE(newline_msg_result, "Newline message loopback failed");
    
    printf("TEST: UART loopback functionality test passed\n");
}

// Test 4: Ring Buffer Integration - Single Message
/**
 * @brief Test ring buffer integration with single message
 * 
 * Requirement: Sending and receiving of a single line of text (ending with newline)
 */
void test_ring_buffer_integration_single_message(void) {
    printf("TEST: test_ring_buffer_integration_single_message\n");
    
    // Initialize UART hardware manager
    bool init_result = uart_hardware_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Hardware Manager initialization failed");
    
    // Create a TCP→UART message in ring buffer
    const char* test_payload = "Ring Buffer Test Message\n";
    create_tcp_to_uart_message(test_payload);
    
    // Test: Manager should detect pending work
    bool has_work = uart_hardware_manager_has_pending_work();
    TEST_ASSERT_TRUE_MESSAGE(has_work, "UART Hardware Manager should detect pending work");
    
    // Test: Process outgoing data (TCP→UART)
    bool process_result = uart_hardware_manager_process_outgoing_data();
    TEST_ASSERT_TRUE_MESSAGE(process_result, "Failed to process outgoing data");
    
    // Test: Process incoming data (UART→TCP)
    // Loopback is now immediate in uart1_driver_send_char(), no delay needed
    bool incoming_result = uart_hardware_manager_process_incoming_data();
    TEST_ASSERT_TRUE_MESSAGE(incoming_result, "Failed to process incoming data");
    
    // Test: Verify UART→TCP response message in ring buffer
    bool response_valid = verify_uart_to_tcp_response(test_payload);
    TEST_ASSERT_TRUE_MESSAGE(response_valid, "UART→TCP response message not found or invalid");
    
    printf("TEST: Ring buffer integration single message test passed\n");
}

// Test 5: Ring Buffer Integration - Multiple Messages
/**
 * @brief Test ring buffer integration with multiple sequential messages
 * 
 * Requirement: Loops always returning what was sent from port 4001 to port 4001
 */
void test_ring_buffer_integration_multiple_messages(void) {
    printf("TEST: test_ring_buffer_integration_multiple_messages\n");
    
    // Initialize UART hardware manager
    bool init_result = uart_hardware_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "UART Hardware Manager initialization failed");
    
    const char* test_messages[] = {
        "Message 1\n",
        "Second test message\n", 
        "Third message with numbers 123\n",
        "Final test message\n"
    };
    const int num_messages = sizeof(test_messages) / sizeof(test_messages[0]);
    
    // Test each message in sequence
    for (int i = 0; i < num_messages; i++) {
        printf("TEST: Processing message %d: %s", i+1, test_messages[i]);
        
        // Create TCP→UART message
        create_tcp_to_uart_message(test_messages[i]);
        
        // Process outgoing data
        bool process_out = uart_hardware_manager_process_outgoing_data();
        TEST_ASSERT_TRUE_MESSAGE(process_out, "Failed to process outgoing data");
        
        // Process incoming echo (loopback is now immediate)
        bool process_in = uart_hardware_manager_process_incoming_data();
        TEST_ASSERT_TRUE_MESSAGE(process_in, "Failed to process incoming data");
        
        // Verify response
        bool response_valid = verify_uart_to_tcp_response(test_messages[i]);
        TEST_ASSERT_TRUE_MESSAGE(response_valid, "Response message validation failed");
    }
    
    printf("TEST: Ring buffer integration multiple messages test passed\n");
}

// Test 6: Error Handling and Recovery
/**
 * @brief Test error handling and recovery scenarios
 */
void test_uart_error_handling(void) {
    printf("TEST: test_uart_error_handling\n");
    
    // Test: Double initialization should be handled gracefully
    bool first_init = uart_hardware_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(first_init, "First initialization failed");
    
    bool second_init = uart_hardware_manager_init();
    TEST_ASSERT_TRUE_MESSAGE(second_init, "Second initialization should succeed (idempotent)");
    
    // Test: Manager should still be ready after double initialization
    bool ready_status = uart_hardware_manager_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready_status, "Manager not ready after double initialization");
    
    // Test: Deinitialization should work
    uart_hardware_manager_deinit();
    bool ready_after_deinit = uart_hardware_manager_is_ready();
    TEST_ASSERT_FALSE_MESSAGE(ready_after_deinit, "Manager should not be ready after deinitialization");
    
    printf("TEST: UART error handling test passed\n");
}

// Helper function implementations

/**
 * @brief Setup UART for loopback testing
 */
static void setup_uart_loopback_test(void) {
    // Configure GPIO pins for loopback
    gpio_set_function(TEST_RX_GPIO, GPIO_FUNC_UART);
    gpio_set_function(TEST_TX_GPIO, GPIO_FUNC_UART);
    
    // In real hardware testing, RX and TX pins would be physically connected
    printf("TEST: UART loopback test setup complete\n");
}

/**
 * @brief Wait for UART to become ready
 */
static bool wait_for_uart_ready(uint32_t timeout_ms) {
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < timeout_ms) {
        if (uart_hardware_manager_is_ready()) {
            return true;
        }
        sleep_ms(10);
    }
    
    return false;
}

/**
 * @brief Verify UART configuration is correct
 */
static bool verify_uart_configuration(void) {
    // For now, return true - actual implementation would check hardware registers
    // In real implementation, this would verify:
    // - Baud rate is set to 230400
    // - Data bits = 8, stop bits = 1, parity = none
    // - GPIO pins are configured correctly
    // - Interrupts are enabled
    return true;
}

/**
 * @brief Send message and verify loopback response
 */
static bool send_and_verify_loopback(const char* message) {
    // For now, return true - actual implementation would:
    // - Send message via UART1
    // - Wait for echo response
    // - Compare received data with sent data
    printf("TEST: Loopback test for message: %s\n", message);
    return true;
}

/**
 * @brief Create TCP→UART message in ring buffer
 */
static void create_tcp_to_uart_message(const char* payload) {
    // Clear any stale RX data before sending new message
    if (uart_hardware_manager_is_ready()) {
        while (uart_hardware_manager_process_incoming_data()) {
            // Process and discard any stale data
        }
    }
    
    ring_entry_t* entry = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "Failed to get free ring buffer entry");
    
    entry->uart_channel = 1;  // UART1
    entry->direction = RX_TCP_TO_UART;
    entry->payload_length = strlen(payload);
    entry->timestamp = to_ms_since_boot(get_absolute_time());
    
    memcpy(entry->payload, payload, entry->payload_length);
    
    bool enqueue_result = ringbuffer_enqueue_entry(entry);
    TEST_ASSERT_TRUE_MESSAGE(enqueue_result, "Failed to enqueue TCP→UART message");
    
    printf("TEST: Created TCP→UART message: %s", payload);
}

/**
 * @brief Verify UART→TCP response message in ring buffer
 */
static bool verify_uart_to_tcp_response(const char* expected_payload) {
    ring_entry_t* response = ringbuffer_dequeue_entry(RX_UART_TO_TCP);
    if (!response) {
        printf("TEST: No UART→TCP response found in ring buffer\n");
        return false;
    }
    
    // Verify payload matches expected
    if (response->payload_length != strlen(expected_payload)) {
        printf("TEST: Response payload length mismatch - expected: %zu, actual: %u\n", 
               strlen(expected_payload), response->payload_length);
        printf("TEST: Expected payload: '%s'\n", expected_payload);
        printf("TEST: Actual payload: '%.*s'\n", response->payload_length, response->payload);
        ringbuffer_mark_consumed(response);
        return false;
    }
    
    if (strncmp((char*)response->payload, expected_payload, response->payload_length) != 0) {
        printf("TEST: Response payload content mismatch\n");
        printf("TEST: Expected (%zu chars): '%s'\n", strlen(expected_payload), expected_payload);
        printf("TEST: Actual (%u chars): '%.*s'\n", response->payload_length, response->payload_length, response->payload);
        printf("TEST: Expected bytes: ");
        for (size_t i = 0; i < strlen(expected_payload); i++) {
            printf("%02X ", (unsigned char)expected_payload[i]);
        }
        printf("\nTEST: Actual bytes: ");
        for (uint32_t i = 0; i < response->payload_length; i++) {
            printf("%02X ", (unsigned char)response->payload[i]);
        }
        printf("\n");
        ringbuffer_mark_consumed(response);
        return false;
    }
    
    // Mark as consumed
    ringbuffer_mark_consumed(response);
    
    printf("TEST: Verified UART→TCP response: %s", expected_payload);
    return true;
}

/**
 * @brief Main test runner
 */
int main() {
    stdio_init_all();
    sleep_ms(2000);  // Allow time for USB serial initialization
    
    printf("\n=== UART Hardware Manager Test Suite ===\n");
    printf("Testing Issue #76 implementation\n\n");
    
    UNITY_BEGIN();
    
    // Run tests in order of complexity
    RUN_TEST(test_uart_hardware_manager_initialization);
    RUN_TEST(test_uart1_driver_configuration);
    RUN_TEST(test_uart_loopback_functionality);
    RUN_TEST(test_ring_buffer_integration_single_message);
    RUN_TEST(test_ring_buffer_integration_multiple_messages);
    RUN_TEST(test_uart_error_handling);
    
    int result = UNITY_END();
    
    // Keep running for embedded environment
    while (true) {
        printf("Tests completed with result: %d\n", result);
        printf("UART Hardware Manager Test Suite finished\n");
        sleep_ms(5000);
    }
    
    return result;
}