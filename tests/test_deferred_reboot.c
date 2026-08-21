/**
 * @file test_deferred_reboot.c
 * @brief Unit tests for the deferred reboot mechanism
 *
 * A reboot requested over the network (POST /reboot, or the reboot required
 * after a TCP port change) must not reset the device before the HTTP
 * response has left the wire. The current implementation calls
 * watchdog_reboot(0, 0, 1) synchronously from the request handler, which
 * races the response transmission.
 *
 * The deferred reboot mechanism decouples request from execution:
 *  - deferred_reboot_request(reason) records the reason and arms the
 *    CORE1_TIMER_REBOOT_GRACE timer with DEFERRED_REBOOT_GRACE_MS. It does
 *    NOT change the main state, so Core1 keeps servicing lwIP and the
 *    response can be transmitted during the grace period.
 *  - deferred_reboot_poll() is called from the Core1 work loop. Once the
 *    grace timer has expired it stores the reboot reason and posts
 *    MAIN_EVENT_REBOOT_REQUESTED, entering the existing ADR-017 reboot
 *    path (flush configuration and logs, then execute reboot).
 *
 * Documentation Reference:
 * - ADR-019 (proposed): TCP port changes take effect at boot / deferred reboot
 * - ADR-017: Reboot state handling (flush before reboot)
 * - arc42 Chapter 6 - Runtime View, configuration change sequence
 */

#include "unity.h"
#include "update/deferred_reboot.h"
#include "update/update_manager.h"
#include "state_machine.h"
#include "core1_timer.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdatomic.h>

// Access to the state machine initialization flag, same pattern as
// test_state_machine.c, to force a clean re-initialization per test.
extern _Atomic bool g_initialized;

/**
 * Drive the main state machine from INIT to OPERATIONAL.
 * MAIN_EVENT_REBOOT_REQUESTED is only accepted in OPERATIONAL and ERROR.
 */
static void enter_operational_state(void) {
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_CONFIGURATION,
                              state_machine_get_main_state(),
                              "Precondition: CONFIGURATION state required");
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE0);
    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_OPERATIONAL,
                              state_machine_get_main_state(),
                              "Precondition: OPERATIONAL state required");
}

void setUp(void) {
    atomic_store(&g_initialized, false);
    state_machine_init();
    core1_timer_cleanup();
    TEST_ASSERT_TRUE(core1_timer_init());
    deferred_reboot_init();
    update_set_reboot_reason(REBOOT_REASON_NONE);
}

void tearDown(void) {
    core1_timer_cleanup();
}

/**
 * Test: A reboot request must not change the main state.
 *
 * The state machine leaving OPERATIONAL is what stops lwIP servicing on
 * Core1. Staying in OPERATIONAL during the grace period is the property
 * that lets the HTTP acknowledgement leave the device.
 */
void test_request_does_not_change_main_state(void) {
    enter_operational_state();

    deferred_reboot_request(REBOOT_REASON_USER_REQUESTED);

    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_OPERATIONAL,
                              state_machine_get_main_state(),
                              "Reboot request must not leave OPERATIONAL before grace period");
}

/**
 * Test: A reboot request marks the mechanism pending and arms the grace timer.
 */
void test_request_marks_pending_and_arms_timer(void) {
    enter_operational_state();

    TEST_ASSERT_FALSE_MESSAGE(deferred_reboot_is_pending(),
                              "No reboot must be pending before a request");

    deferred_reboot_request(REBOOT_REASON_USER_REQUESTED);

    TEST_ASSERT_TRUE_MESSAGE(deferred_reboot_is_pending(),
                             "Reboot must be pending after a request");
    TEST_ASSERT_TRUE_MESSAGE(core1_timer_is_active(CORE1_TIMER_REBOOT_GRACE),
                             "Grace timer must be armed by the request");
}

/**
 * Test: Polling before the grace period expires must not trigger the reboot.
 */
void test_poll_before_grace_expiry_does_nothing(void) {
    enter_operational_state();
    deferred_reboot_request(REBOOT_REASON_USER_REQUESTED);

    // Poll immediately, well inside the grace period.
    deferred_reboot_poll();

    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_OPERATIONAL,
                              state_machine_get_main_state(),
                              "Poll inside grace period must not transition to REBOOT");
    TEST_ASSERT_TRUE_MESSAGE(deferred_reboot_is_pending(),
                             "Request must remain pending inside grace period");
}

/**
 * Test: Polling after the grace period stores the reason and enters the
 * ADR-017 reboot path.
 *
 * Entering MAIN_STATE_REBOOT is safe in this test: the Core1 main loop is
 * not running, so CORE1_REBOOT_FLUSH / CORE1_REBOOT_EXECUTE are never
 * processed and no actual reboot occurs.
 */
void test_poll_after_grace_expiry_enters_reboot_state(void) {
    enter_operational_state();
    deferred_reboot_request(REBOOT_REASON_USER_REQUESTED);

    // Wait out the grace period, with margin for timer granularity.
    sleep_ms(DEFERRED_REBOOT_GRACE_MS + 50);

    deferred_reboot_poll();

    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_REBOOT,
                              state_machine_get_main_state(),
                              "Poll after grace period must enter MAIN_STATE_REBOOT");
    TEST_ASSERT_EQUAL_MESSAGE(REBOOT_REASON_USER_REQUESTED,
                              update_get_reboot_reason(),
                              "Reboot reason must be stored for the ADR-017 flush path");
    TEST_ASSERT_FALSE_MESSAGE(deferred_reboot_is_pending(),
                              "Pending flag must clear once the reboot is triggered");
}

/**
 * Test: Polling without a prior request is a no-op.
 *
 * deferred_reboot_poll() runs in every Core1 work loop iteration; it must
 * be side-effect free when no reboot was requested.
 */
void test_poll_without_request_is_noop(void) {
    enter_operational_state();

    deferred_reboot_poll();

    TEST_ASSERT_EQUAL_MESSAGE(MAIN_STATE_OPERATIONAL,
                              state_machine_get_main_state(),
                              "Poll without request must not change state");
    TEST_ASSERT_EQUAL_MESSAGE(REBOOT_REASON_NONE,
                              update_get_reboot_reason(),
                              "Poll without request must not set a reboot reason");
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // Allow USB/UART enumeration before test output

    printf("\n=== Deferred Reboot Tests (ADR-019 proposed) ===\n");

    UNITY_BEGIN();
    RUN_TEST(test_request_does_not_change_main_state);
    RUN_TEST(test_request_marks_pending_and_arms_timer);
    RUN_TEST(test_poll_before_grace_expiry_does_nothing);
    RUN_TEST(test_poll_after_grace_expiry_enters_reboot_state);
    RUN_TEST(test_poll_without_request_is_noop);
    return UNITY_END();
}
