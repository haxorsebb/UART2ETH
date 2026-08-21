/**
 * @file core1_timer.h
 * @brief Core1 Timer Subsystem Header
 * 
 * Provides hardware timer alarm-based timing services for Core1 network and
 * maintenance operations. Uses RP2350 Timer Alarm 1 for independent, interrupt-driven
 * timer management supporting larger timer arrays for diverse network operations.
 * 
 * Documentation Reference:
 * - ADR-009: Per-Core Timer Subsystem Architecture
 * - arc42 Chapter 5 - Core 1 Timer Subsystem Whitebox
 */

#ifndef CORE1_TIMER_H
#define CORE1_TIMER_H

#include <stdint.h>
#include <stdbool.h>

// Core1 timer identifiers (Network/Maintenance focus)  
typedef enum {
    CORE1_TIMER_DHCP_RENEWAL = 0,       // DHCP lease renewal timer
    CORE1_TIMER_NETWORK_TIMEOUT,        // Network operation timeout
    CORE1_TIMER_NETWORK_TX_TIMEOUT,        // Network operation timeout
    CORE1_TIMER_PERSISTENCE_INTERVAL,   // Flash persistence interval
    CORE1_TIMER_LOG_FLUSH,              // Log processing/flush timer
    CORE1_TIMER_CONNECTION_TIMEOUT,     // TCP connection timeout
    CORE1_TIMER_DHCP_DISCOVER,          // DHCP discover retry timer
    CORE1_TIMER_MAINTENANCE_CYCLE,      // Periodic maintenance cycle
    CORE1_TIMER_STATISTICS_UPDATE,      // Statistics collection timer
    CORE1_TIMER_CONFIG_BACKUP,          // Configuration backup timer
    CORE1_TIMER_WATCHDOG_FEED,          // Watchdog feeding timer
    CORE1_TIMER_LINK_POLL,              // ENC28J60 PHY link status poll (ADR-007, core 1 idle wait)
    CORE1_TIMER_REBOOT_GRACE,           // Deferred reboot grace period (ADR-019)
    CORE1_TIMER_RESERVED_3,             // Reserved for future use
    CORE1_TIMER_RESERVED_4,             // Reserved for future use
    CORE1_MAX_TIMERS = 16               // Maximum number of Core1 timers
} core1_timer_id_t;

/**
 * @brief Initialize the Core1 timer subsystem
 * 
 * Sets up the timer state array, configures RP2350 Timer Alarm 1,
 * and registers the timer interrupt handler for Core1.
 * 
 * @return true if initialization successful, false otherwise
 */
bool core1_timer_init(void);

/**
 * @brief Clean up the Core1 timer subsystem
 * 
 * Cancels all active timers, disables Timer Alarm 1, and
 * resets the timer subsystem state. Used for testing.
 */
void core1_timer_cleanup(void);

/**
 * @brief Set a timer with specified interval
 * 
 * Sets a timer to expire after the specified interval. If the timer
 * is already active, it will be reset with the new interval.
 * 
 * @param timer_id The timer ID to set
 * @param interval_ms Timer interval in milliseconds
 */
void core1_timer_set(core1_timer_id_t timer_id, uint32_t interval_ms);

/**
 * @brief Check if a timer has expired
 * 
 * Returns true if the timer has expired. This is a sticky flag
 * that remains true until the timer is reset or cancelled.
 * 
 * @param timer_id The timer ID to check
 * @return true if timer has expired, false otherwise
 */
bool core1_timer_is_expired(core1_timer_id_t timer_id);

/**
 * @brief Check if a timer is currently active
 * 
 * Returns true if the timer is currently running (set but not expired).
 * 
 * @param timer_id The timer ID to check
 * @return true if timer is active, false otherwise
 */
bool core1_timer_is_active(core1_timer_id_t timer_id);

/**
 * @brief Cancel an active timer
 * 
 * Cancels the specified timer, making it inactive and not expired.
 * Has no effect if the timer is not currently active.
 * 
 * @param timer_id The timer ID to cancel
 */
void core1_timer_cancel(core1_timer_id_t timer_id);

/**
 * @brief Get the number of currently active timers
 * 
 * Returns the count of timers that are currently active (set but not expired).
 * 
 * @return Number of active timers (0 to CORE1_MAX_TIMERS)
 */
uint32_t core1_timer_get_active_count(void);

/**
 * @brief Timer interrupt service routine
 * 
 * Internal function called by RP2350 Timer Alarm 1 interrupt.
 * Scans all active timers, marks expired timers, and schedules
 * the next hardware alarm interrupt.
 * 
 * @note This function is called from interrupt context
 */
void core1_timer_alarm_isr(void);

/**
 * @brief check if next timer expiration has already passed
 */
bool core1_timer_no_next_alarm(void);

#endif // CORE1_TIMER_H
