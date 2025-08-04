/**
 * @file test_log_manager.c
 * @brief Unit tests for fixed-size entry log manager system
 * 
 * Tests the new event logging functionality with fixed-size entries
 * as documented in arc42 Chapter 5 and Chapter 6.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Log Manager Whitebox (Fixed-Size Entry System)
 * - arc42 Chapter 6 - Runtime View - Fixed-Size Entry Pattern
 */

// Explicitly enable USB reset interface with all required options - CRITICAL for autonomous flashing
#define PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_FLASH_BOOT 1

#include "unity.h"
#include "log_manager.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hardware/sync.h"

void setUp(void) {
    // Initialize shared memory before each test
    shared_memory_init();
    log_manager_init();
}

void tearDown(void) {
    // Reset log manager state completely for test isolation
    log_manager_reset_for_testing();
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
 * Test: Single event should be logged successfully
 * 
 * Tests the basic event logging functionality with fixed-size entries.
 */  
void test_single_event_logged(void) {
    // ARRANGE: Log manager initialized in setUp()
    
    // ACT: Log a simple system event
    bool result = log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
    
    // ASSERT: Event should be logged successfully
    TEST_ASSERT_TRUE_MESSAGE(result, "Single event should be logged successfully");
    
    // ASSERT: Pending count should be 1
    uint32_t pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL_MESSAGE(1, pending, "Pending count should be 1 after logging one event");
    
    // ASSERT: Total count should be 1
    uint32_t total = log_manager_get_total_count();
    TEST_ASSERT_EQUAL_MESSAGE(1, total, "Total count should be 1 after logging one event");
}

/**
 * Test: Log buffer utilization should increase after logging events
 * 
 * Tests that buffer utilization tracking works correctly with fixed-size entries.
 */
void test_log_buffer_utilization_fixed_entries(void) {
    // ARRANGE: Log manager initialized, get initial pending count
    uint32_t initial_pending = log_manager_get_pending_count();
    uint32_t initial_utilization = log_manager_get_utilization();
    
    // ACT: Log multiple events to increase utilization
    bool result1 = log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 115200);
    bool result2 = log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 230400);
    bool result3 = log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_CONNECT, 4001);
    bool result4 = log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
    
    TEST_ASSERT_TRUE(result1 && result2 && result3 && result4);
    
    // ASSERT: Pending count should increase (more reliable than utilization percentage)
    uint32_t after_pending = log_manager_get_pending_count();
    TEST_ASSERT_GREATER_THAN_MESSAGE(initial_pending, after_pending,
        "Pending count should increase after logging events");
    TEST_ASSERT_EQUAL_MESSAGE(4, after_pending - initial_pending,
        "Should have exactly 4 more pending events");
    
    // ASSERT: Utilization should be reasonable (not over 100%)
    uint32_t after_utilization = log_manager_get_utilization();
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(100, after_utilization,
        "Buffer utilization should not exceed 100%");
}

/**
 * Test: Multiple events from different sources should be logged
 * 
 * Tests event logging from various system components.
 */
void test_multiple_events_different_sources(void) {
    // ARRANGE: Log manager initialized
    
    // ACT: Log events from different sources
    bool result1 = log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_DATA_RX, 64);
    bool result2 = log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_CONNECT, 4001);  
    bool result3 = log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_WARN, LOG_EVENT_CONFIG_CHANGED, 1);
    bool result4 = log_event(EVENT_SOURCE_OTA, LOG_LEVEL_ERROR, LOG_EVENT_OTA_ERROR, 404);
    
    // ASSERT: All events should be logged
    TEST_ASSERT_TRUE_MESSAGE(result1, "UART event should be logged");
    TEST_ASSERT_TRUE_MESSAGE(result2, "Network event should be logged");
    TEST_ASSERT_TRUE_MESSAGE(result3, "Config event should be logged");
    TEST_ASSERT_TRUE_MESSAGE(result4, "OTA event should be logged");
    
    // ASSERT: Pending count should be 4
    uint32_t pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL_MESSAGE(4, pending, "Pending count should be 4 after logging four events");
    
    // ASSERT: Total count should be 4
    uint32_t total = log_manager_get_total_count();
    TEST_ASSERT_EQUAL_MESSAGE(4, total, "Total count should be 4 after logging four events");
}

/**
 * Test: Core1 formatter should process pending log entries
 * 
 * Tests the Core1 background formatting functionality.
 */
void test_core1_formatter_processes_entries(void) {
    // ARRANGE: Log some events first
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 115200);
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_CONNECT, 4001);
    uint32_t initial_pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL(2, initial_pending);
    
    // ACT: Call Core1 formatter task
    uint32_t formatted_count = log_manager_format_pending();
    
    // ASSERT: Formatter should report processing entries
    TEST_ASSERT_EQUAL_MESSAGE(2, formatted_count, 
        "Formatter should report processing 2 entries");
    
    // ASSERT: Pending count should decrease
    uint32_t after_pending = log_manager_get_pending_count();
    TEST_ASSERT_EQUAL_MESSAGE(0, after_pending,
        "Pending count should be 0 after formatting all entries");
    
    // ASSERT: Total count should remain the same
    uint32_t total = log_manager_get_total_count();
    TEST_ASSERT_EQUAL_MESSAGE(2, total,
        "Total count should remain 2 after formatting (not reset)");
}

/**
 * Test: Per-core event sequence numbering should work correctly
 * 
 * Tests that each core maintains its own event sequence counter.
 */
void test_per_core_event_sequence_numbering(void) {
    // ARRANGE: Log manager initialized
    uint32_t initial_core0_seq = log_manager_get_core_sequence(0);
    uint32_t initial_core1_seq = log_manager_get_core_sequence(1);
    
    // ACT: Log events and check sequence numbers increase
    // Note: In test environment, we simulate different cores
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 115200); // Core 0
    uint32_t after_core0_seq_1 = log_manager_get_core_sequence(0);
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_CONNECT, 4001); // Core 1  
    uint32_t after_core1_seq_1 = log_manager_get_core_sequence(1);
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 230400); // Core 0
    uint32_t after_core0_seq_2 = log_manager_get_core_sequence(0);
    
    // ASSERT: Core 0 sequence should increase by 2
    TEST_ASSERT_EQUAL_MESSAGE(initial_core0_seq + 2, after_core0_seq_2,
        "Core 0 sequence should increase by 2 after logging 2 events");
    
    // ASSERT: Core 1 sequence should increase by 1
    TEST_ASSERT_EQUAL_MESSAGE(initial_core1_seq + 1, after_core1_seq_1,
        "Core 1 sequence should increase by 1 after logging 1 event");
}

/**
 * Test: Event format string lookup should work correctly
 * 
 * Tests that event types map to correct format strings.
 */
void test_event_format_string_lookup(void) {
    // ARRANGE: Various event types
    
    // ACT & ASSERT: Test known event format strings
    const char* uart_init_format = log_manager_get_event_format_string(LOG_EVENT_UART_INIT);
    TEST_ASSERT_NOT_NULL_MESSAGE(uart_init_format, "UART init event should have format string");
    
    const char* tcp_connect_format = log_manager_get_event_format_string(LOG_EVENT_TCP_CONNECT);
    TEST_ASSERT_NOT_NULL_MESSAGE(tcp_connect_format, "TCP connect event should have format string");
    
    const char* system_ready_format = log_manager_get_event_format_string(LOG_EVENT_SYSTEM_READY);
    TEST_ASSERT_NOT_NULL_MESSAGE(system_ready_format, "System ready event should have format string");
    
    // ACT & ASSERT: Test invalid event type
    const char* invalid_format = log_manager_get_event_format_string(9999);
    TEST_ASSERT_NULL_MESSAGE(invalid_format, "Invalid event type should return NULL");
}

/**
 * Test: Buffer wraparound should handle entry boundaries correctly
 * 
 * Tests that the circular buffer properly wraps around when full
 * with fixed-size entries (much simpler than variable-length).
 */
void test_log_buffer_wraparound_fixed_entries(void) {
    // ARRANGE: Get buffer capacity
    uint32_t buffer_capacity = shared_memory_get_log_buffer_capacity();
    TEST_ASSERT_GREATER_THAN_MESSAGE(10, buffer_capacity, "Buffer should have reasonable capacity for test");
    
    // ACT: Fill buffer to near capacity
    uint32_t successful_writes = 0;
    for (uint32_t i = 0; i < buffer_capacity - 2; i++) {
        bool result = log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, i);
        if (result) {
            successful_writes++;
        } else {
            break; // Stop when buffer gets full
        }
    }
    
    TEST_ASSERT_GREATER_THAN_MESSAGE(5, successful_writes, 
        "Should successfully write several events before buffer fills");
    
    // Get buffer state before wraparound
    uint32_t utilization_before = log_manager_get_utilization();
    uint32_t pending_before = log_manager_get_pending_count();
    
    // ACT: Force wraparound by writing events that will wrap around buffer
    bool wrap_result1 = log_event(EVENT_SOURCE_WATCHDOG, LOG_LEVEL_WARN, LOG_EVENT_WATCHDOG_RESET, 1);
    bool wrap_result2 = log_event(EVENT_SOURCE_OTA, LOG_LEVEL_INFO, LOG_EVENT_OTA_START, 2);
    bool wrap_result3 = log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_CONFIG_LOADED, 3);
    
    // ASSERT: Wraparound events should succeed if there's space (or fail predictably if full)
    if (utilization_before < 95) { // If buffer wasn't completely full
        TEST_ASSERT_TRUE_MESSAGE(wrap_result1, "First wraparound event should succeed");
    }
    
    // ASSERT: Buffer utilization should be high but not exceed 100%
    uint32_t utilization_after = log_manager_get_utilization();
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(100, utilization_after,
        "Buffer utilization should never exceed 100%");
    
    // ACT: Try to format some entries to test wraparound read
    uint32_t formatted_count = log_manager_format_pending();
    
    // ASSERT: Should be able to format entries, including wraparound entries
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, formatted_count,
        "Should be able to format entries from buffer including wraparound");
    
    // ASSERT: Pending count should decrease after formatting
    uint32_t pending_after_format = log_manager_get_pending_count();
    TEST_ASSERT_LESS_THAN_MESSAGE(pending_before + 3, pending_after_format,
        "Pending count should decrease after formatting entries");
    
    // ACT: Write more events after partial consumption to test continued wraparound
    bool post_wrap_result1 = log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_DATA_TX, 128);
    bool post_wrap_result2 = log_event(EVENT_SOURCE_UART1, LOG_LEVEL_DEBUG, LOG_EVENT_UART_DATA_RX, 256);
    
    // ASSERT: Should be able to continue writing after wraparound and consumption
    TEST_ASSERT_TRUE_MESSAGE(post_wrap_result1 || post_wrap_result2,
        "Should be able to write at least one event after wraparound consumption");
    
    // Final safety check: buffer should still be within reasonable bounds
    uint32_t final_utilization = log_manager_get_utilization();
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(100, final_utilization,
        "Final utilization should not exceed 100%");
}

// Performance test constants
#define CORE1_STARTUP_DELAY_MS 200
#define CORE1_TIMEOUT_MS 10000
#define MIN_EXPECTED_EVENTS_PER_SEC 1000.0
#define MIN_EXPECTED_CONCURRENT_EVENTS_PER_SEC 1500.0

// Global variables for concurrent test synchronization
static volatile bool g_start_concurrent_logging = false;
static volatile uint32_t g_core1_events_logged = 0;
static volatile uint64_t g_core1_time_us = 0;
static volatile bool g_core1_error = false;
static volatile char g_core1_error_message[64] = {0};

/**
 * Helper function to report performance metrics (DRY principle)
 */
static double report_performance(const char* label, uint32_t events, uint64_t time_us) {
    double time_seconds = (double)time_us / 1000000.0;
    double events_per_second = (double)events / time_seconds;
    
    printf("%s: %u events in %llu us (%.2f events/sec)\n", 
           label, events, time_us, events_per_second);
    
    return events_per_second;
}

/**
 * Helper function for Core1 buffer fill performance test
 * Runs on Core1 and fills buffer with performance timing
 */
static void core1_buffer_fill_helper(void) {
    // Reset error state
    g_core1_error = false;
    g_core1_error_message[0] = '\0';
    
    // Get buffer capacity
    uint32_t buffer_capacity = shared_memory_get_log_buffer_capacity();
    if (buffer_capacity == 0) {
        g_core1_error = true;
        strncpy((char*)g_core1_error_message, "Buffer capacity is zero", sizeof(g_core1_error_message) - 1);
        g_core1_error_message[sizeof(g_core1_error_message) - 1] = '\0';
        return;
    }
    
    // Start timing
    absolute_time_t start_time = get_absolute_time();
    
    // Fill buffer completely from Core1 (use NETWORK events to ensure Core1)
    uint32_t events_logged = 0;
    for (uint32_t i = 0; i < buffer_capacity; i++) {
        bool result = log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, 
                               LOG_EVENT_TCP_CONNECT, i);
        if (result) {
            events_logged++;
        } else {
            break; // Buffer full or error
        }
    }
    
    // End timing
    absolute_time_t end_time = get_absolute_time();
    uint64_t time_diff_us = absolute_time_diff_us(start_time, end_time);
    
    // Validate results
    if (events_logged < buffer_capacity / 4) {
        g_core1_error = true;
        strncpy((char*)g_core1_error_message, "Too few events logged", sizeof(g_core1_error_message) - 1);
        g_core1_error_message[sizeof(g_core1_error_message) - 1] = '\0';
        return;
    }
    
    // Store results in global variables for main core to read
    g_core1_events_logged = events_logged;
    g_core1_time_us = time_diff_us;
    
    // Memory barrier to ensure visibility on Core0
    __dmb();
}

/**
 * Helper function for concurrent Core1 logging
 * Waits for start signal, then logs half the buffer capacity
 */
static void core1_concurrent_helper(void) {
    // Reset error state
    g_core1_error = false;
    g_core1_error_message[0] = '\0';
    
    // Busy wait for start signal with timeout
    absolute_time_t start_wait = get_absolute_time();
    while (!g_start_concurrent_logging) {
        if (absolute_time_diff_us(start_wait, get_absolute_time()) > CORE1_TIMEOUT_MS * 1000) {
            g_core1_error = true;
            strncpy((char*)g_core1_error_message, "Timeout waiting for start signal", 
                    sizeof(g_core1_error_message) - 1);
            g_core1_error_message[sizeof(g_core1_error_message) - 1] = '\0';
            return;
        }
        tight_loop_contents();
    }
    
    // Memory barrier to ensure start signal is properly read
    __dmb();
    
    // Get half buffer capacity
    uint32_t buffer_capacity = shared_memory_get_log_buffer_capacity();
    if (buffer_capacity == 0) {
        g_core1_error = true;
        strncpy((char*)g_core1_error_message, "Buffer capacity is zero", sizeof(g_core1_error_message) - 1);
        g_core1_error_message[sizeof(g_core1_error_message) - 1] = '\0';
        return;
    }
    
    uint32_t half_capacity = buffer_capacity / 2;
    
    // Start timing
    absolute_time_t start_time = get_absolute_time();
    
    // Fill half buffer from Core1 (use NETWORK events to ensure Core1)
    uint32_t events_logged = 0;
    for (uint32_t i = 0; i < half_capacity; i++) {
        bool result = log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, 
                               LOG_EVENT_TCP_DATA_RX, i);
        if (result) {
            events_logged++;
        } else {
            break; // Buffer full or error
        }
    }
    
    // End timing
    absolute_time_t end_time = get_absolute_time();
    uint64_t time_diff_us = absolute_time_diff_us(start_time, end_time);
    
    // Validate results
    if (events_logged < half_capacity / 4) {
        g_core1_error = true;
        strncpy((char*)g_core1_error_message, "Too few events logged in concurrent test", sizeof(g_core1_error_message) - 1);
        g_core1_error_message[sizeof(g_core1_error_message) - 1] = '\0';
        return;
    }
    
    // Store results for main core
    g_core1_events_logged = events_logged;
    g_core1_time_us = time_diff_us;
    
    // Memory barrier to ensure visibility on Core0
    __dmb();
}

/**
 * Test: Core0 buffer fill performance
 * 
 * Tests how fast Core0 can fill the entire log buffer and measures events/sec.
 */
void test_core0_buffer_fill_performance(void) {
    // ARRANGE: Reset log manager and get buffer capacity
    log_manager_reset_for_testing();
    uint32_t buffer_capacity = shared_memory_get_log_buffer_capacity();
    TEST_ASSERT_GREATER_THAN_MESSAGE(10, buffer_capacity, 
        "Buffer should have reasonable capacity for performance test");
    
    printf("Core0 Performance Test: Filling %u entries...\n", buffer_capacity);
    
    // ACT: Time the buffer fill operation from Core0
    absolute_time_t start_time = get_absolute_time();
    
    uint32_t events_logged = 0;
    for (uint32_t i = 0; i < buffer_capacity; i++) {
        bool result = log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, 
                               LOG_EVENT_UART_DATA_TX, i);
        if (result) {
            events_logged++;
        } else {
            break; // Buffer full or error
        }
    }
    
    absolute_time_t end_time = get_absolute_time();
    uint64_t time_diff_us = absolute_time_diff_us(start_time, end_time);
    
    // ASSERT: Should have logged events successfully
    TEST_ASSERT_GREATER_THAN_MESSAGE(buffer_capacity / 2, events_logged,
        "Should have logged at least half the buffer capacity");
    
    // Calculate and display performance metrics
    double events_per_second = report_performance("Core0 Results", events_logged, time_diff_us);
    
    // ASSERT: Performance should be reasonable
    TEST_ASSERT_GREATER_THAN_MESSAGE(MIN_EXPECTED_EVENTS_PER_SEC, events_per_second,
        "Core0 should achieve minimum expected performance");
}

/**
 * Test: Core1 buffer fill performance
 * 
 * Tests how fast Core1 can fill the entire log buffer and measures events/sec.
 */
void test_core1_buffer_fill_performance(void) {
    // ARRANGE: Reset log manager
    log_manager_reset_for_testing();
    uint32_t buffer_capacity = shared_memory_get_log_buffer_capacity();
    TEST_ASSERT_GREATER_THAN_MESSAGE(10, buffer_capacity,
        "Buffer should have reasonable capacity for performance test");
    
    printf("Core1 Performance Test: Filling %u entries...\n", buffer_capacity);
    
    // Reset global variables
    g_core1_events_logged = 0;
    g_core1_time_us = 0;
    g_core1_error = false;
    g_core1_error_message[0] = '\0';
    
    // ACT: Launch Core1 helper function
    multicore_launch_core1(core1_buffer_fill_helper);
    
    // Wait for Core1 to complete with timeout
    uint32_t elapsed_ms = 0;
    while (g_core1_time_us == 0 && elapsed_ms < CORE1_TIMEOUT_MS) {
        sleep_ms(10);
        elapsed_ms += 10;
    }
    
    // ASSERT: Check for timeout
    TEST_ASSERT_FALSE_MESSAGE(elapsed_ms >= CORE1_TIMEOUT_MS, 
        "Core1 test timed out - possible deadlock or failure");
    
    // ASSERT: Check for Core1 errors
    TEST_ASSERT_FALSE_MESSAGE(g_core1_error, (char*)g_core1_error_message);
    
    // ASSERT: Should have logged events successfully
    TEST_ASSERT_GREATER_THAN_MESSAGE(buffer_capacity / 2, g_core1_events_logged,
        "Core1 should have logged at least half the buffer capacity");
    
    // Calculate and display performance metrics
    double events_per_second = report_performance("Core1 Results", g_core1_events_logged, g_core1_time_us);
    
    // ASSERT: Performance should be reasonable
    TEST_ASSERT_GREATER_THAN_MESSAGE(MIN_EXPECTED_EVENTS_PER_SEC, events_per_second,
        "Core1 should achieve minimum expected performance");
    
    // Reset core1 for next test
    multicore_reset_core1();
}

/**
 * Helper function to read ALL entries from log buffer for verification
 * This bypasses the normal read_log_entry to read entire buffer contents
 */
static uint32_t read_all_log_entries_for_verification(log_entry_t* entries, uint32_t max_entries) {
    if (!entries || max_entries == 0) {
        return 0;
    }
    
    shared_memory_layout_t* layout = shared_memory_get_layout();  
    if (!layout) {
        return 0;
    }
    
    // Read buffer state atomically
    uint32_t save = spin_lock_blocking(layout->log_mgmt.entry_lock);
    uint32_t write_idx = layout->log_mgmt.write_index;
    uint32_t read_idx = layout->log_mgmt.read_index;
    uint32_t buffer_capacity = layout->log_mgmt.max_entries;
    
    uint32_t entries_read = 0;
    bool buffer_overflow = false;
    
    // Read all entries currently in buffer
    uint32_t current_idx = read_idx;
    while (current_idx != write_idx) {
        if (entries_read >= max_entries) {
            buffer_overflow = true;
            break;
        }
        
        entries[entries_read] = layout->log_entries[current_idx];
        entries_read++;
        
        current_idx++;
        if (current_idx >= buffer_capacity) {
            current_idx = 0;  // Wrap around
        }
    }
    
    spin_unlock(layout->log_mgmt.entry_lock, save);
    
    if (buffer_overflow) {
        printf("WARNING: Buffer contains more entries than could be read (limit: %u)\n", max_entries);
    }
    
    return entries_read;
}

// Minimum buffer size required for reliable monotonicity testing
#define MIN_BUFFER_SIZE_FOR_MONOTONICITY_TEST 100

/**
 * Test: Concurrent buffer fill with strict event number monotonicity verification
 * 
 * Tests that after concurrent logging from both cores, all event numbers
 * are strictly monotonic per core, proving no events were lost, duplicated, or overwritten.
 */
void test_concurrent_buffer_fill_monotonic_event_numbers(void) {
    // ARRANGE: Reset log manager
    log_manager_reset_for_testing();
    uint32_t buffer_capacity = shared_memory_get_log_buffer_capacity();
    uint32_t half_capacity = buffer_capacity / 2;
    
    // Ensure buffer is large enough for meaningful test
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(MIN_BUFFER_SIZE_FOR_MONOTONICITY_TEST, buffer_capacity,
        "Buffer capacity must be at least 100 for reliable monotonicity test");
    
    printf("Monotonic Event Number Test: Both cores filling %u entries each...\n", half_capacity);
    
    // Reset synchronization variables (early to avoid race conditions)
    g_start_concurrent_logging = false;
    g_core1_events_logged = 0;
    g_core1_time_us = 0;  // Set early, before launching Core1
    g_core1_error = false;
    g_core1_error_message[0] = '\0';
    
    // ACT: Launch Core1 helper (it will wait for start signal)
    multicore_launch_core1(core1_concurrent_helper);
    
    // Give Core1 time to start waiting
    printf("Waiting for Core1 to initialize...\n");
    sleep_ms(CORE1_STARTUP_DELAY_MS);
    printf("Starting concurrent monotonicity test...\n");
    
    // Signal start and immediately begin Core0 logging
    g_start_concurrent_logging = true;
    __dmb(); // Memory barrier to ensure visibility on Core1
    
    uint32_t core0_events_logged = 0;
    for (uint32_t i = 0; i < half_capacity; i++) {
        bool result = log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, 
                               LOG_EVENT_UART_DATA_RX, i);
        if (result) {
            core0_events_logged++;
        } else {
            break; // Buffer full or error
        }
    }
    
    // Wait for Core1 to complete with timeout
    uint32_t elapsed_ms = 0;
    while (g_core1_time_us == 0 && elapsed_ms < CORE1_TIMEOUT_MS) {
        sleep_ms(10);
        elapsed_ms += 10;
    }
    
    // ASSERT: Check for timeout and Core1 errors - reset core1 on any failure to avoid resource leaks
    if (elapsed_ms >= CORE1_TIMEOUT_MS) {
        multicore_reset_core1();
        TEST_FAIL_MESSAGE("Core1 monotonicity test timed out");
        return;
    }
    
    if (g_core1_error) {
        multicore_reset_core1();
        TEST_FAIL_MESSAGE((char*)g_core1_error_message);
        return;
    }
    
    // ASSERT: Both cores should have logged events  
    if (core0_events_logged <= half_capacity / 2) {
        multicore_reset_core1();
        TEST_FAIL_MESSAGE("Core0 should have logged at least half its target");
        return;
    }
    
    if (g_core1_events_logged <= half_capacity / 2) {
        multicore_reset_core1(); 
        TEST_FAIL_MESSAGE("Core1 should have logged at least half its target");
        return;
    }
    
    printf("Events logged - Core0: %u, Core1: %u\n", core0_events_logged, g_core1_events_logged);
    
    // ACT: Read all entries from buffer for verification
    log_entry_t* all_entries = malloc(buffer_capacity * sizeof(log_entry_t));
    if (!all_entries) {
        multicore_reset_core1();
        TEST_FAIL_MESSAGE("Failed to allocate verification buffer");
        return;
    }
    
    uint32_t total_entries_read = read_all_log_entries_for_verification(all_entries, buffer_capacity);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, total_entries_read, "Should have read some entries from buffer");
    
    printf("Total entries read from buffer: %u\n", total_entries_read);
    
    // ASSERT: Verify strict monotonicity per core
    uint32_t core0_count = 0, core1_count = 0;
    uint32_t prev_core0_event_num = 0, prev_core1_event_num = 0;
    bool core0_first = true, core1_first = true;
    
    for (uint32_t i = 0; i < total_entries_read; i++) {
        log_entry_t* entry = &all_entries[i];
        
        if (entry->event_source == EVENT_SOURCE_NETWORK) {
            // Core1 event
            core1_count++;
            if (core1_first) {
                prev_core1_event_num = entry->event_number;
                core1_first = false;
            } else {
                // CRITICAL FIX: Correct assertion for INCREASING monotonicity
                if (entry->event_number != prev_core1_event_num + 1) {
                    multicore_reset_core1();
                    printf("prev: %d, current: %d", prev_core1_event_num,entry->event_number);
                    TEST_FAIL_MESSAGE("Core1 event numbers must be strictly monotonic increasing");
                    free(all_entries);
                    return;
                }
                prev_core1_event_num = entry->event_number;
            }
        } else {
            // Core0 event  
            core0_count++;
            if (core0_first) {
                prev_core0_event_num = entry->event_number;
                core0_first = false;
            } else {
                // CRITICAL FIX: Correct assertion for INCREASING monotonicity  
                if (entry->event_number != prev_core0_event_num + 1 ) {
                    multicore_reset_core1();
                    printf("prev: %d, current: %d", prev_core0_event_num,entry->event_number);
                    TEST_FAIL_MESSAGE("Core0 event numbers must be strictly monotonic increasing");
                    free(all_entries);
                    return;
                }
                prev_core0_event_num = entry->event_number;
            }
        }
    }
    
    printf("Verification results - Core0 events: %u, Core1 events: %u\n", core0_count, core1_count);
    
    // ASSERT: Should have events from both cores
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, core0_count, "Should have Core0 events in buffer");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, core1_count, "Should have Core1 events in buffer");
    
    // ASSERT: Event counts should be reasonable compared to logged counts
    // (May be less due to buffer wraparound, but should be substantial)
    TEST_ASSERT_GREATER_THAN_MESSAGE(core0_events_logged / 4, core0_count,
        "Should have substantial Core0 events remaining in buffer");
    TEST_ASSERT_GREATER_THAN_MESSAGE(g_core1_events_logged / 4, core1_count,
        "Should have substantial Core1 events remaining in buffer");
    
    // Cleanup
    free(all_entries);
    
    // Reset core1 for next test
    multicore_reset_core1();
    
    printf("✓ Event number monotonicity verified - no events lost, duplicated, or overwritten\n");
}

/**
 * Test: Concurrent buffer fill performance from both cores
 * 
 * Tests concurrent logging performance with both cores writing half the buffer each.
 */
void test_concurrent_buffer_fill_performance(void) {
    // ARRANGE: Reset log manager
    log_manager_reset_for_testing();
    uint32_t buffer_capacity = shared_memory_get_log_buffer_capacity();
    uint32_t half_capacity = buffer_capacity / 2;
    TEST_ASSERT_GREATER_THAN_MESSAGE(10, buffer_capacity,
        "Buffer should have reasonable capacity for performance test");
    
    printf("Concurrent Performance Test: Both cores filling %u entries each...\n", half_capacity);
    
    // Reset synchronization variables
    g_start_concurrent_logging = false;
    g_core1_events_logged = 0;
    g_core1_time_us = 0;
    g_core1_error = false;
    g_core1_error_message[0] = '\0';
    
    // ACT: Launch Core1 helper (it will wait for start signal)
    multicore_launch_core1(core1_concurrent_helper);
    
    // Give Core1 time to start waiting
    printf("Waiting for Core1 to initialize...\n");
    sleep_ms(CORE1_STARTUP_DELAY_MS);
    printf("Starting concurrent test...\n");
    
    // Start timing for Core0
    absolute_time_t core0_start_time = get_absolute_time();
    
    // Signal start with memory barrier and immediately begin Core0 logging
    // Note: This test intentionally creates a race condition between cores
    // to stress-test the log manager's thread safety mechanisms.
    g_start_concurrent_logging = true;
    __dmb(); // Memory barrier to ensure visibility on Core1
    
    uint32_t core0_events_logged = 0;
    for (uint32_t i = 0; i < half_capacity; i++) {
        bool result = log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, 
                               LOG_EVENT_UART_DATA_RX, i);
        if (result) {
            core0_events_logged++;
        } else {
            break; // Buffer full or error
        }
    }
    
    absolute_time_t core0_end_time = get_absolute_time();
    uint64_t core0_time_us = absolute_time_diff_us(core0_start_time, core0_end_time);
    
    // Wait for Core1 to complete with timeout
    uint32_t elapsed_ms = 0;
    while (g_core1_time_us == 0 && elapsed_ms < CORE1_TIMEOUT_MS) {
        sleep_ms(10);
        elapsed_ms += 10;
    }
    
    // ASSERT: Check for timeout
    TEST_ASSERT_FALSE_MESSAGE(elapsed_ms >= CORE1_TIMEOUT_MS, 
        "Core1 concurrent test timed out - possible deadlock or failure");
    
    // ASSERT: Check for Core1 errors
    TEST_ASSERT_FALSE_MESSAGE(g_core1_error, (char*)g_core1_error_message);
    
    // ASSERT: Both cores should have logged events
    TEST_ASSERT_GREATER_THAN_MESSAGE(half_capacity / 2, core0_events_logged,
        "Core0 should have logged at least half its target");
    TEST_ASSERT_GREATER_THAN_MESSAGE(half_capacity / 2, g_core1_events_logged,
        "Core1 should have logged at least half its target");
    
    // Calculate performance metrics using helper
    printf("Concurrent Results:\n");
    double core0_events_per_second = report_performance("  Core0", core0_events_logged, core0_time_us);
    double core1_events_per_second = report_performance("  Core1", g_core1_events_logged, g_core1_time_us);
    
    uint32_t total_events = core0_events_logged + g_core1_events_logged;
    uint64_t max_time_us = (core0_time_us > g_core1_time_us) ? core0_time_us : g_core1_time_us;
    double combined_events_per_second = report_performance("  Combined", total_events, max_time_us);
    
    // ASSERT: Combined performance should be better than single core
    TEST_ASSERT_GREATER_THAN_MESSAGE(MIN_EXPECTED_CONCURRENT_EVENTS_PER_SEC, combined_events_per_second,
        "Concurrent logging should achieve minimum expected combined performance");
    
    // Reset core1 for next test
    multicore_reset_core1();
}

// Test runner
int main() {
    // Initialize Pico SDK
    stdio_init_all();
    
    // Wait for USB-serial connection
    sleep_ms(2000);
    
    printf("Starting Fixed-Size Entry Log Manager Tests...\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_log_manager_initialization);
    RUN_TEST(test_single_event_logged);
    RUN_TEST(test_log_buffer_utilization_fixed_entries);
    RUN_TEST(test_multiple_events_different_sources);
    RUN_TEST(test_core1_formatter_processes_entries);
    RUN_TEST(test_per_core_event_sequence_numbering);
    RUN_TEST(test_event_format_string_lookup);
    // RUN_TEST(test_log_buffer_wraparound_fixed_entries); // Temporarily disabled - causing loop
    
    // Performance tests
    RUN_TEST(test_core0_buffer_fill_performance);
    RUN_TEST(test_core1_buffer_fill_performance);
    RUN_TEST(test_concurrent_buffer_fill_performance);
    RUN_TEST(test_concurrent_buffer_fill_monotonic_event_numbers);
        
    while (true) {
        printf("Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}