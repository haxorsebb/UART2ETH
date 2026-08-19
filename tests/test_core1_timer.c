/**
 * @file test_core1_timer.c
 * @brief Unit tests for Core1 Timer Subsystem
 * 
 * Tests the Core1 timer subsystem functionality including timer setting,
 * expiration checking, cancellation, and hardware timer alarm integration.
 * 
 * Documentation Reference:
 * - ADR-009: Per-Core Timer Subsystem Architecture
 * - arc42 Chapter 5 - Core 1 Timer Subsystem Whitebox
 */

#include "unity.h"
#include "core1_timer.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include <stdio.h>

// Test setup and teardown
void setUp(void) {
    // Initialize Core1 timer subsystem for each test
    core1_timer_init();
}

void tearDown(void) {
    // Clean up after each test
    core1_timer_cleanup();
}

/**
 * @brief Test Core1 timer initialization
 * 
 * Verifies that the timer subsystem initializes correctly and
 * all timers are in the expected initial state.
 */
void test_core1_timer_init(void) {
    // Test that initialization succeeds
    bool result = core1_timer_init();
    TEST_ASSERT_TRUE(result);
    
    // Test that all timers are initially not active and not expired
    for (int i = 0; i < CORE1_MAX_TIMERS; i++) {
        TEST_ASSERT_FALSE(core1_timer_is_expired((core1_timer_id_t)i));
        TEST_ASSERT_FALSE(core1_timer_is_active((core1_timer_id_t)i));
    }
}

/**
 * @brief Test setting network timers
 * 
 * Verifies that network-specific timers can be set and work correctly.
 */
void test_core1_timer_network_timers(void) {
    // Test DHCP renewal timer
    core1_timer_set(CORE1_TIMER_DHCP_RENEWAL, 500);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_DHCP_RENEWAL));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_DHCP_RENEWAL));
    
    // Test connection timeout timer
    core1_timer_set(CORE1_TIMER_CONNECTION_TIMEOUT, 200);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_CONNECTION_TIMEOUT));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_CONNECTION_TIMEOUT));
    
    // Test network timeout
    core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, 300);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_NETWORK_TIMEOUT));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT));
}

/**
 * @brief Test maintenance timers
 * 
 * Verifies that maintenance-specific timers function correctly.
 */
void test_core1_timer_maintenance_timers(void) {
    // Test persistence interval timer
    core1_timer_set(CORE1_TIMER_PERSISTENCE_INTERVAL, 1000);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_PERSISTENCE_INTERVAL));
    
    // Test log flush timer
    core1_timer_set(CORE1_TIMER_LOG_FLUSH, 100);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_LOG_FLUSH));
    
    // Test maintenance cycle timer
    core1_timer_set(CORE1_TIMER_MAINTENANCE_CYCLE, 5000);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_MAINTENANCE_CYCLE));
}

/**
 * @brief Test large timer array capacity
 * 
 * Verifies that Core1 can handle its larger timer array (16 timers).
 */
void test_core1_timer_large_capacity(void) {
    // Set all 16 timers with different intervals
    for (int i = 0; i < CORE1_MAX_TIMERS; i++) {
        core1_timer_set((core1_timer_id_t)i, 100 + i * 50);
        TEST_ASSERT_TRUE(core1_timer_is_active((core1_timer_id_t)i));
    }
    
    // Verify all timers are active
    TEST_ASSERT_EQUAL(CORE1_MAX_TIMERS, core1_timer_get_active_count());
    
    // Cancel half of them
    for (int i = 0; i < CORE1_MAX_TIMERS / 2; i++) {
        core1_timer_cancel((core1_timer_id_t)i);
    }
    
    // Verify correct count
    TEST_ASSERT_EQUAL(CORE1_MAX_TIMERS / 2, core1_timer_get_active_count());
}

/**
 * @brief Test long interval timers
 * 
 * Verifies that Core1 can handle longer intervals typical for network operations.
 */
void test_core1_timer_long_intervals(void) {
    // Test very long DHCP renewal timer (simulating hours)
    core1_timer_set(CORE1_TIMER_DHCP_RENEWAL, 3600000);  // 1 hour in ms
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_DHCP_RENEWAL));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_DHCP_RENEWAL));
    
    // Test medium interval maintenance timer
    core1_timer_set(CORE1_TIMER_MAINTENANCE_CYCLE, 60000);  // 1 minute
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_MAINTENANCE_CYCLE));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_MAINTENANCE_CYCLE));
}

/**
 * @brief Test timer expiration with network-typical intervals
 * 
 * Verifies timer expiration with intervals typical for network operations.
 */
void test_core1_timer_network_expiration(void) {
    // Set short network timeout for testing
    core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, 50);
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT));
    
    // Wait for expiration
    sleep_ms(60);
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT));
    
    // Set connection timeout timer
    core1_timer_set(CORE1_TIMER_CONNECTION_TIMEOUT, 80);
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_CONNECTION_TIMEOUT));
    
    // Wait for expiration
    sleep_ms(90);
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_CONNECTION_TIMEOUT));
}

/**
 * @brief Test mixed timer operations
 * 
 * Verifies complex scenarios with mixed timer operations.
 */
void test_core1_timer_mixed_operations(void) {
    // Set multiple different timers
    core1_timer_set(CORE1_TIMER_DHCP_RENEWAL, 200);
    core1_timer_set(CORE1_TIMER_CONNECTION_TIMEOUT, 100);
    core1_timer_set(CORE1_TIMER_LOG_FLUSH, 150);
    core1_timer_set(CORE1_TIMER_PERSISTENCE_INTERVAL, 300);
    
    TEST_ASSERT_EQUAL(4, core1_timer_get_active_count());
    
    // Wait for connection timeout to expire first
    sleep_ms(110);
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_CONNECTION_TIMEOUT));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_DHCP_RENEWAL));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_LOG_FLUSH));
    
    // Reset the expired timer
    core1_timer_set(CORE1_TIMER_CONNECTION_TIMEOUT, 400);
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_CONNECTION_TIMEOUT));
    
    // Cancel persistence timer
    core1_timer_cancel(CORE1_TIMER_PERSISTENCE_INTERVAL);
    TEST_ASSERT_EQUAL(3, core1_timer_get_active_count());
}

/**
 * @brief Test timer accuracy over longer periods
 * 
 * Verifies that timers maintain accuracy over longer intervals.
 */
void test_core1_timer_accuracy(void) {
    uint64_t start_time = time_us_64();
    uint32_t interval_ms = 250;
    
    core1_timer_set(CORE1_TIMER_MAINTENANCE_CYCLE, interval_ms);
    
    // Wait for timer to expire
    while (!core1_timer_is_expired(CORE1_TIMER_MAINTENANCE_CYCLE)) {
        sleep_ms(1);
    }
    
    uint64_t end_time = time_us_64();
    uint32_t elapsed_ms = (end_time - start_time) / 1000;
    
    // Allow for some timing tolerance (±10ms)
    TEST_ASSERT_INT_WITHIN(10, interval_ms, elapsed_ms);
}

/**
 * @brief Test concurrent timer expiration handling
 * 
 * Verifies that multiple timers expiring simultaneously are handled correctly.
 */
void test_core1_timer_concurrent_expiration(void) {
    uint32_t interval = 100;
    
    // Set multiple timers with same interval
    core1_timer_set(CORE1_TIMER_DHCP_DISCOVER, interval);
    core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, interval);
    core1_timer_set(CORE1_TIMER_CONNECTION_TIMEOUT, interval);
    
    // None should be expired initially
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_DHCP_DISCOVER));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_CONNECTION_TIMEOUT));
    
    // Wait for all to expire
    sleep_ms(interval + 20);
    
    // All should be expired
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_DHCP_DISCOVER));
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT));
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_CONNECTION_TIMEOUT));
}

/**
 * @brief Test edge case timer operations
 * 
 * Verifies edge cases like maximum interval, minimum interval, etc.
 */
void test_core1_timer_edge_cases(void) {
    // Test maximum interval (close to uint32_t max)
    core1_timer_set(CORE1_TIMER_DHCP_RENEWAL, 0xFFFFFFFE);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_DHCP_RENEWAL));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_DHCP_RENEWAL));
    
    // Test minimum non-zero interval
    core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, 1);
    sleep_ms(10);
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT));
}

/**
 * @brief Test the PHY link poll timer used by core1_check_for_pending_work()
 *
 * ADR-007 ("Core 1 idle wait"): the 500 ms link poll is timer-driven so the
 * alarm IRQ terminates __wfi() on core 1. The timer must expire, and it must
 * be re-armable so the poll repeats.
 */
void test_core1_timer_link_poll(void) {
    core1_timer_set(CORE1_TIMER_LINK_POLL, 30);
    TEST_ASSERT_TRUE(core1_timer_is_active(CORE1_TIMER_LINK_POLL));
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_LINK_POLL));

    sleep_ms(40);
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_LINK_POLL));

    // Re-arm as the poll loop does after each expiry
    core1_timer_set(CORE1_TIMER_LINK_POLL, 30);
    TEST_ASSERT_FALSE(core1_timer_is_expired(CORE1_TIMER_LINK_POLL));
    sleep_ms(40);
    TEST_ASSERT_TRUE(core1_timer_is_expired(CORE1_TIMER_LINK_POLL));
}

// Unity test runner
int main(void) {
    stdio_init_all();
    
    UNITY_BEGIN();
    
    RUN_TEST(test_core1_timer_init);
    RUN_TEST(test_core1_timer_network_timers);
    RUN_TEST(test_core1_timer_maintenance_timers);
    RUN_TEST(test_core1_timer_large_capacity);
    RUN_TEST(test_core1_timer_long_intervals);
    RUN_TEST(test_core1_timer_network_expiration);
    RUN_TEST(test_core1_timer_mixed_operations);
    RUN_TEST(test_core1_timer_accuracy);
    RUN_TEST(test_core1_timer_concurrent_expiration);
    RUN_TEST(test_core1_timer_edge_cases);
    RUN_TEST(test_core1_timer_link_poll);
    
    while (true) {
        printf("Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}
