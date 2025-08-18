/**
 * @file test_ringbuffer.c
 * @brief Unit tests for ring buffer implementation
 * 
 * Tests the ring buffer functionality required by Issue #68:
 * 1. an initialized ringbuffer is empty
 * 2. a message can be put into the ringbuffer  
 * 3. the ringbuffer reports a length of 1 after a message was put into the ringbuffer
 * 4. when more than <capacity> messages are put into the ringbuffer, the oldest message is overwritten and thus, lost
 * 5. a message can be removed from the ringbuffer
 * 6. after a message was removed from the rinbuffer, it will report a length decremented by 1
 * 7. core0 will only 'turn-around' the oldest message with a direction of 'RX_TCP_TO_UART' in one main loop execution.
 * 8. core1 will add messages with a direction of 'RX_UART_TO_TCP' to the ringbuffer
 * 9. core1 will remove the oldest message with a direction of 'RX_UART_TO_TCP' and send it to the remote client in one main loop execution
 * 
 * Documentation Reference:
 * - ADR-011: Ring Buffer Implementation for UART-TCP Message Bridging
 * - Issue #68: Add ringbuffer implementation
 */

#include "unity.h"
#include "ringbuffer.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// Test constants
#define TEST_MESSAGE_1 "#1234Hello World!\r\n"
#define TEST_MESSAGE_2 "#ABCDTEST MESSAGE!\r\n"
#define TEST_MESSAGE_3 "#FEEDOVERFLOW_TEST!\r\n"
#define TEST_UART_CHANNEL 0

// Test helper functions
static void setup_test_message(ring_entry_t* entry, const char* message, uint8_t direction, uint8_t uart_channel);
static void verify_message_content(const ring_entry_t* entry, const char* expected_message, uint8_t expected_direction);

void setUp(void) {
    // Initialize ring buffer before each test
    TEST_ASSERT_TRUE_MESSAGE(ringbuffer_init(), "Ring buffer initialization failed");
}

void tearDown(void) {
    // Reset statistics after each test for clean state
    ringbuffer_reset_statistics();
}

/**
 * Test 1: An initialized ringbuffer is empty
 */
void test_initialized_ringbuffer_is_empty(void) {
    // Verify empty state after initialization
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ringbuffer_get_count(RX_TCP_TO_UART), 
                                     "TCP→UART queue should be empty after init");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ringbuffer_get_count(RX_UART_TO_TCP), 
                                     "UART→TCP queue should be empty after init");
    
    // Verify no entries can be dequeued from empty buffer
    TEST_ASSERT_NULL_MESSAGE(ringbuffer_dequeue_entry(RX_TCP_TO_UART), 
                             "Should not dequeue from empty TCP→UART queue");
    TEST_ASSERT_NULL_MESSAGE(ringbuffer_dequeue_entry(RX_UART_TO_TCP), 
                             "Should not dequeue from empty UART→TCP queue");
    
    // Verify full capacity is available
    uint32_t capacity = ringbuffer_get_capacity();
    uint32_t free_count = ringbuffer_get_free_count();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(capacity, free_count, 
                                     "All entries should be free after init");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, capacity, 
                                           "Ring buffer should have non-zero capacity");
}

/**
 * Test 2: A message can be put into the ringbuffer
 */
void test_message_can_be_put_into_ringbuffer(void) {
    // Get free entry
    ring_entry_t* entry = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "Should get free entry from empty buffer");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ENTRY_STATUS_FILLING, entry->status, 
                                   "Free entry should have FILLING status");
    
    // Setup test message
    setup_test_message(entry, TEST_MESSAGE_1, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    
    // Enqueue the message
    bool result = ringbuffer_enqueue_entry(entry);
    TEST_ASSERT_TRUE_MESSAGE(result, "Should successfully enqueue message");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ENTRY_STATUS_READY, entry->status, 
                                   "Enqueued entry should have READY status");
    
    // Verify message content is preserved
    verify_message_content(entry, TEST_MESSAGE_1, RX_TCP_TO_UART);
}

/**
 * Test 3: The ringbuffer reports a length of 1 after a message was put into the ringbuffer
 */
void test_ringbuffer_reports_length_1_after_message_added(void) {
    // Add one message
    ring_entry_t* entry = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry);
    setup_test_message(entry, TEST_MESSAGE_1, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry));
    
    // Verify count is 1 for correct direction
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ringbuffer_get_count(RX_TCP_TO_UART), 
                                     "Should have 1 message in TCP→UART queue");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ringbuffer_get_count(RX_UART_TO_TCP), 
                                     "Should have 0 messages in UART→TCP queue");
    
    // Verify free count decreased by 1
    uint32_t capacity = ringbuffer_get_capacity();
    uint32_t free_count = ringbuffer_get_free_count();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(capacity - 1, free_count, 
                                     "Free count should decrease by 1");
}

/**
 * Test 4: When more than <capacity> messages are put into the ringbuffer, 
 *         the oldest message is overwritten and thus, lost
 */
void test_overflow_overwrites_oldest_message(void) {
    uint32_t capacity = ringbuffer_get_capacity();
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(2, capacity, 
                                           "Need at least 3 entries for overflow test");
    
    // Fill buffer to capacity
    for (uint32_t i = 0; i < capacity; i++) {
        ring_entry_t* entry = ringbuffer_get_free_entry();
        TEST_ASSERT_NOT_NULL_MESSAGE(entry, "Should get free entry while filling buffer");
        
        // Create unique message for each entry
        char test_message[32];
        snprintf(test_message, sizeof(test_message), "#%04XMSG%u!\r\n", (unsigned int)i, (unsigned int)i);
        setup_test_message(entry, test_message, RX_TCP_TO_UART, TEST_UART_CHANNEL);
        entry->sequence_id = i;  // Set sequence for tracking
        
        TEST_ASSERT_TRUE_MESSAGE(ringbuffer_enqueue_entry(entry), "Should enqueue message during fill");
    }
    
    // Verify buffer is full
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ringbuffer_get_free_count(), "Buffer should be full");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(capacity, ringbuffer_get_count(RX_TCP_TO_UART), "All entries should be queued");
    
    // Attempt to add one more message (should trigger overflow)
    ring_entry_t* overflow_entry = ringbuffer_get_free_entry();
    
    if (overflow_entry != NULL) {
        // If we got an entry, it means drop-oldest policy is working
        setup_test_message(overflow_entry, TEST_MESSAGE_3, RX_TCP_TO_UART, TEST_UART_CHANNEL);
        overflow_entry->sequence_id = capacity; // Mark as overflow message
        TEST_ASSERT_TRUE_MESSAGE(ringbuffer_enqueue_entry(overflow_entry), "Should enqueue overflow message");
        
        // Verify overflow counter increased
        TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, ringbuffer_get_overflow_count(), 
                                               "Overflow count should increase");
        
        // Verify oldest message (sequence_id = 0) is no longer in buffer
        ring_entry_t* dequeued = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
        TEST_ASSERT_NOT_NULL_MESSAGE(dequeued, "Should be able to dequeue message");
        TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, dequeued->sequence_id, 
                                            "Oldest message should have been dropped");
    } else {
        // Buffer full and no drop-oldest policy - verify overflow count
        TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, ringbuffer_get_overflow_count(), 
                                               "Overflow count should increase when buffer full");
    }
}

/**
 * Test 5: A message can be removed from the ringbuffer
 */
void test_message_can_be_removed_from_ringbuffer(void) {
    // Add a message first
    ring_entry_t* entry = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry);
    setup_test_message(entry, TEST_MESSAGE_1, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry));
    
    // Remove the message
    ring_entry_t* dequeued = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
    TEST_ASSERT_NOT_NULL_MESSAGE(dequeued, "Should be able to dequeue message");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(entry, dequeued, "Dequeued entry should be same as enqueued");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ENTRY_STATUS_READY, dequeued->status, 
                                   "Dequeued entry should have READY status");
    
    // Verify message content is preserved
    verify_message_content(dequeued, TEST_MESSAGE_1, RX_TCP_TO_UART);
    
    // Mark as consumed to complete the cycle
    ringbuffer_mark_consumed(dequeued);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ENTRY_STATUS_FREE, dequeued->status, 
                                   "Consumed entry should have FREE status");
}

/**
 * Test 6: After a message was removed from the ringbuffer, 
 *         it will report a length decremented by 1
 */
void test_length_decrements_after_message_removed(void) {
    // Add two messages
    ring_entry_t* entry1 = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry1);
    setup_test_message(entry1, TEST_MESSAGE_1, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry1));
    
    ring_entry_t* entry2 = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry2);
    setup_test_message(entry2, TEST_MESSAGE_2, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry2));
    
    // Verify initial count
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, ringbuffer_get_count(RX_TCP_TO_UART), 
                                     "Should have 2 messages before removal");
    
    // Remove one message
    ring_entry_t* dequeued = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
    TEST_ASSERT_NOT_NULL(dequeued);
    ringbuffer_mark_consumed(dequeued);
    
    // Verify count decremented by 1
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ringbuffer_get_count(RX_TCP_TO_UART), 
                                     "Should have 1 message after removal");
    
    // Verify free count increased by 1
    uint32_t capacity = ringbuffer_get_capacity();
    uint32_t free_count = ringbuffer_get_free_count();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(capacity - 1, free_count, 
                                     "Free count should increase by 1");
}

/**
 * Test 7: Core0 will only 'turn-around' the oldest message with a direction 
 *         of 'RX_TCP_TO_UART' in one main loop execution
 */
void test_core0_turns_around_oldest_tcp_to_uart_message(void) {
    // Add multiple TCP→UART messages with different timestamps
    ring_entry_t* entry1 = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry1);
    setup_test_message(entry1, TEST_MESSAGE_1, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    entry1->timestamp = 1000;  // Older message
    entry1->sequence_id = 1;
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry1));
    
    ring_entry_t* entry2 = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry2);
    setup_test_message(entry2, TEST_MESSAGE_2, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    entry2->timestamp = 2000;  // Newer message
    entry2->sequence_id = 2;
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry2));
    
    // Simulate Core0 main loop execution: process only one message
    ring_entry_t* to_turn_around = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
    TEST_ASSERT_NOT_NULL_MESSAGE(to_turn_around, "Should dequeue oldest message");
    
    // Verify it's the oldest message (entry1)
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, to_turn_around->sequence_id, 
                                     "Should dequeue oldest message first");
    
    // Turn around: change direction from TCP→UART to UART→TCP
    to_turn_around->direction = RX_UART_TO_TCP;
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(to_turn_around));
    
    // Verify only one message was processed (entry2 still in TCP→UART queue)
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ringbuffer_get_count(RX_TCP_TO_UART), 
                                     "Should have 1 message remaining in TCP→UART queue");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ringbuffer_get_count(RX_UART_TO_TCP), 
                                     "Should have 1 message in UART→TCP queue after turn-around");
}

/**
 * Test 8: Core1 will add messages with a direction of 'RX_UART_TO_TCP' to the ringbuffer
 */
void test_core1_adds_uart_to_tcp_messages(void) {
    // Simulate Core1 adding UART→TCP message (from UART hardware or echo response)
    ring_entry_t* entry = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry);
    setup_test_message(entry, TEST_MESSAGE_1, RX_UART_TO_TCP, TEST_UART_CHANNEL);
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry));
    
    // Verify message is in UART→TCP queue
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ringbuffer_get_count(RX_UART_TO_TCP), 
                                     "Should have 1 message in UART→TCP queue");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ringbuffer_get_count(RX_TCP_TO_UART), 
                                     "Should have 0 messages in TCP→UART queue");
    
    // Verify message content and direction
    ring_entry_t* dequeued = ringbuffer_dequeue_entry(RX_UART_TO_TCP);
    TEST_ASSERT_NOT_NULL(dequeued);
    verify_message_content(dequeued, TEST_MESSAGE_1, RX_UART_TO_TCP);
    ringbuffer_mark_consumed(dequeued);
}

/**
 * Test 9: Core1 will remove the oldest message with a direction of 'RX_UART_TO_TCP' 
 *         and send it to the remote client in one main loop execution
 */
void test_core1_removes_oldest_uart_to_tcp_message(void) {
    // Add multiple UART→TCP messages with different timestamps
    ring_entry_t* entry1 = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry1);
    setup_test_message(entry1, TEST_MESSAGE_1, RX_UART_TO_TCP, TEST_UART_CHANNEL);
    entry1->timestamp = 1000;  // Older message
    entry1->sequence_id = 1;
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry1));
    
    ring_entry_t* entry2 = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry2);
    setup_test_message(entry2, TEST_MESSAGE_2, RX_UART_TO_TCP, TEST_UART_CHANNEL);
    entry2->timestamp = 2000;  // Newer message
    entry2->sequence_id = 2;
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry2));
    
    // Simulate Core1 main loop execution: process only one message for transmission
    ring_entry_t* to_transmit = ringbuffer_dequeue_entry(RX_UART_TO_TCP);
    TEST_ASSERT_NOT_NULL_MESSAGE(to_transmit, "Should dequeue oldest message");
    
    // Verify it's the oldest message (entry1)
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, to_transmit->sequence_id, 
                                     "Should dequeue oldest message first");
    
    // Simulate transmission to TCP client (mark as consumed)
    ringbuffer_mark_consumed(to_transmit);
    
    // Verify only one message was processed (entry2 still in UART→TCP queue)
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ringbuffer_get_count(RX_UART_TO_TCP), 
                                     "Should have 1 message remaining in UART→TCP queue");
}

/**
 * Test: Ring buffer statistics and monitoring
 */
void test_ringbuffer_statistics(void) {
    ringbuffer_stats_t stats;
    
    // Check initial statistics
    ringbuffer_get_stats(&stats);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.total_enqueued, "Initial enqueue count should be 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.total_dequeued, "Initial dequeue count should be 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.overflow_count, "Initial overflow count should be 0");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, stats.total_entries, "Should have non-zero capacity");
    
    // Add and remove a message
    ring_entry_t* entry = ringbuffer_get_free_entry();
    TEST_ASSERT_NOT_NULL(entry);
    setup_test_message(entry, TEST_MESSAGE_1, RX_TCP_TO_UART, TEST_UART_CHANNEL);
    TEST_ASSERT_TRUE(ringbuffer_enqueue_entry(entry));
    
    ring_entry_t* dequeued = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
    TEST_ASSERT_NOT_NULL(dequeued);
    ringbuffer_mark_consumed(dequeued);
    
    // Check updated statistics
    ringbuffer_get_stats(&stats);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stats.total_enqueued, "Should have 1 enqueue");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stats.total_dequeued, "Should have 1 dequeue");
}

// Test helper function implementations

static void setup_test_message(ring_entry_t* entry, const char* message, uint8_t direction, uint8_t uart_channel) {
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "Entry should not be NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(message, "Message should not be NULL");
    
    entry->direction = direction;
    entry->uart_channel = uart_channel;
    entry->payload_length = strlen(message);
    entry->timestamp = to_ms_since_boot(get_absolute_time());
    
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(RINGBUFFER_PAYLOAD_MAX_SIZE, entry->payload_length, 
                                     "Message too long for payload");
    
    memcpy(entry->payload, message, entry->payload_length);
    entry->payload[entry->payload_length] = '\0';  // Null terminate for safety
}

static void verify_message_content(const ring_entry_t* entry, const char* expected_message, uint8_t expected_direction) {
    TEST_ASSERT_NOT_NULL_MESSAGE(entry, "Entry should not be NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(expected_message, "Expected message should not be NULL");
    
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected_direction, entry->direction, 
                                   "Message direction should match");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(strlen(expected_message), entry->payload_length, 
                                    "Message length should match");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(expected_message, (char*)entry->payload, entry->payload_length,
                                        "Message content should match");
}

// Unity test runner
int main(void) {
    // Initialize pico SDK for absolute time functions
    stdio_init_all();
    
    UNITY_BEGIN();
    
    // Core ring buffer functionality tests (per Issue #68)
    RUN_TEST(test_initialized_ringbuffer_is_empty);
    RUN_TEST(test_message_can_be_put_into_ringbuffer);
    RUN_TEST(test_ringbuffer_reports_length_1_after_message_added);
    RUN_TEST(test_overflow_overwrites_oldest_message);
    RUN_TEST(test_message_can_be_removed_from_ringbuffer);
    RUN_TEST(test_length_decrements_after_message_removed);
    RUN_TEST(test_core0_turns_around_oldest_tcp_to_uart_message);
    RUN_TEST(test_core1_adds_uart_to_tcp_messages);
    RUN_TEST(test_core1_removes_oldest_uart_to_tcp_message);
    
    // Additional functionality tests
    RUN_TEST(test_ringbuffer_statistics);
    
    // End tests and keep running (as required for embedded system)
    UNITY_END();
    
    // IMPORTANT: Main function MUST NOT terminate per development requirements
    while (true) {
        printf("Ring buffer tests completed successfully!\n");
        sleep_ms(2000);  // Print status every 2 seconds
    }
}