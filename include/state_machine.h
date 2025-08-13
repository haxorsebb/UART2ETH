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

// Doorbell numbers for cross-core synchronization - extern for global access
extern int doorbell_core0_wakes_core1;
extern int doorbell_core1_wakes_core0;

extern void shared_doorbell_irq();

typedef enum {
    WAKE_MAIN_STATE_CHANGED = 1,
    WAKE_CONFIG_UPDATE = 2,
    WAKE_ERROR_CONDITION = 3
} wake_reason_t;

// Main system states (shared between cores, atomic access)
typedef enum {
    MAIN_STATE_INIT = 0,           // System initialization and hardware setup
    MAIN_STATE_CONFIGURATION = 1,  // Configuration loading and validation
    MAIN_STATE_OPERATIONAL = 2,    // Normal operation mode
    MAIN_STATE_ERROR = 3           // Error state requiring recovery
} main_state_t;

// Core0 sub-states (UART processing, ISR-safe)
typedef enum {
    CORE0_INIT_UART,        // Initializing UART hardware
    CORE0_INIT_COMPLETE,    // UART init complete, signaling main state
    CORE0_INIT_IDLE,        // Waiting for other core during init  
    CORE0_INIT_ERROR,       // UART initialization failed
    CORE0_CONFIG_UART,      // configure UARTS 
    CORE0_CONFIG_COMPLETE,      // reconfiguration successful
    CORE0_CONFIG_IDLE,      // reconfiguration successful
    CORE0_CONFIG_ERROR,      // reconfiguration NOT successful
    CORE0_UART_IDLE,        // No active UART operations  
    CORE0_UART_ACTIVE,      // Processing UART data
    CORE0_UART_ERROR        // UART hardware error state
} core0_substate_t;

// Core1 sub-states (Network and maintenance, ISR-safe)
typedef enum {
    CORE1_INIT_PERISTENCE,      // Init persistence
    CORE1_INIT_LOGGING,         // init logging
    CORE1_INIT_NET,             // init net
    CORE1_INIT_WAIT_FOR_LINK,   // init is not complete without link
    CORE1_INIT_COMPLETE,        // init complete
    CORE1_INIT_IDLE,            // init complete, sleep until other core finishes init
    CORE1_CONFIG_NET,           // (re)-configure net 
    CORE1_CONFIG_COMPLETE,      // (re)-configuration of net successfull
    CORE1_CONFIG_NET_WAIT_FOR_DHCP, // (re)-configuration of net successfull, got dhcp
    CORE1_CONFIG_IDLE,          // (re)-configuration of net successfull
    CORE1_CONFIG_ERROR,         // (re)-configuration of net NOT successfull
    CORE1_NET_LINK_CHANGE,      // Network interface changing
    CORE1_NET_CONNECTED,        // Network interface up
    CORE1_NET_DISCONNECTED,     // Network interface down
    CORE1_NET_IDLE,             // Network interface up, no connections
    CORE1_NET_ACTIVE_RECEIVE,   // Active network connections
    CORE1_NET_ACTIVE_SEND,      // Active network connections
    CORE1_PERSISTENCE_ACTIVE,   // Flash persistence operation active
    CORE1_LOG_ACTIVE,           // Log processing active
    CORE1_IDLE,                 // we might sleep here or schedule new work
    CORE1_INIT_ERROR,           // unrecoverable init error
    CORE1_SHUTDOWN              // stop main loop
} core1_substate_t;

// Main state machine events
typedef enum {
    MAIN_EVENT_INIT_COMPLETE_CORE0,     // UART initialized
    MAIN_EVENT_INIT_COMPLETE_CORE1,     // Network interface initialized 
    MAIN_EVENT_CONFIG_COMPLETE_CORE0,      // Configuration loaded and validated
    MAIN_EVENT_CONFIG_COMPLETE_CORE1,      // Configuration loaded and validated
    MAIN_EVENT_SYSTEM_ERROR,       // System error detected
    MAIN_EVENT_ERROR_RECOVERED     // Recovery from error state complete
    
} main_state_event_t;

// Core0 state machine events
typedef enum {
    CORE0_EVENT_INIT_UART_COMPLETE,     // UART initialized
    CORE0_EVENT_INIT_UART_FAILED,       // UART could not be initialized
    CORE0_EVENT_CONFIG_UART_COMPLETE,       // UART could not be initialized
    CORE0_EVENT_CONFIG_UART_FAILED,       // UART could not be initialized
    CORE0_EVENT_UART_DATA_READY,        // UART data available for processing
    CORE0_EVENT_UART_IDLE,              // UART processing completed
    CORE0_EVENT_UART_ERROR,             // UART hardware error detected
    CORE0_EVENT_ERROR_RECOVERED,         // UART error recovery complete
    CORE0_EVENT_AUTO_TRANSITION            // DUMMY EVENT FOR auto-state transition
} core0_event_t;

// Core1 state machine events
typedef enum {
    CORE1_EVENT_INIT_PERSISTENCE_COMPLETE, // Peristence initialized 
    CORE1_EVENT_INIT_PERSISTENCE_FAILED, // Peristence initialized 
    CORE1_EVENT_INIT_LOGGING_COMPLETE,     // Logging initialized 
    CORE1_EVENT_INIT_LOGGING_FAILED,     // Logging initialized 
    CORE1_EVENT_INIT_NET_WAIT_FOR_LINK_UP,  // Network interface processed LINK_CHANGE
    CORE1_EVENT_INIT_NET_LINK_UP,        // Network interface processed LINK_CHANGE
    CORE1_EVENT_INIT_NET_LINK_DOWN,        // Network interface processed LINK_CHANGE
    CORE1_EVENT_INIT_NET_COMPLETE,     // Network initialized 
    CORE1_EVENT_INIT_NET_FAILED,     // Network initialized 
    CORE1_EVENT_CONFIG_NET_DHCP_REQUEST,     // Network sent dhcp request
    CORE1_EVENT_CONFIG_NET_DHCP_TIMEOUT,     // Network dhcp request timed out
    CORE1_EVENT_CONFIG_NET_COMPLETE,     // Network configured
    CORE1_EVENT_CONFIG_NET_FAILED,     // Network configuration
    CORE1_EVENT_NETWORK_LINK_CHANGE_ACTIVE,        // Network interface LINK_CHANGE
    CORE1_EVENT_NETWORK_LINK_UP,          // should never happen here, but in init
    CORE1_EVENT_NETWORK_LINK_DOWN,          // link was lost
    CORE1_EVENT_NETWORK_RECEIVE_ACTIVE,        // Network interface has packets
    CORE1_EVENT_NETWORK_RECEIVE_FINISHED,        // Network interface has no more packets
    CORE1_EVENT_NETWORK_SENDING_ACTIVE,        // Network interface has packets
    CORE1_EVENT_NETWORK_SENDING_FINISHED,        // Network interface has no more packets
    CORE1_EVENT_NETWORK_UP,        // Network interface connected
    CORE1_EVENT_NETWORK_DOWN,      // Network interface disconnected
    CORE1_EVENT_CONNECTION_ACTIVE, // TCP connection established
    CORE1_EVENT_CONNECTION_IDLE,   // All connections closed
    CORE1_EVENT_PERSISTENCE_START, // Flash operation starting
    CORE1_EVENT_PERSISTENCE_END,   // Flash operation completed
    CORE1_EVENT_LOG_START,         // Log processing starting
    CORE1_EVENT_LOG_END,            // Log processing completed
    CORE1_EVENT_AUTO_TRANSITION            // DUMMY EVENT FOR auto-state transition
} core1_event_t;

/**
 * @brief Initialize the state machine
 * 
 * Sets up internal data structures and initializes all state machines
 * to their initial states. Safe to call multiple times.
 * 
 * @return true if initialization successful, false otherwise
 */
bool state_machine_init(void);

/**
 * @brief Get current main state (thread-safe, non-blocking)
 * 
 * Atomically reads the current main state. Safe to call from any core
 * or interrupt context.
 * 
 * @return Current main state
 */
main_state_t state_machine_get_main_state(void);

/**
 * @brief Get current Core0 sub-state (thread-safe, non-blocking)
 * 
 * Atomically reads the current Core0 sub-state. Safe to call from any 
 * core or interrupt context.
 * 
 * @return Current Core0 sub-state
 */
core0_substate_t state_machine_get_core0_substate(void);

/**
 * @brief Get current Core1 sub-state (thread-safe, non-blocking)
 * 
 * Atomically reads the current Core1 sub-state. Safe to call from any
 * core or interrupt context.
 * 
 * @return Current Core1 sub-state
 */
core1_substate_t state_machine_get_core1_substate(void);

/**
 * @brief Process a main state machine event (thread-safe)
 * 
 * Validates the event against current state and applies transition
 * if valid. Uses atomic operations for thread-safety.
 * 
 * @param event The main state event to process
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_main_event(main_state_event_t event);

/**
 * @brief Process a Core0 state machine event (ISR-safe)
 * 
 * Validates the event against current Core0 sub-state and applies
 * transition if valid. ISR-safe with interrupt disable/restore.
 * 
 * @param event The Core0 event to process  
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_core0_event(core0_event_t event);

/**
 * @brief Process a Core1 state machine event (ISR-safe)
 * 
 * Validates the event against current Core1 sub-state and applies
 * transition if valid. ISR-safe with interrupt disable/restore.
 * 
 * @param event The Core1 event to process
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_core1_event(core1_event_t event);

#endif // STATE_MACHINE_H