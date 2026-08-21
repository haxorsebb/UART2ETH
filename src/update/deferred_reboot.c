/**
 * @file deferred_reboot.c
 * @brief Deferred reboot with acknowledgement grace period
 *
 * See deferred_reboot.h for the contract. State lives on Core1 only:
 * deferred_reboot_request() is called from the HTTP layer (which runs on
 * Core1) and deferred_reboot_poll() from the Core1 work loop, so no
 * cross-core synchronization is required.
 *
 * Documentation Reference:
 * - ADR-019: Network Configuration Changes Take Effect at Boot
 * - ADR-017: Update Module Architecture (reboot state flow)
 */

#include "deferred_reboot.h"

#include "core1_timer.h"
#include "state_machine.h"

static bool g_reboot_pending = false;
static reboot_reason_t g_reboot_reason = REBOOT_REASON_NONE;

void deferred_reboot_init(void) {
    g_reboot_pending = false;
    g_reboot_reason = REBOOT_REASON_NONE;
    core1_timer_cancel(CORE1_TIMER_REBOOT_GRACE);
}

void deferred_reboot_request(reboot_reason_t reason) {
    g_reboot_reason = reason;
    g_reboot_pending = true;
    core1_timer_set(CORE1_TIMER_REBOOT_GRACE, DEFERRED_REBOOT_GRACE_MS);
}

bool deferred_reboot_is_pending(void) {
    return g_reboot_pending;
}

void deferred_reboot_poll(void) {
    if (!g_reboot_pending) {
        return;
    }
    if (!core1_timer_is_expired(CORE1_TIMER_REBOOT_GRACE)) {
        return;
    }

    g_reboot_pending = false;
    core1_timer_cancel(CORE1_TIMER_REBOOT_GRACE);

    update_set_reboot_reason(g_reboot_reason);
    state_machine_process_main_event(MAIN_EVENT_REBOOT_REQUESTED);
}
