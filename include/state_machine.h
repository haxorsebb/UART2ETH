/**
 * @file state_machine.h
 * @brief Event-driven state machine for dual-core coordination
 * 
 * Implements three independent event-driven state machines with atomic main state
 * and ISR-safe sub-states. Uses strict event processing pattern where state changes
 * only occur through event processing with condition validation.
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Global State Machine Whitebox
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

// Main system states (shared between cores, atomic access)
typedef enum {
    MAIN_STATE_INIT = 0,           // System initialization and hardware setup
    MAIN_STATE_CONFIGURATION = 1,  // Configuration loading and validation
    MAIN_STATE_OPERATIONAL = 2,    // Normal operation mode
    MAIN_STATE_ERROR = 3           // Error state requiring recovery
} main_state_t;

// Core0 sub-states (UART processing, ISR-safe)
typedef enum {
    CORE0_UART_IDLE = 0,    // No active UART operations
    CORE0_UART_ACTIVE = 1,  // Processing UART data
    CORE0_UART_ERROR = 2    // UART hardware error state
} core0_substate_t;

// Core1 sub-states (Network and maintenance, ISR-safe)
typedef enum {
    CORE1_NET_DISCONNECTED = 0,   // Network interface down
    CORE1_NET_IDLE = 1,           // Network interface up, no connections
    CORE1_NET_ACTIVE = 2,         // Active network connections
    CORE1_PERSISTENCE_ACTIVE = 3, // Flash persistence operation active
    CORE1_LOG_ACTIVE = 4          // Log processing active
} core1_substate_t;

// Main state machine events
typedef enum {
    MAIN_EVENT_INIT_COMPLETE,      // System initialization finished successfully
    MAIN_EVENT_CONFIG_LOADED,      // Configuration loaded and validated
    MAIN_EVENT_SYSTEM_ERROR,       // System error detected
    MAIN_EVENT_ERROR_RECOVERED     // Recovery from error state complete
} main_state_event_t;

// Core0 state machine events
typedef enum {
    CORE0_EVENT_UART_DATA_READY,   // UART data available for processing
    CORE0_EVENT_UART_IDLE,         // UART processing completed
    CORE0_EVENT_UART_ERROR,        // UART hardware error detected
    CORE0_EVENT_ERROR_RECOVERED    // UART error recovery complete
} core0_event_t;

// Core1 state machine events
typedef enum {
    CORE1_EVENT_NETWORK_UP,        // Network interface connected
    CORE1_EVENT_NETWORK_DOWN,      // Network interface disconnected
    CORE1_EVENT_CONNECTION_ACTIVE, // TCP connection established
    CORE1_EVENT_CONNECTION_IDLE,   // All connections closed
    CORE1_EVENT_PERSISTENCE_START, // Flash operation starting
    CORE1_EVENT_PERSISTENCE_END,   // Flash operation completed
    CORE1_EVENT_LOG_START,         // Log processing starting
    CORE1_EVENT_LOG_END            // Log processing completed
} core1_event_t;

// State machine initialization
bool state_machine_init(void);

// State query functions (non-blocking, read-only)
main_state_t state_machine_get_main_state(void);
core0_substate_t state_machine_get_core0_substate(void);
core1_substate_t state_machine_get_core1_substate(void);

// Event processing functions (ONLY way to change states)
bool state_machine_process_main_event(main_state_event_t event);
bool state_machine_process_core0_event(core0_event_t event);
bool state_machine_process_core1_event(core1_event_t event);

#endif // STATE_MACHINE_H