/**
 * @file test_ringbuffer_debug.c
 * @brief Debug version of ring buffer tests with extensive printf debugging
 */

#include "pico/stdlib.h"
#include "ringbuffer.h"
#include <stdio.h>
#include <string.h>

// Simple test functions without Unity framework
static bool test_basic_init(void);
static bool test_basic_operations(void);

int main(void) {
    // Step 1: Initialize stdio
    printf("=== RINGBUFFER DEBUG TEST START ===\n");
    printf("Step 1: Before stdio_init_all()\n");
    
    stdio_init_all();
    
    printf("Step 2: After stdio_init_all(), waiting for USB...\n");
    sleep_ms(3000);  // Wait for USB-CDC to stabilize
    
    printf("Step 3: Testing basic functionality\n");
    
    // Step 2: Test ringbuffer initialization
    printf("Step 4: Before ringbuffer_init()\n");
    bool init_result = ringbuffer_init();
    printf("Step 5: After ringbuffer_init(), result = %s\n", init_result ? "SUCCESS" : "FAILED");
    
    if (!init_result) {
        printf("ERROR: Ringbuffer initialization failed!\n");
        while (true) {
            printf("STUCK: Init failed\n");
            sleep_ms(2000);
        }
    }
    
    // Step 3: Test basic operations
    printf("Step 6: Testing basic operations\n");
    if (test_basic_init()) {
        printf("Step 7: Basic init test PASSED\n");
    } else {
        printf("Step 7: Basic init test FAILED\n");
    }
    
    if (test_basic_operations()) {
        printf("Step 8: Basic operations test PASSED\n");
    } else {
        printf("Step 8: Basic operations test FAILED\n");
    }
    
    printf("=== ALL DEBUG TESTS COMPLETED ===\n");
    
    // Keep running with status
    uint32_t counter = 0;
    while (true) {
        printf("Debug test running... counter: %u\n", counter++);
        sleep_ms(3000);
    }
}

static bool test_basic_init(void) {
    printf("  test_basic_init: start\n");
    
    // Test capacity
    uint32_t capacity = ringbuffer_get_capacity();
    printf("  test_basic_init: capacity = %u\n", capacity);
    if (capacity == 0) {
        printf("  test_basic_init: ERROR - zero capacity\n");
        return false;
    }
    
    // Test free count
    uint32_t free_count = ringbuffer_get_free_count();
    printf("  test_basic_init: free_count = %u\n", free_count);
    if (free_count != capacity) {
        printf("  test_basic_init: ERROR - free_count != capacity\n");
        return false;
    }
    
    // Test empty counts
    uint32_t tcp_to_uart = ringbuffer_get_count(RX_TCP_TO_UART);
    uint32_t uart_to_tcp = ringbuffer_get_count(RX_UART_TO_TCP);
    printf("  test_basic_init: tcp_to_uart = %u, uart_to_tcp = %u\n", tcp_to_uart, uart_to_tcp);
    
    if (tcp_to_uart != 0 || uart_to_tcp != 0) {
        printf("  test_basic_init: ERROR - queues not empty\n");
        return false;
    }
    
    printf("  test_basic_init: SUCCESS\n");
    return true;
}

static bool test_basic_operations(void) {
    printf("  test_basic_operations: start\n");
    
    // Get a free entry
    printf("  test_basic_operations: getting free entry\n");
    ring_entry_t* entry = ringbuffer_get_free_entry();
    if (!entry) {
        printf("  test_basic_operations: ERROR - no free entry\n");
        return false;
    }
    printf("  test_basic_operations: got free entry at %p\n", (void*)entry);
    
    // Setup test message
    printf("  test_basic_operations: setting up message\n");
    const char* test_msg = "#1234Hello!\r\n";
    entry->direction = RX_TCP_TO_UART;
    entry->uart_channel = 0;
    entry->payload_length = strlen(test_msg);
    memcpy(entry->payload, test_msg, entry->payload_length);
    
    printf("  test_basic_operations: message setup complete, length = %u\n", entry->payload_length);
    
    // Enqueue the entry
    printf("  test_basic_operations: enqueuing entry\n");
    bool enqueue_result = ringbuffer_enqueue_entry(entry);
    if (!enqueue_result) {
        printf("  test_basic_operations: ERROR - enqueue failed\n");
        return false;
    }
    printf("  test_basic_operations: enqueue SUCCESS\n");
    
    // Check count
    uint32_t count = ringbuffer_get_count(RX_TCP_TO_UART);
    printf("  test_basic_operations: count after enqueue = %u\n", count);
    if (count != 1) {
        printf("  test_basic_operations: ERROR - count != 1\n");
        return false;
    }
    
    // Dequeue the entry
    printf("  test_basic_operations: dequeuing entry\n");
    ring_entry_t* dequeued = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
    if (!dequeued) {
        printf("  test_basic_operations: ERROR - dequeue failed\n");
        return false;
    }
    printf("  test_basic_operations: dequeue SUCCESS, got entry at %p\n", (void*)dequeued);
    
    // Verify it's the same entry
    if (dequeued != entry) {
        printf("  test_basic_operations: ERROR - different entry returned\n");
        return false;
    }
    
    // Mark as consumed
    printf("  test_basic_operations: marking as consumed\n");
    ringbuffer_mark_consumed(dequeued);
    
    // Check count is back to 0
    count = ringbuffer_get_count(RX_TCP_TO_UART);
    printf("  test_basic_operations: count after consume = %u\n", count);
    if (count != 0) {
        printf("  test_basic_operations: ERROR - count != 0 after consume\n");
        return false;
    }
    
    printf("  test_basic_operations: SUCCESS\n");
    return true;
}
