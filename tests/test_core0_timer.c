/**
 * @file test_core0_timer.c
 * @brief Unit tests for Core0 Timer Subsystem
 * 
 * Tests the Core0 timer subsystem functionality including timer setting,
 * expiration checking, cancellation, and hardware timer alarm integration.
 * 
 * Documentation Reference:
 * - ADR-009: Per-Core Timer Subsystem Architecture
 * - arc42 Chapter 5 - Core 0 Timer Subsystem Whitebox
 */

#include "unity.h"
#include "core0_timer.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include <stdio.h>

// Test setup and teardown
void setUp(void) {
    // Initialize Core0 timer subsystem for each test
    core0_timer_init();
}

void tearDown(void) {
    // Clean up after each test
    core0_timer_cleanup();
}

/**
 * @brief Test Core0 timer initialization
 * 
 * Verifies that the timer subsystem initializes correctly and
 * all timers are in the expected initial state.
 */
void test_core0_timer_init(void) {
    // Test that initialization succeeds
    bool result = core0_timer_init();
    TEST_ASSERT_TRUE(result);
    
    // Test that all timers are initially not active and not expired
    for (int i = 0; i < CORE0_MAX_TIMERS; i++) {
        TEST_ASSERT_FALSE(core0_timer_is_expired((core0_timer_id_t)i));
        TEST_ASSERT_FALSE(core0_timer_is_active((core0_timer_id_t)i));
    }
}

/**
 * @brief Test setting a single timer
 * 
 * Verifies that a timer can be set with a specific interval
 * and becomes active but not expired initially.
 */
void test_core0_timer_set_single(void) {
    core0_timer_id_t timer_id = CORE0_TIMER_UART0_TIMEOUT;
    uint32_t interval_ms = 100;
    
    // Set the timer
    core0_timer_set(timer_id, interval_ms);
    
    // Timer should be active but not expired
    TEST_ASSERT_TRUE(core0_timer_is_active(timer_id));
    TEST_ASSERT_FALSE(core0_timer_is_expired(timer_id));
}

/**
 * @brief Test timer expiration after waiting
 * 
 * Verifies that a timer correctly expires after its interval.
 * This test requires actual time passage.
 */
void test_core0_timer_expiration(void) {
    core0_timer_id_t timer_id = CORE0_TIMER_UART1_TIMEOUT;
    uint32_t interval_ms = 50;  // Short interval for test
    
    // Set the timer
    core0_timer_set(timer_id, interval_ms);
    
    // Timer should not be expired immediately
    TEST_ASSERT_FALSE(core0_timer_is_expired(timer_id));
    
    // Wait for expiration
    sleep_ms(interval_ms + 10);  // Extra 10ms for safety
    
    // Timer should now be expired
    TEST_ASSERT_TRUE(core0_timer_is_expired(timer_id));
}

/**
 * @brief Test timer cancellation
 * 
 * Verifies that a timer can be cancelled and becomes inactive.
 */
void test_core0_timer_cancel(void) {
    core0_timer_id_t timer_id = CORE0_TIMER_UART2_TIMEOUT;
    uint32_t interval_ms = 100;
    
    // Set the timer
    core0_timer_set(timer_id, interval_ms);
    TEST_ASSERT_TRUE(core0_timer_is_active(timer_id));
    
    // Cancel the timer
    core0_timer_cancel(timer_id);
    
    // Timer should no longer be active
    TEST_ASSERT_FALSE(core0_timer_is_active(timer_id));
    TEST_ASSERT_FALSE(core0_timer_is_expired(timer_id));
}

/**
 * @brief Test multiple concurrent timers
 * 
 * Verifies that multiple timers can be active simultaneously
 * and expire independently.
 */
void test_core0_timer_multiple_concurrent(void) {
    // Set multiple timers with different intervals
    core0_timer_set(CORE0_TIMER_UART0_TIMEOUT, 30);
    core0_timer_set(CORE0_TIMER_UART1_TIMEOUT, 60);
    core0_timer_set(CORE0_TIMER_UART2_TIMEOUT, 90);
    
    // All should be active initially
    TEST_ASSERT_TRUE(core0_timer_is_active(CORE0_TIMER_UART0_TIMEOUT));
    TEST_ASSERT_TRUE(core0_timer_is_active(CORE0_TIMER_UART1_TIMEOUT));
    TEST_ASSERT_TRUE(core0_timer_is_active(CORE0_TIMER_UART2_TIMEOUT));
    
    // None should be expired initially
    TEST_ASSERT_FALSE(core0_timer_is_expired(CORE0_TIMER_UART0_TIMEOUT));
    TEST_ASSERT_FALSE(core0_timer_is_expired(CORE0_TIMER_UART1_TIMEOUT));
    TEST_ASSERT_FALSE(core0_timer_is_expired(CORE0_TIMER_UART2_TIMEOUT));
    
    // Wait for first timer to expire
    sleep_ms(40);
    
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART0_TIMEOUT));
    TEST_ASSERT_FALSE(core0_timer_is_expired(CORE0_TIMER_UART1_TIMEOUT));
    TEST_ASSERT_FALSE(core0_timer_is_expired(CORE0_TIMER_UART2_TIMEOUT));
    
    // Wait for second timer to expire  
    sleep_ms(30);
    
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART0_TIMEOUT));
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART1_TIMEOUT));
    TEST_ASSERT_FALSE(core0_timer_is_expired(CORE0_TIMER_UART2_TIMEOUT));
    
    // Wait for third timer to expire
    sleep_ms(30);
    
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART0_TIMEOUT));
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART1_TIMEOUT));
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART2_TIMEOUT));
}

/**
 * @brief Test timer reset functionality
 * 
 * Verifies that a timer can be reset with a new interval
 * while it's active.
 */
void test_core0_timer_reset(void) {
    core0_timer_id_t timer_id = CORE0_TIMER_PROTOCOL_DELAY;
    
    // Set initial timer
    core0_timer_set(timer_id, 100);
    TEST_ASSERT_TRUE(core0_timer_is_active(timer_id));
    
    // Wait a bit
    sleep_ms(20);
    
    // Reset with new interval
    core0_timer_set(timer_id, 50);
    TEST_ASSERT_TRUE(core0_timer_is_active(timer_id));
    TEST_ASSERT_FALSE(core0_timer_is_expired(timer_id));
    
    // Wait for new interval to expire
    sleep_ms(60);
    TEST_ASSERT_TRUE(core0_timer_is_expired(timer_id));
}

/**
 * @brief Test maximum timer limit
 * 
 * Verifies that the system correctly handles the maximum
 * number of concurrent timers.
 */
void test_core0_timer_maximum_timers(void) {
    // Set all available timers
    for (int i = 0; i < CORE0_MAX_TIMERS; i++) {
        core0_timer_set((core0_timer_id_t)i, 100 + i * 10);
        TEST_ASSERT_TRUE(core0_timer_is_active((core0_timer_id_t)i));
    }
    
    // Verify all timers are active
    for (int i = 0; i < CORE0_MAX_TIMERS; i++) {
        TEST_ASSERT_TRUE(core0_timer_is_active((core0_timer_id_t)i));
    }
}

/**
 * @brief Test invalid timer ID handling
 * 
 * Verifies that invalid timer IDs are handled gracefully.
 */
void test_core0_timer_invalid_id(void) {
    core0_timer_id_t invalid_id = (core0_timer_id_t)CORE0_MAX_TIMERS;
    
    // Operations with invalid ID should not crash and return sensible values
    core0_timer_set(invalid_id, 100);  // Should be ignored
    TEST_ASSERT_FALSE(core0_timer_is_active(invalid_id));
    TEST_ASSERT_FALSE(core0_timer_is_expired(invalid_id));
}

/**
 * @brief Test zero interval timer
 * 
 * Verifies that setting a timer with zero interval
 * results in immediate expiration.
 */
void test_core0_timer_zero_interval(void) {
    core0_timer_id_t timer_id = CORE0_TIMER_RETRANSMIT;
    
    // Set timer with zero interval
    core0_timer_set(timer_id, 0);
    
    // Should be expired immediately
    TEST_ASSERT_TRUE(core0_timer_is_expired(timer_id));
}

/**
 * @brief Test timer statistics
 * 
 * Verifies that timer subsystem maintains correct statistics
 * about active and expired timers.
 */
void test_core0_timer_statistics(void) {
    // Initially no active timers
    TEST_ASSERT_EQUAL(0, core0_timer_get_active_count());
    
    // Set some timers
    core0_timer_set(CORE0_TIMER_UART0_TIMEOUT, 100);
    core0_timer_set(CORE0_TIMER_UART1_TIMEOUT, 200);
    
    TEST_ASSERT_EQUAL(2, core0_timer_get_active_count());
    
    // Cancel one timer
    core0_timer_cancel(CORE0_TIMER_UART0_TIMEOUT);
    
    TEST_ASSERT_EQUAL(1, core0_timer_get_active_count());
}

/**
 * @brief Test that a target already in the past still expires
 *
 * ADR-009, "Timer instance and IRQ line": if the alarm target is missed
 * (interval shorter than the time to arm it), the module forces the IRQ so the
 * ISR expires the timer immediately instead of waiting for a counter wrap.
 * Also guards against an IRQ storm from a forced IRQ left asserted.
 */
void test_core0_timer_missed_target_expires(void) {
    core0_timer_set(CORE0_TIMER_UART0_TIMEOUT, 1);
    sleep_ms(5);
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART0_TIMEOUT));

    // A second timer afterwards must still work normally (no stuck forced IRQ)
    core0_timer_set(CORE0_TIMER_UART1_TIMEOUT, 30);
    TEST_ASSERT_FALSE(core0_timer_is_expired(CORE0_TIMER_UART1_TIMEOUT));
    sleep_ms(40);
    TEST_ASSERT_TRUE(core0_timer_is_expired(CORE0_TIMER_UART1_TIMEOUT));
}

// Unity test runner
int main(void) {
    stdio_init_all();
    
    UNITY_BEGIN();
    
    RUN_TEST(test_core0_timer_init);
    RUN_TEST(test_core0_timer_set_single);
    RUN_TEST(test_core0_timer_expiration);
    RUN_TEST(test_core0_timer_cancel);
    RUN_TEST(test_core0_timer_multiple_concurrent);
    RUN_TEST(test_core0_timer_reset);
    RUN_TEST(test_core0_timer_maximum_timers);
    RUN_TEST(test_core0_timer_invalid_id);
    RUN_TEST(test_core0_timer_zero_interval);
    RUN_TEST(test_core0_timer_statistics);
    RUN_TEST(test_core0_timer_missed_target_expires);
    
    while (true) {
        printf("Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}
