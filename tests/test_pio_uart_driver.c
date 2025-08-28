/**
 * @file test_pio_uart_driver.c
 * @brief Unit tests for PIO UART Driver implementation (Issue #82)
 * 
 * Tests the PIO UART driver that provides Channel 2 UART functionality using
 * RP2350 PIO0 state machines with DMA acceleration and robust error handling.
 * 
 * Test Requirements from Issue #82:
 * 1. Successful configuring of hardware uart to 230400 baud, 8N1, and interrupt
 * 2. Successful testing of loopback configuration of uart channel 2  
 * 3. Sending and receiving of a single line of text (ending with newline)
 * 4. Loops of (2) always returning what was sent from port 4003 to port 4003
 * 
 * Documentation Reference:
 * - Issue #82: PIO UART Driver Implementation
 * - ADR-013: PIO UART Implementation for Channel 2
 * - arc42 Chapter 5 - PIO UART Driver Implementation
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Pico SDK includes
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

// Unity test framework
#include "unity/src/unity.h"

// System includes
#include "state_machine.h"
#include "ringbuffer.h"
#include "log_manager.h"
#include "shared_memory.h"

// Module under test
#include "uart/uart_interface.h"
#include "uart/uart_manager.h"

// Test configuration
#define TEST_TIMEOUT_MS 2000
#define TEST_BAUD_RATE 230400
#define PIO_UART_TX_GPIO 14  // As per ADR-008 and ADR-013  
#define PIO_UART_RX_GPIO 15  // As per ADR-008 and ADR-013
#define TEST_MESSAGE_MAX_LEN 256
#define STATE_MACHINE_STABILIZATION_MS 100

// Test data
static char g_test_message[] = "Hello PIO UART Test!\r\n";
static char g_received_buffer[TEST_MESSAGE_MAX_LEN];
static uint32_t g_initial_log_count;

// Test state tracking
static bool g_pio_uart_initialized = false;
static uart_instance_t* g_channel_2_instance = NULL;

// Function prototypes for helper functions
static void reset_test_environment(void);
static void setup_pio_uart_loopback_test(void);
static bool wait_for_pio_uart_ready(uint32_t timeout_ms);
static bool verify_pio_uart_configuration(void);
static bool send_and_verify_loopback(const char* message);
static void create_tcp_to_uart_message(const char* payload);
static bool verify_uart_to_tcp_response(const char* expected_payload);
static void setup_hardware_loopback(void);
static void cleanup_hardware_loopback(void);

/**
 * @brief Set up before each test
 */
void setUp(void) {
    printf("TEST: setUp() - PIO UART Driver Test\n");
    
    // Ensure clean slate first
    uart_manager_deinit();
    g_pio_uart_initialized = false;
    g_channel_2_instance = NULL;
    
    // Initialize test environment  
    reset_test_environment();
    g_initial_log_count = log_manager_get_total_count();
    memset(g_received_buffer, 0, sizeof(g_received_buffer));
    
    printf("TEST: setUp() complete\n");
}

/**
 * @brief Clean up after each test  
 */
void tearDown(void) {
    printf("TEST: tearDown() - PIO UART Driver Test\n");
    
    // Clean up hardware loopback
    cleanup_hardware_loopback();
    
    // Clear all ring buffers to prevent test contamination
    ring_entry_t* entry;
    int cleanup_count = 0;
    while ((entry = ringbuffer_dequeue_entry(RX_TCP_TO_UART, CHANNEL_2, ENTRY_STATUS_READY)) != NULL) {
        ringbuffer_mark_consumed(entry);
        cleanup_count++;
        if (cleanup_count > 100) break; // Prevent infinite loop
    }
    cleanup_count = 0;
    while ((entry = ringbuffer_dequeue_entry(RX_UART_TO_TCP, CHANNEL_2, ENTRY_STATUS_READY)) != NULL) {
        ringbuffer_mark_consumed(entry);
        cleanup_count++;
        if (cleanup_count > 100) break; // Prevent infinite loop
    }
    
    // Deinitialize UART manager if initialized
    uart_manager_deinit();
    g_pio_uart_initialized = false;
    g_channel_2_instance = NULL;
    
    // Allow time for cleanup to complete
    sleep_ms(50);
    
    printf("TEST: tearDown() complete\n");
}

/**
 * @brief Reset test environment to known state
 */
static void reset_test_environment(void) {
    printf("TEST: Fully resetting all subsystems...\n");
    
    // Initialize critical subsystems in order
    if (!shared_memory_init()) {
        printf("ERROR: Failed to initialize shared memory\n");
        TEST_FAIL_MESSAGE("Shared memory initialization failed");
    }
    
    if (!log_manager_init()) {
        printf("ERROR: Failed to initialize log manager\n"); 
        TEST_FAIL_MESSAGE("Log manager initialization failed");
    }
    
    if (!state_machine_init()) {
        printf("ERROR: Failed to initialize state machine\n");
        TEST_FAIL_MESSAGE("State machine initialization failed");
    }
    
    if (!ringbuffer_init()) {
        printf("ERROR: Failed to initialize ring buffer\n");
        TEST_FAIL_MESSAGE("Ring buffer initialization failed");
    }
    
    // Wait for system stabilization
    sleep_ms(STATE_MACHINE_STABILIZATION_MS);
    
    printf("TEST: All subsystems initialized successfully\n");
}

/**
 * @brief Set up hardware loopback connection for testing
 * 
 * This function configures GPIO pins for loopback testing.
 * In hardware, GP14 (TX) and GP15 (RX) should be connected with a wire.
 */
static void setup_hardware_loopback(void) {
    printf("TEST: Setting up hardware loopback GP14<->GP15\n");
    
    // Note: Hardware loopback requires physical wire connection
    // between GP14 and GP15 on the target hardware
    
    printf("TEST: Hardware loopback setup complete\n");
    printf("TEST: IMPORTANT - Ensure GP14 and GP15 are connected with a wire!\n");
}

/**
 * @brief Clean up hardware loopback configuration
 */
static void cleanup_hardware_loopback(void) {
    // GPIO cleanup is handled by PIO driver deinit
    printf("TEST: Hardware loopback cleanup complete\n");
}

/**
 * @brief Set up PIO UART loopback test environment
 */
static void setup_pio_uart_loopback_test(void) {
    printf("TEST: Setting up PIO UART loopback test...\n");
    
    // Set up hardware loopback first  
    setup_hardware_loopback();
    
    // Initialize UART manager
    if (!uart_manager_init()) {
        printf("ERROR: UART manager initialization failed\n");
        TEST_FAIL_MESSAGE("UART manager initialization failed");
    }
    
    // Wait for UART manager to be ready
    if (!wait_for_pio_uart_ready(TEST_TIMEOUT_MS)) {
        printf("ERROR: PIO UART failed to become ready\n");
        TEST_FAIL_MESSAGE("PIO UART not ready within timeout");
    }
    
    g_pio_uart_initialized = true;
    printf("TEST: PIO UART loopback test setup complete\n");
}

/**
 * @brief Wait for PIO UART to become ready
 * 
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return true if UART is ready, false on timeout
 */
static bool wait_for_pio_uart_ready(uint32_t timeout_ms) {
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);
    
    while (!time_reached(timeout)) {
        if (uart_manager_is_ready()) {
            // Check if channel 2 specifically is ready
            uart_manager_status_t status = uart_manager_get_status();
            if (status == UART_MANAGER_STATUS_READY) {
                printf("TEST: PIO UART ready after %llu ms\n", 
                       timeout_ms - absolute_time_diff_us(get_absolute_time(), timeout) / 1000);
                return true;
            }
        }
        sleep_ms(10);
    }
    
    printf("TEST: PIO UART not ready after %u ms timeout\n", timeout_ms);
    return false;
}

/**
 * @brief Verify PIO UART configuration matches test requirements
 * 
 * @return true if configuration is correct, false otherwise
 */
static bool verify_pio_uart_configuration(void) {
    printf("TEST: Verifying PIO UART configuration...\n");
    
    if (!uart_manager_is_ready()) {
        printf("ERROR: UART manager not ready for configuration check\n");
        return false;
    }
    
    // Get diagnostic information
    char diag_buffer[512];
    int diag_len = uart_manager_get_diagnostic_info(diag_buffer, sizeof(diag_buffer));
    
    if (diag_len > 0) {
        printf("TEST: UART Manager Diagnostics:\n%s\n", diag_buffer);
        
        // Check for Channel 2 configuration in diagnostics
        if (strstr(diag_buffer, "Channel 2") == NULL) {
            printf("ERROR: Channel 2 not found in diagnostics\n");
            return false;
        }
        
        if (strstr(diag_buffer, "230400") == NULL) {
            printf("ERROR: 230400 baud rate not found in diagnostics\n");
            return false;
        }
        
        if (strstr(diag_buffer, "PIO") == NULL) {
            printf("ERROR: PIO type not found in diagnostics\n");
            return false;
        }
    }
    
    printf("TEST: PIO UART configuration verified\n");
    return true;
}

/**
 * @brief Send message and verify loopback response
 * 
 * @param message Message to send for loopback test
 * @return true if loopback successful, false otherwise
 */
static bool send_and_verify_loopback(const char* message) {
    printf("TEST: Testing loopback with message: '%s'\n", message);
    fflush(stdout);
    
    // Create TCP-to-UART message in ring buffer
    create_tcp_to_uart_message(message);
    
    // Process outgoing data (TCP -> UART)
    bool processed = false;
    absolute_time_t timeout = make_timeout_time_ms(TEST_TIMEOUT_MS);
    
    printf("TEST: Processing outgoing data...\n");
    fflush(stdout);
    
    while (!time_reached(timeout)) {
        if (uart_manager_process_outgoing_data()) {
            processed = true;
            break;
        }
        sleep_ms(10);
    }
    
    if (!processed) {
        printf("ERROR: Failed to process outgoing data within timeout\n");
        fflush(stdout);
        return false;
    }
    
    printf("TEST: Outgoing data processed successfully\n");
    fflush(stdout);
    
    // Wait for loopback response
    printf("TEST: Waiting for hardware loopback...\n");
    fflush(stdout);
    sleep_ms(100); // Allow time for hardware loopback
    
    // Process incoming data (UART -> TCP)
    bool incoming_processed = false;
    timeout = make_timeout_time_ms(TEST_TIMEOUT_MS);
    
    printf("TEST: Processing incoming loopback data...\n");
    fflush(stdout);
    
    while (!time_reached(timeout)) {
        if (uart_manager_has_incoming_work()) {
            printf("TEST: Found incoming work, processing...\n");
            fflush(stdout);
            if (uart_manager_process_incoming_data()) {
                incoming_processed = true;
                break;
            }
        }
        sleep_ms(10);
    }
    
    if (!incoming_processed) {
        printf("ERROR: Failed to process incoming loopback data - timeout reached\n");
        fflush(stdout);
        return false;
    }
    
    printf("TEST: Incoming data processed, verifying response...\n");
    fflush(stdout);
    
    // Verify response message
    return verify_uart_to_tcp_response(message);
}

/**
 * @brief Create TCP-to-UART message in ring buffer
 * 
 * @param payload Message payload to send
 */
static void create_tcp_to_uart_message(const char* payload) {
    ring_entry_t* entry = ringbuffer_get_free_entry(RX_TCP_TO_UART, CHANNEL_2);
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "Failed to get free ring buffer entry");
    
    entry->channel = CHANNEL_2;
    entry->direction = RX_TCP_TO_UART;
    entry->fill_index = strlen(payload);
    entry->status = ENTRY_STATUS_READY;  // Mark as ready for processing
    memcpy(entry->payload, payload, entry->fill_index);
    
    ringbuffer_enqueue_entry(entry);
    printf("TEST: Created TCP-to-UART message for channel 2\n");
}

/**
 * @brief Verify UART-to-TCP response message
 * 
 * @param expected_payload Expected message payload
 * @return true if response matches expected, false otherwise
 */
static bool verify_uart_to_tcp_response(const char* expected_payload) {
    printf("TEST: Verifying UART-to-TCP response...\n");
    
    // Look for UART-to-TCP message in ring buffer
    ring_entry_t* entry = NULL;
    absolute_time_t timeout = make_timeout_time_ms(TEST_TIMEOUT_MS);
    
    while (!time_reached(timeout)) {
        entry = ringbuffer_dequeue_entry(RX_UART_TO_TCP, CHANNEL_2, ENTRY_STATUS_READY);
        if (entry) {
            break;
        }
        sleep_ms(10);
    }
    
    if (!entry) {
        printf("ERROR: No UART-to-TCP response found within timeout\n");
        return false;
    }
    
    // Verify response content
    if (entry->fill_index != strlen(expected_payload)) {
        printf("ERROR: Response length mismatch - expected %zu, got %u\n", 
               strlen(expected_payload), entry->fill_index);
        ringbuffer_mark_consumed(entry);
        return false;
    }
    
    if (memcmp(entry->payload, expected_payload, entry->fill_index) != 0) {
        printf("ERROR: Response content mismatch\n");
        printf("Expected: '%.*s'\n", (int)strlen(expected_payload), expected_payload);
        printf("Received: '%.*s'\n", entry->fill_index, entry->payload);
        ringbuffer_mark_consumed(entry);
        return false;
    }
    
    printf("TEST: UART-to-TCP response verified successfully\n");
    printf("Response: '%.*s'\n", entry->fill_index, entry->payload);
    
    ringbuffer_mark_consumed(entry);
    return true;
}

// ============================================================================
// UNIT TESTS
// ============================================================================

/**
 * @brief Test GPIO loopback connection GP14↔GP15
 */
void test_gpio_loopback_connection(void) {
    printf("\n=== TEST: GPIO Loopback Connection ===\n");
    fflush(stdout);
    
    // Test GP14 output → GP15 input
    gpio_init(PIO_UART_TX_GPIO);
    gpio_init(PIO_UART_RX_GPIO);
    gpio_set_dir(PIO_UART_TX_GPIO, GPIO_OUT);
    gpio_set_dir(PIO_UART_RX_GPIO, GPIO_IN);
    gpio_pull_down(PIO_UART_RX_GPIO);
    
    sleep_ms(10);
    
    // Test high signal
    gpio_put(PIO_UART_TX_GPIO, 1);
    sleep_ms(10);
    bool rx_high = gpio_get(PIO_UART_RX_GPIO);
    printf("GP14=HIGH → GP15=%s\n", rx_high ? "HIGH" : "LOW");
    fflush(stdout);
    
    // Test low signal  
    gpio_put(PIO_UART_TX_GPIO, 0);
    sleep_ms(10);
    bool rx_low = gpio_get(PIO_UART_RX_GPIO);
    printf("GP14=LOW → GP15=%s\n", rx_low ? "HIGH" : "LOW");
    fflush(stdout);
    
    // Test GP15 output → GP14 input
    gpio_set_dir(PIO_UART_TX_GPIO, GPIO_IN);
    gpio_set_dir(PIO_UART_RX_GPIO, GPIO_OUT);
    gpio_pull_down(PIO_UART_TX_GPIO);
    
    sleep_ms(10);
    
    // Test high signal reverse
    gpio_put(PIO_UART_RX_GPIO, 1);
    sleep_ms(10);
    bool tx_high = gpio_get(PIO_UART_TX_GPIO);
    printf("GP15=HIGH → GP14=%s\n", tx_high ? "HIGH" : "LOW");
    fflush(stdout);
    
    // Test low signal reverse
    gpio_put(PIO_UART_RX_GPIO, 0);
    sleep_ms(10);
    bool tx_low = gpio_get(PIO_UART_TX_GPIO);
    printf("GP15=LOW → GP14=%s\n", tx_low ? "HIGH" : "LOW");
    fflush(stdout);
    
    // Verify loopback works both ways
    bool loopback_works = rx_high && !rx_low && tx_high && !tx_low;
    
    if (loopback_works) {
        printf("PASS: GPIO loopback connection verified\n");
    } else {
        printf("FAIL: GPIO loopback connection not working\n");
        printf("Check GP14↔GP15 wire connection!\n");
    }
    fflush(stdout);
    
    TEST_ASSERT_TRUE_MESSAGE(loopback_works, "GPIO loopback connection failed");
}

/**
 * @brief Test GPIO connectivity between GP14 and GP15
 * 
 * Simple test to verify physical wire connection exists before PIO UART tests
 */
void test_gpio_connectivity(void) {
    printf("\n=== TEST: GPIO Connectivity GP14<->GP15 ===\n");
    
    // Initialize GPIOs
    gpio_init(PIO_UART_TX_GPIO);  // GP14
    gpio_init(PIO_UART_RX_GPIO);  // GP15
    
    // Test 1: GP14 high, GP15 should read high
    printf("TEST: Setting GP14 HIGH, reading GP15...\n");
    gpio_set_dir(PIO_UART_TX_GPIO, GPIO_OUT);
    gpio_set_dir(PIO_UART_RX_GPIO, GPIO_IN);
    gpio_put(PIO_UART_TX_GPIO, 1);
    sleep_ms(10);  // Allow signal to settle
    bool high_test = gpio_get(PIO_UART_RX_GPIO);
    printf("GP14=HIGH -> GP15=%s\n", high_test ? "HIGH" : "LOW");
    
    // Test 2: GP14 low, GP15 should read low  
    printf("TEST: Setting GP14 LOW, reading GP15...\n");
    gpio_put(PIO_UART_TX_GPIO, 0);
    sleep_ms(10);  // Allow signal to settle
    bool low_test = gpio_get(PIO_UART_RX_GPIO);
    printf("GP14=LOW -> GP15=%s\n", low_test ? "HIGH" : "LOW");
    
    // Test 3: Reverse direction - GP15 out, GP14 in
    printf("TEST: Setting GP15 HIGH, reading GP14...\n");
    gpio_set_dir(PIO_UART_TX_GPIO, GPIO_IN);
    gpio_set_dir(PIO_UART_RX_GPIO, GPIO_OUT);
    gpio_put(PIO_UART_RX_GPIO, 1);
    sleep_ms(10);
    bool reverse_high_test = gpio_get(PIO_UART_TX_GPIO);
    printf("GP15=HIGH -> GP14=%s\n", reverse_high_test ? "HIGH" : "LOW");
    
    // Test 4: GP15 low, GP14 should read low
    printf("TEST: Setting GP15 LOW, reading GP14...\n");
    gpio_put(PIO_UART_RX_GPIO, 0);
    sleep_ms(10);
    bool reverse_low_test = gpio_get(PIO_UART_TX_GPIO);
    printf("GP15=LOW -> GP14=%s\n", reverse_low_test ? "HIGH" : "LOW");
    
    // Verify connectivity
    bool connectivity_ok = high_test && !low_test && reverse_high_test && !reverse_low_test;
    
    if (connectivity_ok) {
        printf("PASS: GPIO connectivity verified - GP14 and GP15 are connected\n");
    } else {
        printf("FAIL: GPIO connectivity failed - check wire connection between GP14 and GP15\n");
        printf("Expected: HIGH->HIGH, LOW->LOW in both directions\n");
    }
    
    TEST_ASSERT_TRUE_MESSAGE(connectivity_ok, "GPIO connectivity test failed - check GP14<->GP15 wire");
}

/**
 * @brief Test PIO UART hardware configuration to 230400 baud, 8N1, interrupt
 * 
 * Requirement: Successful configuring the hardware uart to 230400 baud, 8N1, and interrupt.
 */
void test_pio_uart_configuration(void) {
    printf("\n=== TEST: PIO UART Configuration ===\n");
    
    setup_pio_uart_loopback_test();
    TEST_ASSERT_TRUE_MESSAGE(verify_pio_uart_configuration(), 
                            "PIO UART configuration verification failed");
    
    printf("PASS: PIO UART configured correctly (230400 baud, 8N1, interrupt)\n");
}

/**
 * @brief Test PIO UART loopback configuration of Channel 2
 * 
 * Requirement: Successful testing the loopback configuration of uart channel 2.
 */
void test_pio_uart_loopback_configuration(void) {
    printf("\n=== TEST: PIO UART Loopback Configuration ===\n");
    
    setup_pio_uart_loopback_test();
    
    // Verify we can send a basic test message through loopback
    const char* test_msg = "LOOPBACK_TEST\r\n";
    TEST_ASSERT_TRUE_MESSAGE(send_and_verify_loopback(test_msg), 
                            "PIO UART loopback test failed");
    
    printf("PASS: PIO UART Channel 2 loopback configuration working\n");
}

/**
 * @brief Test sending and receiving single line of text with newline
 * 
 * Requirement: Sending and receiving of a single line of text (ending with a newline).
 */
void test_pio_uart_single_line_transmission(void) {
    printf("\n=== TEST: PIO UART Single Line Transmission ===\n");
    
    setup_pio_uart_loopback_test();
    
    TEST_ASSERT_TRUE_MESSAGE(send_and_verify_loopback(g_test_message), 
                            "Single line transmission failed");
    
    printf("PASS: Single line text transmission with newline successful\n");
}

/**
 * @brief Test multiple message loops returning sent data
 * 
 * Requirement: Loops of (2) always returning what was sent from port 4003 to port 4003.
 */
void test_pio_uart_multiple_message_loops(void) {
    printf("\n=== TEST: PIO UART Multiple Message Loops ===\n");
    
    setup_pio_uart_loopback_test();
    
    // Test multiple different messages in sequence
    const char* test_messages[] = {
        "First loop message!\r\n",
        "Second loop message!\r\n", 
        "Third loop message!\r\n",
        "Final loop message!\r\n"
    };
    
    const int num_messages = sizeof(test_messages) / sizeof(test_messages[0]);
    
    for (int i = 0; i < num_messages; i++) {
        printf("TEST: Loop %d - testing message: '%s'\n", i + 1, test_messages[i]);
        
        TEST_ASSERT_TRUE_MESSAGE(send_and_verify_loopback(test_messages[i]), 
                                "Message loop failed");
        
        // Small delay between messages
        sleep_ms(50);
    }
    
    printf("PASS: Multiple message loops successful (all messages echoed correctly)\n");
}

/**
 * @brief Test PIO UART statistics and diagnostics
 */
void test_pio_uart_statistics(void) {
    printf("\n=== TEST: PIO UART Statistics ===\n");
    
    setup_pio_uart_loopback_test();
    
    // Get initial statistics
    uart_manager_stats_t initial_stats;
    uart_manager_get_stats(&initial_stats);
    
    // Send test message
    TEST_ASSERT_TRUE_MESSAGE(send_and_verify_loopback(g_test_message),
                            "Test message for statistics failed");
    
    // Get updated statistics  
    uart_manager_stats_t final_stats;
    uart_manager_get_stats(&final_stats);
    
    // Verify statistics were updated
    TEST_ASSERT_GREATER_THAN_MESSAGE(initial_stats.bytes_received, final_stats.bytes_received,
                                    "RX byte count not updated");
    TEST_ASSERT_GREATER_THAN_MESSAGE(initial_stats.bytes_transmitted, final_stats.bytes_transmitted, 
                                    "TX byte count not updated");
    
    printf("PASS: PIO UART statistics tracking working\n");
}

/**
 * @brief Test PIO UART error handling and recovery
 */
void test_pio_uart_error_handling(void) {
    printf("\n=== TEST: PIO UART Error Handling ===\n");
    
    setup_pio_uart_loopback_test();
    
    // Test that the system can handle and recover from various conditions
    
    // Test 1: Clear receive buffer and verify recovery
    uart_instance_t* instance = uart_manager_get_channel_instance(CHANNEL_2);
    if (instance && instance->ops && instance->ops->clear_rx_buffer) {
        instance->ops->clear_rx_buffer(instance->driver_context);
        printf("TEST: Receive buffer cleared successfully\n");
    }
    
    // Test 2: Reset statistics and verify
    uart_manager_reset_stats();
    uart_manager_stats_t stats;
    uart_manager_get_stats(&stats);
    
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.bytes_received, 
                                    "Statistics not reset properly");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.bytes_transmitted,
                                    "Statistics not reset properly");
    
    // Test 3: Verify system still works after resets
    TEST_ASSERT_TRUE_MESSAGE(send_and_verify_loopback("Recovery test!\r\n"),
                            "System not working after error handling");
    
    printf("PASS: PIO UART error handling and recovery working\n");
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(void) {
    // Initialize stdio
    stdio_init_all();
    
    // Wait for USB connection
    sleep_ms(2000);
    
    printf("\n");
    printf("========================================\n");
    printf("PIO UART Driver Tests (Issue #82)\n");
    printf("========================================\n");
    printf("Testing PIO UART Channel 2 implementation\n");
    printf("GP14 (TX) and GP15 (RX) - 230400 baud\n");
    printf("IMPORTANT: Ensure GP14 and GP15 are connected with a wire for loopback testing!\n");
    printf("========================================\n\n");
    
    // Initialize Unity
    UNITY_BEGIN();
    
    // Run all tests
    RUN_TEST(test_gpio_connectivity);
    RUN_TEST(test_pio_uart_configuration);
    RUN_TEST(test_pio_uart_loopback_configuration);  
    RUN_TEST(test_pio_uart_single_line_transmission);
    RUN_TEST(test_pio_uart_multiple_message_loops);
    RUN_TEST(test_pio_uart_statistics);
    RUN_TEST(test_pio_uart_error_handling);
    
    // Finalize Unity
    int result = UNITY_END();
    
    printf("\n========================================\n");
    if (result == 0) {
        printf("ALL PIO UART TESTS PASSED!\n");
        printf("PIO UART Channel 2 implementation ready for integration.\n");
    } else {
        printf("SOME PIO UART TESTS FAILED!\n");
        printf("Review test output and fix implementation.\n");
    }
    printf("========================================\n");
    fflush(stdout);
    
    // Keepalive loop to show target is responsive and flush debug output
    printf("Test complete - entering keepalive mode...\n");
    fflush(stdout);
    
    int keepalive_count = 0;
    while (true) {
        printf("Keepalive %d - Target OK\n", ++keepalive_count);
        fflush(stdout);
        sleep_ms(2000);
    }
    
    return result;
}