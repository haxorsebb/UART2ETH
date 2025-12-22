/**
 * @file core1_timer.c
 * @brief Core1 Timer Subsystem Implementation
 * 
 * Implements hardware timer alarm-based timing services for Core1 network and
 * maintenance operations using RP2350 Timer Alarm 1. Provides independent, 
 * interrupt-driven timer management with larger static allocation for diverse
 * network operations and no cross-core coordination.
 * 
 * Documentation Reference:
 * - ADR-009: Per-Core Timer Subsystem Architecture
 * - arc42 Chapter 5 - Core 1 Timer Subsystem Whitebox
 */

#include "core1_timer.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>
#include "state_machine.h"

// Timer state structure
typedef struct {
    uint32_t interval_ms;        // Timer interval in milliseconds
    uint64_t expiration_time_us; // Absolute expiration time in microseconds
    bool active;                 // Timer is currently running
    bool expired;                // Timer has expired (sticky flag)
} timer_entry_t;

// Core1 timer subsystem state
typedef struct {
    timer_entry_t timers[CORE1_MAX_TIMERS];  // Static timer array (16 timers)
    uint32_t next_alarm_timer_id;            // Timer ID for next hardware alarm
    uint64_t next_alarm_time_us;             // Next hardware alarm time
    uint32_t active_timer_count;             // Number of active timers
    bool initialized;                        // Subsystem initialization flag
} core1_timer_subsystem_t;

// Global timer subsystem state
static core1_timer_subsystem_t g_core1_timers = {0};
static uint64_t core1_next_expiration = UINT64_MAX;
    

// Hardware timer alarm number for Core1
#define CORE1_TIMER_ALARM_NUM 1

// Forward declarations for internal functions
static void core1_timer_update_next_alarm(void);
static void core1_timer_scan_for_expired(void);
static bool core1_timer_id_valid(core1_timer_id_t timer_id);
    


/**
 * @brief Initialize the Core1 timer subsystem
 */
bool core1_timer_init(void) {
    // Clear timer subsystem state
    memset(&g_core1_timers, 0, sizeof(g_core1_timers));
    
    // Set up hardware timer alarm interrupt
    hw_set_bits(&timer_hw->inte, 1u << CORE1_TIMER_ALARM_NUM);
    irq_set_exclusive_handler(TIMER1_IRQ_1, core1_timer_alarm_isr);
    irq_set_enabled(TIMER1_IRQ_1, true);
    
    g_core1_timers.initialized = true;
    
    return true;
}

/**
 * @brief Clean up the Core1 timer subsystem
 */
void core1_timer_cleanup(void) {
    if (!g_core1_timers.initialized) {
        return;
    }
    
    // Disable timer alarm interrupt
    irq_set_enabled(TIMER1_IRQ_1, false);
    hw_clear_bits(&timer_hw->inte, 1u << CORE1_TIMER_ALARM_NUM);
    
    // Clear all timers
    memset(&g_core1_timers, 0, sizeof(g_core1_timers));
}

/**
 * @brief Set a timer with specified interval
 */
void core1_timer_set(core1_timer_id_t timer_id, uint32_t interval_ms) {
    if (!core1_timer_id_valid(timer_id) || !g_core1_timers.initialized) {
        return;
    }

    // Update hardware alarm for next expiration
    core1_timer_update_next_alarm();
    
    timer_entry_t* timer = &g_core1_timers.timers[timer_id];
    
    // Cancel timer if it was previously active
    if (timer->active) {
        timer->active = false;
        g_core1_timers.active_timer_count--;
    }
    
    // Set up new timer
    timer->interval_ms = interval_ms;
    timer->expiration_time_us = timer_time_us_64(timer_hw) + (interval_ms * 1000ULL);
    timer->active = true;
    timer->expired = (interval_ms == 0);  // Zero interval = immediately expired
    
    g_core1_timers.active_timer_count++;
    
    // Update hardware alarm for next expiration
    core1_timer_update_next_alarm();
}

/**
 * @brief Check if a timer has expired
 */
bool core1_timer_is_expired(core1_timer_id_t timer_id) {
    if (!core1_timer_id_valid(timer_id) || !g_core1_timers.initialized) {
        return false;
    }
    
    return g_core1_timers.timers[timer_id].expired;
}

/**
 * @brief Check if a timer is currently active
 */
bool core1_timer_is_active(core1_timer_id_t timer_id) {
    if (!core1_timer_id_valid(timer_id) || !g_core1_timers.initialized) {
        return false;
    }
    
    return g_core1_timers.timers[timer_id].active;
}

/**
 * @brief Cancel an active timer
 */
void core1_timer_cancel(core1_timer_id_t timer_id) {
    if (!core1_timer_id_valid(timer_id) || !g_core1_timers.initialized) {
        return;
    }
    
    timer_entry_t* timer = &g_core1_timers.timers[timer_id];
    
    if (timer->active) {
        timer->active = false;
        timer->expired = false;
        g_core1_timers.active_timer_count--;
        
        // Update hardware alarm
        core1_timer_update_next_alarm();
    }
}

/**
 * @brief Get the number of currently active timers
 */
uint32_t core1_timer_get_active_count(void) {
    if (!g_core1_timers.initialized) {
        return 0;
    }
    
    return g_core1_timers.active_timer_count;
}

/**
 * @brief Timer interrupt service routine
 */
void core1_timer_alarm_isr(void) {

    // Clear the alarm interrupt
    hw_clear_bits(&timer_hw->intr, 1u << CORE1_TIMER_ALARM_NUM);
    
    // Scan for expired timers
    core1_timer_scan_for_expired();
    
    // Schedule next alarm
    core1_timer_update_next_alarm();

}

// Internal helper functions

/**
 * @brief Validate timer ID
 */
static bool core1_timer_id_valid(core1_timer_id_t timer_id) {
    return (timer_id >= 0 && timer_id < CORE1_MAX_TIMERS);
}

/**
 * @brief Scan all active timers for expiration
 */
static void core1_timer_scan_for_expired(void) {
    uint64_t current_time = timer_time_us_64(timer_hw);
    
    for (int i = 0; i < CORE1_MAX_TIMERS; i++) {
        timer_entry_t* timer = &g_core1_timers.timers[i];
        
        if (timer->active && !timer->expired) {
            if (current_time >= timer->expiration_time_us) {
                timer->expired = true;
                timer->active = false;
                g_core1_timers.active_timer_count--;
            }
        }
    }
}

/**
 * @brief Update hardware alarm for next timer expiration
 */
static void core1_timer_update_next_alarm(void) {
    bool found_active_timer = false;
    core1_next_expiration = UINT64_MAX;

    // Find the earliest expiration time among active timers
    for (int i = 0; i < CORE1_MAX_TIMERS; i++) {
        timer_entry_t* timer = &g_core1_timers.timers[i];
        
        if (timer->active && !timer->expired) {
            if (timer->expiration_time_us < core1_next_expiration) {
                core1_next_expiration = timer->expiration_time_us;
                found_active_timer = true;
            }
        }
    }
    
    // Set hardware alarm if we have active timers
    if (found_active_timer) {
        g_core1_timers.next_alarm_time_us = core1_next_expiration;
        if(timer_hardware_alarm_set_target(timer_hw, CORE1_TIMER_ALARM_NUM, (absolute_time_t)core1_next_expiration))
        {
            // printf("CORE1: TIMER WAS MISSED!\n");
        }
    }
}

/**
 * @brief check if next timer expiration has already passed
 */
bool core1_timer_no_next_alarm(void) {
    return(timer_time_us_64(timer_hw) > core1_next_expiration);
}
