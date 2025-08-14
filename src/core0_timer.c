/**
 * @file core0_timer.c
 * @brief Core0 Timer Subsystem Implementation
 * 
 * Implements hardware timer alarm-based timing services for Core0 UART and protocol
 * operations using RP2350 Timer Alarm 0. Provides independent, interrupt-driven timer
 * management with static allocation and no cross-core coordination.
 * 
 * Documentation Reference:
 * - ADR-009: Per-Core Timer Subsystem Architecture
 * - arc42 Chapter 5 - Core 0 Timer Subsystem Whitebox
 */

#include "core0_timer.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include <string.h>

// Timer state structure
typedef struct {
    uint32_t interval_ms;        // Timer interval in milliseconds
    uint64_t expiration_time_us; // Absolute expiration time in microseconds
    bool active;                 // Timer is currently running
    bool expired;                // Timer has expired (sticky flag)
} timer_entry_t;

// Core0 timer subsystem state
typedef struct {
    timer_entry_t timers[CORE0_MAX_TIMERS];  // Static timer array
    uint32_t next_alarm_timer_id;            // Timer ID for next hardware alarm
    uint64_t next_alarm_time_us;             // Next hardware alarm time
    uint32_t active_timer_count;             // Number of active timers
    bool initialized;                        // Subsystem initialization flag
} core0_timer_subsystem_t;

// Global timer subsystem state
static core0_timer_subsystem_t g_core0_timers = {0};

// Hardware timer alarm number for Core0
#define CORE0_TIMER_ALARM_NUM 0

// Forward declarations for internal functions
static void core0_timer_update_next_alarm(void);
static void core0_timer_scan_for_expired(void);
static bool core0_timer_id_valid(core0_timer_id_t timer_id);

/**
 * @brief Initialize the Core0 timer subsystem
 */
bool core0_timer_init(void) {
    // Clear timer subsystem state
    memset(&g_core0_timers, 0, sizeof(g_core0_timers));
    
    // Set up hardware timer alarm interrupt
    hw_set_bits(&timer_hw->inte, 1u << CORE0_TIMER_ALARM_NUM);
    irq_set_exclusive_handler(TIMER0_IRQ_0, core0_timer_alarm_isr);
    irq_set_enabled(TIMER0_IRQ_0, true);
    
    g_core0_timers.initialized = true;
    
    return true;
}

/**
 * @brief Clean up the Core0 timer subsystem
 */
void core0_timer_cleanup(void) {
    if (!g_core0_timers.initialized) {
        return;
    }
    
    // Disable timer alarm interrupt
    irq_set_enabled(TIMER0_IRQ_0, false);
    hw_clear_bits(&timer_hw->inte, 1u << CORE0_TIMER_ALARM_NUM);
    
    // Clear all timers
    memset(&g_core0_timers, 0, sizeof(g_core0_timers));
}

/**
 * @brief Set a timer with specified interval
 */
void core0_timer_set(core0_timer_id_t timer_id, uint32_t interval_ms) {
    if (!core0_timer_id_valid(timer_id) || !g_core0_timers.initialized) {
        return;
    }
    
    timer_entry_t* timer = &g_core0_timers.timers[timer_id];
    
    // Cancel timer if it was previously active
    if (timer->active) {
        timer->active = false;
        g_core0_timers.active_timer_count--;
    }
    
    // Set up new timer
    timer->interval_ms = interval_ms;
    timer->expiration_time_us = time_us_64() + (interval_ms * 1000ULL);
    timer->active = true;
    timer->expired = (interval_ms == 0);  // Zero interval = immediately expired
    
    g_core0_timers.active_timer_count++;
    
    // Update hardware alarm for next expiration
    core0_timer_update_next_alarm();
}

/**
 * @brief Check if a timer has expired
 */
bool core0_timer_is_expired(core0_timer_id_t timer_id) {
    if (!core0_timer_id_valid(timer_id) || !g_core0_timers.initialized) {
        return false;
    }
    
    return g_core0_timers.timers[timer_id].expired;
}

/**
 * @brief Check if a timer is currently active
 */
bool core0_timer_is_active(core0_timer_id_t timer_id) {
    if (!core0_timer_id_valid(timer_id) || !g_core0_timers.initialized) {
        return false;
    }
    
    return g_core0_timers.timers[timer_id].active;
}

/**
 * @brief Cancel an active timer
 */
void core0_timer_cancel(core0_timer_id_t timer_id) {
    if (!core0_timer_id_valid(timer_id) || !g_core0_timers.initialized) {
        return;
    }
    
    timer_entry_t* timer = &g_core0_timers.timers[timer_id];
    
    if (timer->active) {
        timer->active = false;
        timer->expired = false;
        g_core0_timers.active_timer_count--;
        
        // Update hardware alarm
        core0_timer_update_next_alarm();
    }
}

/**
 * @brief Get the number of currently active timers
 */
uint32_t core0_timer_get_active_count(void) {
    if (!g_core0_timers.initialized) {
        return 0;
    }
    
    return g_core0_timers.active_timer_count;
}

/**
 * @brief Timer interrupt service routine
 */
void core0_timer_alarm_isr(void) {
    // Clear the alarm interrupt
    hw_clear_bits(&timer_hw->intr, 1u << CORE0_TIMER_ALARM_NUM);
    
    // Scan for expired timers
    core0_timer_scan_for_expired();
    
    // Schedule next alarm
    core0_timer_update_next_alarm();
}

// Internal helper functions

/**
 * @brief Validate timer ID
 */
static bool core0_timer_id_valid(core0_timer_id_t timer_id) {
    return (timer_id >= 0 && timer_id < CORE0_MAX_TIMERS);
}

/**
 * @brief Scan all active timers for expiration
 */
static void core0_timer_scan_for_expired(void) {
    uint64_t current_time = time_us_64();
    
    for (int i = 0; i < CORE0_MAX_TIMERS; i++) {
        timer_entry_t* timer = &g_core0_timers.timers[i];
        
        if (timer->active && !timer->expired) {
            if (current_time >= timer->expiration_time_us) {
                timer->expired = true;
                timer->active = false;
                g_core0_timers.active_timer_count--;
            }
        }
    }
}

/**
 * @brief Update hardware alarm for next timer expiration
 */
static void core0_timer_update_next_alarm(void) {
    uint64_t next_expiration = UINT64_MAX;
    bool found_active_timer = false;
    
    // Find the earliest expiration time among active timers
    for (int i = 0; i < CORE0_MAX_TIMERS; i++) {
        timer_entry_t* timer = &g_core0_timers.timers[i];
        
        if (timer->active && !timer->expired) {
            if (timer->expiration_time_us < next_expiration) {
                next_expiration = timer->expiration_time_us;
                found_active_timer = true;
            }
        }
    }
    
    // Set hardware alarm if we have active timers
    if (found_active_timer) {
        g_core0_timers.next_alarm_time_us = next_expiration;
        timer_hw->alarm[CORE0_TIMER_ALARM_NUM] = (uint32_t)next_expiration;
    }
}
