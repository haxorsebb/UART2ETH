/**
 * @file state_machine.c
 * @brief Event-driven state machine implementation
 * 
 * Implements three independent event-driven state machines with atomic main state
 * and ISR-safe sub-states. Uses strict event processing pattern with condition
 * validation for all state transitions.
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Global State Machine Whitebox
 */

#include "state_machine.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/sync.h"
#include <stdatomic.h>

// State variables with proper synchronization
static _Atomic main_state_t g_main_state = MAIN_STATE_INIT;          // Atomic for cross-core access
static volatile core0_substate_t g_core0_substate = CORE0_UART_IDLE;  // ISR-safe volatile
static volatile core1_substate_t g_core1_substate = CORE1_NET_DISCONNECTED;  // ISR-safe volatile

// Initialization flag
static volatile bool g_initialized = false;

// Forward declarations for condition checking functions
static bool check_initialization_complete(void);
static bool check_configuration_valid(void);
static bool check_error_recovery_complete(void);
static bool check_uart_data_available(void);
static bool check_uart_processing_complete(void);
static bool check_uart_hardware_recovered(void);
static bool check_network_interface_ready(void);
static bool check_main_state_operational(void);

/**
 * Initialize the event-driven state machine
 * 
 * @return true if initialization successful, false otherwise
 */
bool state_machine_init(void) {
    if (g_initialized) {
        return true;  // Already initialized
    }
    
    // Initialize all state machines to their initial states
    atomic_store(&g_main_state, MAIN_STATE_INIT);
    g_core0_substate = CORE0_UART_IDLE;
    g_core1_substate = CORE1_NET_DISCONNECTED;
    
    // Memory barrier to ensure state initialization is visible to all cores
    __sync_synchronize();
    
    g_initialized = true;
    return true;
}

/**
 * Get current main state (non-blocking, atomic read)
 * 
 * @return Current main state
 */
main_state_t state_machine_get_main_state(void) {
    return atomic_load(&g_main_state);
}

/**
 * Get current Core0 sub-state (non-blocking, volatile read)
 * 
 * @return Current Core0 sub-state
 */
core0_substate_t state_machine_get_core0_substate(void) {
    return g_core0_substate;
}

/**
 * Get current Core1 sub-state (non-blocking, volatile read)
 * 
 * @return Current Core1 sub-state
 */
core1_substate_t state_machine_get_core1_substate(void) {
    return g_core1_substate;
}

/**
 * Process main state machine events (atomic operations)
 * 
 * Implements: main_state + event + check_condition() → new_main_state
 * 
 * @param event The main state event to process
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_main_event(main_state_event_t event) {
    if (!g_initialized) {
        return false;
    }
    
    main_state_t current_state = atomic_load(&g_main_state);
    main_state_t new_state = current_state;  // Default: no change
    
    // State + Event + Condition → New State
    switch (current_state) {
        case MAIN_STATE_INIT:
            switch (event) {
                case MAIN_EVENT_INIT_COMPLETE:
                    if (check_initialization_complete()) {
                        new_state = MAIN_STATE_CONFIGURATION;
                    }
                    break;
                case MAIN_EVENT_SYSTEM_ERROR:
                    new_state = MAIN_STATE_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case MAIN_STATE_CONFIGURATION:
            switch (event) {
                case MAIN_EVENT_CONFIG_LOADED:
                    if (check_configuration_valid()) {
                        new_state = MAIN_STATE_OPERATIONAL;
                    }
                    break;
                case MAIN_EVENT_SYSTEM_ERROR:
                    new_state = MAIN_STATE_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case MAIN_STATE_OPERATIONAL:
            switch (event) {
                case MAIN_EVENT_SYSTEM_ERROR:
                    new_state = MAIN_STATE_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case MAIN_STATE_ERROR:
            switch (event) {
                case MAIN_EVENT_ERROR_RECOVERED:
                    if (check_error_recovery_complete()) {
                        new_state = MAIN_STATE_OPERATIONAL;
                    }
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
    }
    
    // Apply atomic state transition if state changed
    if (new_state != current_state) {
        main_state_t expected = current_state;
        return atomic_compare_exchange_strong(&g_main_state, &expected, new_state);
    }
    
    return true;  // No change needed, event processed successfully
}

/**
 * Process Core0 sub-state machine events (ISR-safe operations)
 * 
 * Implements: core0_substate + event + check_condition() → new_core0_substate
 * 
 * @param event The Core0 event to process
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_core0_event(core0_event_t event) {
    if (!g_initialized) {
        return false;
    }
    
    // Disable interrupts for ISR-safe atomic read-modify-write
    uint32_t interrupts = save_and_disable_interrupts();
    
    core0_substate_t current_state = g_core0_substate;
    core0_substate_t new_state = current_state;  // Default: no change
    
    // State + Event + Condition → New State
    switch (current_state) {
        case CORE0_UART_IDLE:
            switch (event) {
                case CORE0_EVENT_UART_DATA_READY:
                    if (check_uart_data_available() && check_main_state_operational()) {
                        new_state = CORE0_UART_ACTIVE;
                    }
                    break;
                case CORE0_EVENT_UART_ERROR:
                    new_state = CORE0_UART_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE0_UART_ACTIVE:
            switch (event) {
                case CORE0_EVENT_UART_IDLE:
                    if (check_uart_processing_complete()) {
                        new_state = CORE0_UART_IDLE;
                    }
                    break;
                case CORE0_EVENT_UART_ERROR:
                    new_state = CORE0_UART_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE0_UART_ERROR:
            switch (event) {
                case CORE0_EVENT_ERROR_RECOVERED:
                    if (check_uart_hardware_recovered()) {
                        new_state = CORE0_UART_IDLE;
                    }
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
    }
    
    // Apply state change if needed
    if (new_state != current_state) {
        g_core0_substate = new_state;
    }
    
    // Re-enable interrupts
    restore_interrupts(interrupts);
    
    return true;  // Event processed successfully
}

/**
 * Process Core1 sub-state machine events (ISR-safe operations)
 * 
 * Implements: core1_substate + event + check_condition() → new_core1_substate
 * 
 * @param event The Core1 event to process
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_core1_event(core1_event_t event) {
    if (!g_initialized) {
        return false;
    }
    
    // Disable interrupts for ISR-safe atomic read-modify-write
    uint32_t interrupts = save_and_disable_interrupts();
    
    core1_substate_t current_state = g_core1_substate;
    core1_substate_t new_state = current_state;  // Default: no change
    
    // State + Event + Condition → New State
    switch (current_state) {
        case CORE1_NET_DISCONNECTED:
            switch (event) {
                case CORE1_EVENT_NETWORK_UP:
                    if (check_network_interface_ready()) {
                        new_state = CORE1_NET_IDLE;
                    }
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_NET_IDLE:
            switch (event) {
                case CORE1_EVENT_NETWORK_DOWN:
                    new_state = CORE1_NET_DISCONNECTED;
                    break;
                case CORE1_EVENT_CONNECTION_ACTIVE:
                    new_state = CORE1_NET_ACTIVE;
                    break;
                case CORE1_EVENT_PERSISTENCE_START:
                    new_state = CORE1_PERSISTENCE_ACTIVE;
                    break;
                case CORE1_EVENT_LOG_START:
                    new_state = CORE1_LOG_ACTIVE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_NET_ACTIVE:
            switch (event) {
                case CORE1_EVENT_NETWORK_DOWN:
                    new_state = CORE1_NET_DISCONNECTED;
                    break;
                case CORE1_EVENT_CONNECTION_IDLE:
                    new_state = CORE1_NET_IDLE;
                    break;
                case CORE1_EVENT_PERSISTENCE_START:
                    new_state = CORE1_PERSISTENCE_ACTIVE;
                    break;
                case CORE1_EVENT_LOG_START:
                    new_state = CORE1_LOG_ACTIVE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_PERSISTENCE_ACTIVE:
            switch (event) {
                case CORE1_EVENT_PERSISTENCE_END:
                    // Return to previous network state (simplified: assume NET_IDLE)
                    new_state = CORE1_NET_IDLE;
                    break;
                case CORE1_EVENT_NETWORK_DOWN:
                    new_state = CORE1_NET_DISCONNECTED;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_LOG_ACTIVE:
            switch (event) {
                case CORE1_EVENT_LOG_END:
                    // Return to previous network state (simplified: assume NET_IDLE)
                    new_state = CORE1_NET_IDLE;
                    break;
                case CORE1_EVENT_NETWORK_DOWN:
                    new_state = CORE1_NET_DISCONNECTED;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
    }
    
    // Apply state change if needed
    if (new_state != current_state) {
        g_core1_substate = new_state;
    }
    
    // Re-enable interrupts
    restore_interrupts(interrupts);
    
    return true;  // Event processed successfully
}

// Condition checking functions (these would integrate with actual system components)

/**
 * Check if system initialization is complete
 */
static bool check_initialization_complete(void) {
    // For testing: always return true
    // In real implementation: check shared_memory_initialized(), hardware_ready(), etc.
    return true;
}

/**
 * Check if configuration is valid
 */
static bool check_configuration_valid(void) {
    // For testing: always return true
    // In real implementation: validate configuration integrity
    return true;
}

/**
 * Check if error recovery is complete
 */
static bool check_error_recovery_complete(void) {
    // For testing: always return true
    // In real implementation: verify all systems operational
    return true;
}

/**
 * Check if UART data is available
 */
static bool check_uart_data_available(void) {
    // For testing: always return true
    // In real implementation: check UART hardware status
    return true;
}

/**
 * Check if UART processing is complete
 */
static bool check_uart_processing_complete(void) {
    // For testing: always return true
    // In real implementation: check UART processing status
    return true;
}

/**
 * Check if UART hardware has recovered from error
 */
static bool check_uart_hardware_recovered(void) {
    // For testing: always return true
    // In real implementation: verify UART hardware status
    return true;
}

/**
 * Check if network interface is ready
 */
static bool check_network_interface_ready(void) {
    // For testing: always return true
    // In real implementation: check network interface status
    return true;
}

/**
 * Check if main state is operational
 */
static bool check_main_state_operational(void) {
    return atomic_load(&g_main_state) == MAIN_STATE_OPERATIONAL;
}