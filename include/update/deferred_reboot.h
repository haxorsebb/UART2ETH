/**
 * @file deferred_reboot.h
 * @brief Deferred reboot with acknowledgement grace period
 *
 * Decouples a network-triggered reboot request from its execution. A request
 * records the reboot reason and arms a grace timer; the main state stays
 * OPERATIONAL during the grace period so Core1 keeps servicing lwIP and the
 * HTTP acknowledgement is transmitted before the reset. Once the grace timer
 * expires, the poll function enters the existing reboot path (flush
 * configuration and logs, then execute the reset).
 *
 * Call sequence:
 *   HTTP handler:   deferred_reboot_request(REBOOT_REASON_USER_REQUESTED)
 *   Core1 loop:     deferred_reboot_poll()   (every work iteration)
 *
 * Documentation Reference:
 * - ADR-019: Network Configuration Changes Take Effect at Boot
 * - ADR-017: Update Module Architecture (reboot state flow)
 */

#ifndef DEFERRED_REBOOT_H
#define DEFERRED_REBOOT_H

#include <stdbool.h>

#include "update_manager.h"  // reboot_reason_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Grace period between the reboot request and entering the reboot path.
 * Long enough for lwIP to transmit the queued HTTP response, short enough
 * to feel immediate to the user.
 */
#define DEFERRED_REBOOT_GRACE_MS 300

/**
 * @brief Reset the deferred reboot state (no reboot pending)
 *
 * Called once during Core1 initialization; also used by tests.
 */
void deferred_reboot_init(void);

/**
 * @brief Request a reboot after the grace period
 *
 * Records the reason and arms CORE1_TIMER_REBOOT_GRACE. Does NOT change the
 * main state; normal operation (including lwIP servicing) continues until
 * deferred_reboot_poll() observes the expired grace timer. A repeated
 * request overwrites the reason and restarts the grace period.
 *
 * @param reason Reason to log and persist for the reboot (ADR-017)
 */
void deferred_reboot_request(reboot_reason_t reason);

/**
 * @brief Check whether a reboot request is pending
 *
 * @return true between deferred_reboot_request() and the poll call that
 *         triggers the reboot
 */
bool deferred_reboot_is_pending(void);

/**
 * @brief Trigger the reboot once the grace period has expired
 *
 * Called from the Core1 work loop. No effect while no request is pending or
 * the grace timer is still running. When the timer has expired: stores the
 * reboot reason (update_set_reboot_reason) and posts
 * MAIN_EVENT_REBOOT_REQUESTED, entering the ADR-017 reboot path
 * (CORE1_REBOOT_FLUSH -> CORE1_REBOOT_EXECUTE).
 */
void deferred_reboot_poll(void);

#ifdef __cplusplus
}
#endif

#endif // DEFERRED_REBOOT_H
