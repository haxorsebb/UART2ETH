/**
 * @file core0_timer.h
 * @brief Core0 Timer Subsystem Header
 * 
 * Provides hardware timer alarm-based timing services for Core0 UART and protocol
 * operations. Uses RP2350 Timer Alarm 0 for independent, interrupt-driven timer
 * management with no cross-core coordination.
 * 
 * Documentation Reference:
 * - ADR-009: Per-Core Timer Subsystem Architecture
 * - arc42 Chapter 5 - Core 0 Timer Subsystem Whitebox
 */

#ifndef CORE0_TIMER_H
#define CORE0_TIMER_H

#include <stdint.h>
#include <stdbool.h>

// Core0 timer identifiers (UART/Protocol focus)
typedef enum {
    CORE0_TIMER_UART0_TIMEOUT = 0,     // UART0 transmission timeout
    CORE0_TIMER_UART1_TIMEOUT,         // UART1 transmission timeout
    CORE0_TIMER_UART2_TIMEOUT,         // UART2 transmission timeout
    CORE0_TIMER_UART3_TIMEOUT,         // UART3 transmission timeout
    CORE0_TIMER_PROTOCOL_DELAY,        // Protocol-specific timing delay
    CORE0_TIMER_RETRANSMIT,            // Retransmission timer
    CORE0_TIMER_FRAME_TIMEOUT,         // Serial frame timeout detection
    CORE0_TIMER_RESERVED,              // Reserved for future use
    CORE0_MAX_TIMERS = 8               // Maximum number of Core0 timers
} core0_timer_id_t;

/**
 * @brief Initialize the Core0 timer subsystem
 * 
 * Sets up the timer state array, configures RP2350 Timer Alarm 0,
 * and registers the timer interrupt handler for Core0.
 * 
 * @return true if initialization successful, false otherwise
 */
bool core0_timer_init(void);

/**
 * @brief Clean up the Core0 timer subsystem
 * 
 * Cancels all active timers, disables Timer Alarm 0, and
 * resets the timer subsystem state. Used for testing.
 */
void core0_timer_cleanup(void);

/**
 * @brief Set a timer with specified interval
 * 
 * Sets a timer to expire after the specified interval. If the timer
 * is already active, it will be reset with the new interval.
 * 
 * @param timer_id The timer ID to set
 * @param interval_ms Timer interval in milliseconds
 */
void core0_timer_set(core0_timer_id_t timer_id, uint32_t interval_ms);

/**
 * @brief Check if a timer has expired
 * 
 * Returns true if the timer has expired. This is a sticky flag
 * that remains true until the timer is reset or cancelled.
 * 
 * @param timer_id The timer ID to check
 * @return true if timer has expired, false otherwise
 */
bool core0_timer_is_expired(core0_timer_id_t timer_id);

/**
 * @brief Check if a timer is currently active
 * 
 * Returns true if the timer is currently running (set but not expired).
 * 
 * @param timer_id The timer ID to check
 * @return true if timer is active, false otherwise
 */
bool core0_timer_is_active(core0_timer_id_t timer_id);

/**
 * @brief Cancel an active timer
 * 
 * Cancels the specified timer, making it inactive and not expired.
 * Has no effect if the timer is not currently active.
 * 
 * @param timer_id The timer ID to cancel
 */
void core0_timer_cancel(core0_timer_id_t timer_id);

/**
 * @brief Get the number of currently active timers
 * 
 * Returns the count of timers that are currently active (set but not expired).
 * 
 * @return Number of active timers (0 to CORE0_MAX_TIMERS)
 */
uint32_t core0_timer_get_active_count(void);

/**
 * @brief Timer interrupt service routine
 * 
 * Internal function called by RP2350 Timer Alarm 0 interrupt.
 * Scans all active timers, marks expired timers, and schedules
 * the next hardware alarm interrupt.
 * 
 * @note This function is called from interrupt context
 */
void core0_timer_alarm_isr(void);

#endif // CORE0_TIMER_H
