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
#include "log_manager.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include <stdatomic.h>
#include <stdio.h>
#include "debug.h"

// Cross-core wake-up doorbells (ADR-007, "Cross-Core Wake-Up").
// One SIO doorbell per direction, claimed once in claim_wake_doorbells().
// -1 means "not claimed". The inter-core FIFO is never used by this module.
static int doorbell_core0_wakes_core1 = -1;
static int doorbell_core1_wakes_core0 = -1;

// State variables with proper synchronization
static _Atomic main_state_t g_main_state = MAIN_STATE_INIT;                   // Atomic for cross-core access
static _Atomic core0_substate_t g_core0_substate = CORE0_INIT_UART;           // Atomic for ISR-safe access
static _Atomic core1_substate_t g_core1_substate = CORE1_INIT_PERISTENCE;    // Atomic for ISR-safe access

// Initialization flag (exposed to tests for proper reinitialization)
_Atomic bool g_initialized = false;

// Forward declarations for validation and condition checking functions
static bool is_valid_main_event(main_state_event_t event);
static bool is_valid_core0_event(core0_event_t event);
static bool is_valid_core1_event(core1_event_t event);
static bool check_initialization_complete(void);
static bool check_configuration_valid(void);
static bool check_error_recovery_complete(void);
static bool check_uart_data_available(void);
static bool check_uart_processing_complete(void);
static bool check_uart_hardware_recovered(void);
static bool check_network_interface_ready(void);
static bool check_main_state_operational(void);

// conditions for main state change -> syncing both cores to switch main_stte syncronously
static bool check_core0_initialization_complete(void);
static bool check_core1_initialization_complete(void);
static bool check_core0_configuration_complete(void);
static bool check_core1_configuration_complete(void);

// Cross-core synchronization
static void wake_other_core_after_main_state_change(main_state_t new_state);
static bool claim_wake_doorbells(void);
static int doorbell_for_direction(wake_direction_t direction);
static int doorbell_rung_by_this_core(void);
static void clear_wake_doorbells_on_this_core(void);
static void wake_doorbell_irq_handler(void);

/**
 * Initialize the event-driven state machine
 * 
 * @return true if initialization successful, false otherwise
 */
bool state_machine_init(void) {
    if (atomic_load(&g_initialized)) {
        return true;  // Already initialized
    }
    
    // Initialize all state machines to their initial states
    atomic_store(&g_main_state, MAIN_STATE_INIT);
    atomic_store(&g_core0_substate, CORE0_INIT_UART);
    atomic_store(&g_core1_substate, CORE1_INIT_PERISTENCE);
    
    if (!claim_wake_doorbells()) {
        return false;
    }

    // Atomic operations provide necessary memory ordering guarantees
    atomic_store(&g_initialized, true);

    DEBUG_ONLY({
        printf("Wake doorbells: core0_wakes_core1=%d, core1_wakes_core0=%d\n", 
               doorbell_core0_wakes_core1, doorbell_core1_wakes_core0);
    });

    return true;
}

/**
 * Claim one doorbell per wake direction.
 *
 * Doorbells are hardware resources; claiming is done exactly once for the
 * lifetime of the program, independent of g_initialized, so that
 * re-initialization (tests, core restarts) does not exhaust the 8 doorbells.
 *
 * @return true if both doorbells are available
 */
static bool claim_wake_doorbells(void) {
    if (doorbell_core0_wakes_core1 >= 0 && doorbell_core1_wakes_core0 >= 0) {
        return true;  // Already claimed
    }

    const uint both_cores = 0x3;
    int first = multicore_doorbell_claim_unused(both_cores, false);
    int second = multicore_doorbell_claim_unused(both_cores, false);
    if (first < 0 || second < 0) {
        if (first >= 0) {
            multicore_doorbell_unclaim((uint)first, both_cores);
        }
        return false;
    }

    doorbell_core0_wakes_core1 = first;
    doorbell_core1_wakes_core0 = second;
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
 * Get current Core0 sub-state (non-blocking, atomic read)
 * 
 * @return Current Core0 sub-state
 */
core0_substate_t state_machine_get_core0_substate(void) {
    return atomic_load(&g_core0_substate);
}

/**
 * Get current Core1 sub-state (non-blocking, atomic read)
 * 
 * @return Current Core1 sub-state
 */
core1_substate_t state_machine_get_core1_substate(void) {
    return atomic_load(&g_core1_substate);
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
    if (!atomic_load(&g_initialized)) {
        return false;
    }
    
    // Simple, working logic without complex retry loops
    main_state_t current_state = atomic_load(&g_main_state);
    main_state_t new_state = current_state;  // Default: no change
    
    // State + Event + Condition → New State
    switch (current_state) {
        case MAIN_STATE_INIT:
            switch (event) {
                case MAIN_EVENT_INIT_COMPLETE_CORE0:
                    if (check_core0_initialization_complete()) {
                        new_state = MAIN_STATE_CONFIGURATION;
                        //change substates, too
                        atomic_store(&g_core0_substate, CORE0_CONFIG_UART);
                        atomic_store(&g_core1_substate, CORE1_CONFIG_NET);
                    }
                    break;
                case MAIN_EVENT_INIT_COMPLETE_CORE1:
                    if (check_core1_initialization_complete()) {
                        new_state = MAIN_STATE_CONFIGURATION;
                        //change substates, too
                        atomic_store(&g_core0_substate, CORE0_CONFIG_UART);
                        atomic_store(&g_core1_substate, CORE1_CONFIG_NET);
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
                case MAIN_EVENT_CONFIG_COMPLETE_CORE0:
                    if (check_core0_configuration_complete()) {
                        new_state = MAIN_STATE_OPERATIONAL;
                        //change substates, too
                        atomic_store(&g_core0_substate, CORE0_IDLE);
                        // Core1 enters CORE1_BUY_UPDATE first to handle TBYB (ADR-017)
                        atomic_store(&g_core1_substate, CORE1_BUY_UPDATE);
                    }
                    break;
                case MAIN_EVENT_CONFIG_COMPLETE_CORE1:
                    if (check_core1_configuration_complete()) {

                        printf("CHANGING TO OPERATIONAL!\r\n");
                        

                        new_state = MAIN_STATE_OPERATIONAL;
                        //change substates, too
                        atomic_store(&g_core0_substate, CORE0_IDLE);
                        // Core1 enters CORE1_BUY_UPDATE first to handle TBYB (ADR-017)
                        atomic_store(&g_core1_substate, CORE1_BUY_UPDATE);
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
                case MAIN_EVENT_REBOOT_REQUESTED:
                    // Transition to REBOOT state (ADR-017)
                    new_state = MAIN_STATE_REBOOT;
                    // Set substates for reboot handling
                    atomic_store(&g_core0_substate, CORE0_REBOOT_IDLE);
                    atomic_store(&g_core1_substate, CORE1_REBOOT_FLUSH);
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
                case MAIN_EVENT_REBOOT_REQUESTED:
                    // Allow reboot from error state (ADR-017)
                    new_state = MAIN_STATE_REBOOT;
                    atomic_store(&g_core0_substate, CORE0_REBOOT_IDLE);
                    atomic_store(&g_core1_substate, CORE1_REBOOT_FLUSH);
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case MAIN_STATE_REBOOT:
            // Reboot state is terminal - no transitions out (device will reboot)
            break;
    }
    
    // Apply state change if needed
    if (new_state != current_state) {
        main_state_t old_state = current_state;
        atomic_store(&g_main_state, new_state);
        
        // Log main state change
        log_event(EVENT_SOURCE_MAIN_STATE, LOG_LEVEL_INFO, 
                  LOG_MAIN_STATE_INIT + new_state, (uint32_t)old_state);
        
        // Wake other core after main state change
        wake_other_core_after_main_state_change(new_state);
    }
    
    return true;  // Event processed successfully
}

/**
 * Process Core0 sub-state machine events (thread-safe atomic operations)
 * 
 * Implements: core0_substate + event + check_condition() → new_core0_substate
 * 
 * @param event The Core0 event to process
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_core0_event(core0_event_t event) {
    if (!atomic_load(&g_initialized)) {
        return false;
    }
    
    core0_substate_t current_state = atomic_load(&g_core0_substate);
    core0_substate_t new_state = current_state;  // Default: no change


    // State + Event + Condition → New State
    switch (current_state) {
        case CORE0_INIT_UART:
            switch (event) {
                case CORE0_EVENT_INIT_UART_COMPLETE:
                    new_state = CORE0_INIT_COMPLETE;
                    break;
                case CORE0_EVENT_INIT_UART_FAILED:
                    new_state = CORE0_INIT_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE0_INIT_COMPLETE:
            // This state sends main state event and transitions to WAIT
            new_state = CORE0_INIT_IDLE;
            break;
            
        case CORE0_INIT_IDLE:
            // Waiting for main state transition to CONFIGURATION
            // No events processed here - main state change drives transition
            break;
            
        case CORE0_INIT_ERROR:
            // Unrecoverable init error - no transitions
            break;
        
        case CORE0_CONFIG_UART:
            // load, verify apply uart config in this state
            switch (event) {
                case CORE0_EVENT_CONFIG_UART_COMPLETE:
                    new_state = CORE0_CONFIG_COMPLETE;
                    break;
                case CORE0_EVENT_CONFIG_UART_FAILED:
                    new_state = CORE0_CONFIG_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
        
        case CORE0_CONFIG_COMPLETE:
            // This state sends main state event and transitions to WAIT
            new_state = CORE0_CONFIG_IDLE;
            break;
        
        case CORE0_CONFIG_IDLE:
            // Waiting for main state transition to OPERATIONAL
            // No events processed here - main state change drives transition
            break;
        
        case CORE0_CONFIG_ERROR:
            // recoverable config error - no transitions
            break;
        
        // New operational states per ADR-012
        case CORE0_IDLE:
            switch (event) {
                case CORE0_EVENT_UART_DATA_READY:
                    new_state = CORE0_UART_ACTIVE;
                    break;
                case CORE0_EVENT_RINGBUFFER_DATA_READY:
                    // Ringbuffer work detected
                    new_state = CORE0_RINGBUFFER_ACTIVE;
                    break;
                case CORE0_EVENT_UART_ERROR:
                    new_state = CORE0_UART_ERROR;
                    break;
                default:
                // Invalid event for this state - write log entry
                    log_event(EVENT_SOURCE_CORE0_SUBSTATE, LOG_LEVEL_WARN, 
                            LOG_CORE0_IDLE_INVALID_EVENT, (uint32_t)event);
                    break;
            }
            break;
            
        case CORE0_UART_ACTIVE:
            switch (event) {
                case CORE0_EVENT_UART_WORK_COMPLETE:
                        new_state = CORE0_IDLE;
                    break;
                case CORE0_EVENT_WORK_IDLE:
                    // Alternative completion event
                    new_state = CORE0_IDLE;
                    break;
                case CORE0_EVENT_UART_ERROR:
                    new_state = CORE0_UART_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE0_RINGBUFFER_ACTIVE:
            switch (event) {
                case CORE0_EVENT_RINGBUFFER_WORK_COMPLETE:
                    // Ringbuffer work completed, return to idle
                    new_state = CORE0_IDLE;
                    break;
                case CORE0_EVENT_WORK_IDLE:
                    // Alternative completion event
                    new_state = CORE0_IDLE;
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
                        new_state = CORE0_IDLE;
                    }
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE0_REBOOT_IDLE:
            // Reboot idle state - just WFI while Core1 handles reboot (ADR-017)
            // No transitions - Core1 will execute the reboot
            break;
    }
    
    // Apply atomic state change if needed
    if (new_state != current_state) {
        core0_substate_t old_state = current_state;
        atomic_store(&g_core0_substate, new_state);
        
        // Log Core0 substate change (skip high-frequency UART_ACTIVE state)
        if ((old_state != CORE0_UART_ACTIVE && new_state != CORE0_UART_ACTIVE) ) {
            log_event(EVENT_SOURCE_CORE0_SUBSTATE, LOG_LEVEL_DEBUG, 
                      LOG_CORE0_INIT_UART + new_state, (uint32_t)old_state);
        }
    }
    else {
        log_event(EVENT_SOURCE_CORE0_SUBSTATE, LOG_LEVEL_DEBUG, 
                    LOG_CORE0_EVENT_WITHOUT_STATE_CHANGE , (uint32_t)current_state);
        log_event(EVENT_SOURCE_CORE0_SUBSTATE, LOG_LEVEL_DEBUG, 
                    LOG_CORE0_EVENT_WITHOUT_STATE_CHANGE , (uint32_t)event);
                    
    }
    
    return true;  // Event processed successfully
}

/**
 * Process Core1 sub-state machine events (thread-safe atomic operations)
 * 
 * Implements: core1_substate + event + check_condition() → new_core1_substate
 * 
 * @param event The Core1 event to process
 * @return true if event processed successfully, false if invalid
 */
bool state_machine_process_core1_event(core1_event_t event) {
    if (!atomic_load(&g_initialized)) {
        return false;
    }
    
    // Simplified for debugging  
    core1_substate_t current_state = atomic_load(&g_core1_substate);
    core1_substate_t new_state = current_state;  // Default: no change
    
    // State + Event + Condition → New State
    switch (current_state) {
        case CORE1_INIT_PERISTENCE:
            switch (event) {
                case CORE1_EVENT_INIT_PERSISTENCE_COMPLETE:
                    new_state = CORE1_INIT_LOGGING;
                    break;
                case CORE1_EVENT_INIT_PERSISTENCE_FAILED:
                    new_state = CORE1_INIT_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_INIT_LOGGING:
            switch (event) {
                case CORE1_EVENT_INIT_LOGGING_COMPLETE:
                    new_state = CORE1_INIT_NET;
                    break;
                case CORE1_EVENT_INIT_LOGGING_FAILED:
                    new_state = CORE1_INIT_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_INIT_NET:
            switch (event) {
                case CORE1_EVENT_INIT_NET_WAIT_FOR_LINK_UP:
                    new_state = CORE1_INIT_WAIT_FOR_LINK;
                    break;
                case CORE1_EVENT_INIT_NET_FAILED:
                    new_state = CORE1_INIT_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;

        case CORE1_INIT_WAIT_FOR_LINK:
            switch (event) {
                case CORE1_EVENT_INIT_NET_LINK_UP:
                    new_state = CORE1_INIT_COMPLETE;
                    break;
                case CORE1_EVENT_INIT_NET_LINK_DOWN:
                    new_state = CORE1_INIT_WAIT_FOR_LINK;
                    break;
                case CORE1_EVENT_INIT_NET_FAILED:
                    new_state = CORE1_INIT_ERROR;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_INIT_COMPLETE:
            // This state sends main state event and transitions to WAIT
            new_state = CORE1_INIT_IDLE;
            break;
            
        case CORE1_INIT_IDLE:
            // Waiting for main state transition to CONFIGURATION
            break;
            
        case CORE1_CONFIG_NET:
            switch (event) {
                case CORE1_EVENT_CONFIG_NET_DHCP_REQUEST:
                    {
                        new_state = CORE1_CONFIG_NET_WAIT_FOR_DHCP;
                    }
                    break;
                case CORE1_EVENT_CONFIG_NET_COMPLETE:
                    {
                        // BUGFIX: Handle static IP configuration complete
                        new_state = CORE1_CONFIG_COMPLETE;
                    }
                    break;
                case CORE1_EVENT_CONFIG_NET_FAILED:
                    {
                        new_state = CORE1_CONFIG_ERROR;
                    }
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
        
        case CORE1_CONFIG_NET_WAIT_FOR_DHCP:
            switch (event) {
                case CORE1_EVENT_NETWORK_RECEIVE_ACTIVE:
                    {
                        new_state = CORE1_CONFIG_NET_CHECK_DHCP;
                    }
                    break;
                case CORE1_EVENT_NETWORK_SENDING_ACTIVE:
                    {
                        new_state = CORE1_CONFIG_NET_CHECK_DHCP;
                    }
                    break;
                case CORE1_EVENT_CONFIG_NET_DHCP_TIMEOUT:
                    {
                        new_state = CORE1_CONFIG_NET;
                    }
                    break;
                case CORE1_EVENT_CONFIG_NET_FAILED:
                    {
                        new_state = CORE1_CONFIG_ERROR;
                    }
                    break;
                case CORE1_EVENT_LOG_START:
                    {
                        new_state = CORE1_CONFIG_LOG_ACTIVE;
                    }
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;

        case CORE1_CONFIG_NET_CHECK_DHCP:
            switch (event) {
                case CORE1_EVENT_NETWORK_SENDING_FINISHED:
                    {
                        new_state = CORE1_CONFIG_NET_WAIT_FOR_DHCP;
                    }
                    break;                
                case CORE1_EVENT_CONFIG_NET_GOT_DHCP:
                    {
                        new_state = CORE1_CONFIG_COMPLETE;
                    }
                    break;
                case CORE1_EVENT_CONFIG_NET_DHCP_TIMEOUT:
                    {
                        new_state = CORE1_CONFIG_NET;
                    }
                    break;
                case CORE1_EVENT_CONFIG_NET_WAIT_DHCP:
                    {
                        new_state = CORE1_CONFIG_NET_WAIT_FOR_DHCP;
                    }
                    break;
                case CORE1_EVENT_CONFIG_NET_FAILED:
                    {
                        new_state = CORE1_CONFIG_ERROR;
                    }
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
        
        case CORE1_CONFIG_COMPLETE:
            // This state sends main state event and transitions to WAIT
            new_state = CORE1_CONFIG_IDLE;
        break;
        
        case CORE1_CONFIG_LOG_ACTIVE:
            switch (event) {
                case CORE1_EVENT_LOG_END:
                    // Return to CORE1_CONFIG_NET_WAIT_FOR_DHCP, will check for more work or sleep
                    new_state = CORE1_CONFIG_NET_WAIT_FOR_DHCP;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;

        case CORE1_CONFIG_IDLE:
            // Waiting for main state transition to CONFIGURATION
            break;
        
        case CORE1_CONFIG_ERROR:
            // revcoverable config error (invalid config)
            break;
        
        case CORE1_NET_DISCONNECTED:
            new_state = CORE1_IDLE; //nothing to do here
            break;
        
        case CORE1_NET_LINK_CHANGE:
            switch (event) {
                case CORE1_EVENT_NETWORK_LINK_UP:          // ADD
                    // Link came up — transition to connected, then idle
                    new_state = CORE1_NET_CONNECTED;        // ADD
                    break;   
                case CORE1_EVENT_NETWORK_LINK_DOWN:
                    // Return to idle, will check for more work or sleep
                    new_state = CORE1_NET_DISCONNECTED;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_NET_CONNECTED:
            new_state = CORE1_IDLE; //nothing to do here
            break;
            
        case CORE1_NET_IDLE:
            new_state = CORE1_IDLE; //nothing to do here
            break;
            
        case CORE1_NET_ACTIVE_RECEIVE:
            switch (event) {
                case CORE1_EVENT_NETWORK_RECEIVE_FINISHED:
                    // Return to idle, will check for more work or sleep
                    new_state = CORE1_IDLE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;

        case CORE1_NET_ACTIVE_SEND:
            switch (event) {
                case CORE1_EVENT_NETWORK_SENDING_FINISHED:
                    // Return to idle, will check for more work or sleep
                    new_state = CORE1_IDLE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_PERSISTENCE_ACTIVE:
            switch (event) {
                case CORE1_EVENT_PERSISTENCE_END:
                    // Return to idle, will check for more work or sleep
                    new_state = CORE1_IDLE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
            
        case CORE1_LOG_ACTIVE:
            switch (event) {
                case CORE1_EVENT_LOG_END:
                    // Return to idle, will check for more work or sleep
                    new_state = CORE1_IDLE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;
        
        case CORE1_IDLE:    //we really were idling around in this state
            switch (event) {
                case CORE1_EVENT_NETWORK_LINK_CHANGE_ACTIVE:
                    new_state = CORE1_NET_LINK_CHANGE;
                    break;
                case CORE1_EVENT_NETWORK_RECEIVE_ACTIVE:
                    //ready to receive
                    new_state = CORE1_NET_ACTIVE_RECEIVE;
                    break;
                case CORE1_EVENT_NETWORK_SENDING_ACTIVE:
                    //ready to send
                    new_state = CORE1_NET_ACTIVE_SEND;
                    break;
                case CORE1_EVENT_PERSISTENCE_START:
                    //ready to save
                    new_state = CORE1_PERSISTENCE_ACTIVE;
                    break;
                case CORE1_EVENT_LOG_START:
                    //ready to print logs
                    new_state = CORE1_LOG_ACTIVE;
                    break;
                case CORE1_EVENT_RINGBUFFER_DATA_READY:
                    //ready to process ringbuffer messages for network transmission
                    new_state = CORE1_RINGBUFFER_ACTIVE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;

        case CORE1_RINGBUFFER_ACTIVE:
            switch (event) {
                case CORE1_EVENT_RINGBUFFER_WORK_COMPLETE:
                    // Return to idle, will check for more work or sleep
                    new_state = CORE1_IDLE;
                    break;
                default:
                    // Invalid event for this state - ignore
                    break;
            }
            break;

        case CORE1_INIT_ERROR:
            new_state = CORE1_SHUTDOWN;  //unrecoverable
            break;
    
        case CORE1_SHUTDOWN:
            break;  //main loop will be stopped in next looping
            
        // Buy update state (ADR-017)
        case CORE1_BUY_UPDATE:
            switch (event) {
                case CORE1_EVENT_BUY_SUCCESS:
                    // Buy succeeded, transition to normal idle
                    new_state = CORE1_IDLE;
                    break;
                case CORE1_EVENT_BUY_FAILED:
                    // Buy failed - this should trigger reboot via main event
                    // Stay in this state until main state changes to REBOOT
                    break;
                default:
                    break;
            }
            break;
            
        // Reboot states (ADR-017)
        case CORE1_REBOOT_FLUSH:
            switch (event) {
                case CORE1_EVENT_REBOOT_FLUSH_COMPLETE:
                    new_state = CORE1_REBOOT_EXECUTE;
                    break;
                default:
                    break;
            }
            break;
            
        case CORE1_REBOOT_EXECUTE:
            // Terminal state - reboot will be executed, no transitions
            break;
    }
    
    // Apply atomic state change if needed
    if (new_state != current_state) {
        core1_substate_t old_state = current_state;
        atomic_store(&g_core1_substate, new_state);
        
        // Log Core1 substate change (skip high-frequency network states)
        if (old_state != CORE1_CONFIG_NET_CHECK_DHCP && new_state != CORE1_CONFIG_NET_CHECK_DHCP &&
            old_state != CORE1_LOG_ACTIVE && new_state != CORE1_LOG_ACTIVE &&
            old_state != CORE1_CONFIG_LOG_ACTIVE && new_state != CORE1_CONFIG_LOG_ACTIVE &&
            old_state != CORE1_NET_ACTIVE_RECEIVE && new_state != CORE1_NET_ACTIVE_RECEIVE &&
            old_state != CORE1_RINGBUFFER_ACTIVE && new_state != CORE1_RINGBUFFER_ACTIVE 
        ) {
            log_event(EVENT_SOURCE_CORE1_SUBSTATE, LOG_LEVEL_DEBUG, 
                      LOG_CORE1_INIT_PERISTENCE + new_state, (uint32_t)old_state);
        }
    }
    
    return true;  // Event processed successfully
}

// Event validation functions (security-critical)

/**
 * Validate main state machine event
 */
static bool is_valid_main_event(main_state_event_t event) {
    return (event >= MAIN_EVENT_INIT_COMPLETE_CORE0 && event <= MAIN_EVENT_REBOOT_REQUESTED);
}

/**
 * Validate Core0 state machine event
 */
static bool is_valid_core0_event(core0_event_t event) {
    return (event >= CORE0_EVENT_UART_DATA_READY && event <= CORE0_EVENT_ERROR_RECOVERED);
}

/**
 * Validate Core1 state machine event
 */
static bool is_valid_core1_event(core1_event_t event) {
    return (event >= CORE1_EVENT_INIT_PERSISTENCE_COMPLETE && event <= CORE1_EVENT_REBOOT_FLUSH_COMPLETE);
}

// Condition checking functions (security-hardened)

/**
 * Check if system initialization is complete
 */
static bool check_core0_initialization_complete(void) {
    // This allows transition FROM INIT TO CONFIGURATION from core0 code (core1 must be CORE1_INIT_IDLE)
    DEBUG_ONLY({
        printf("check_core0_initialization_complete: \ng_core0_substate: %d\ng_core1_substate: %d\n", atomic_load(&g_core0_substate),atomic_load(&g_core1_substate));
    });
    return atomic_load(&g_initialized) &&
           atomic_load(&g_main_state) == MAIN_STATE_INIT &&
           (atomic_load(&g_core1_substate) == CORE1_INIT_IDLE) &&
           (atomic_load(&g_core0_substate) == CORE0_INIT_COMPLETE || atomic_load(&g_core0_substate) == CORE0_INIT_IDLE);
}

/**
 * Check if system initialization is complete
 */
static bool check_core1_initialization_complete(void) {
    // This allows transition FROM INIT TO CONFIGURATION from core1 code (core must be CORE0_INIT_IDLE)
    DEBUG_ONLY({
        printf("check_core1_initialization_complete: \ng_core0_substate: %d\ng_core1_substate: %d\n", atomic_load(&g_core0_substate),atomic_load(&g_core1_substate));
    });
    return atomic_load(&g_initialized) &&
           atomic_load(&g_main_state) == MAIN_STATE_INIT &&
           (atomic_load(&g_core1_substate) == CORE1_INIT_COMPLETE || atomic_load(&g_core1_substate) == CORE1_INIT_IDLE) &&
           (atomic_load(&g_core0_substate) == CORE0_INIT_IDLE);
}

/**
 * Check if system initialization is complete
 */
static bool check_core0_configuration_complete(void) {
    // This allows transition FROM CONFIGURATION TO OPERATIONAL from core0 code (core1 must be CORE1_INIT_IDLE)
    DEBUG_ONLY({
        printf("check_core0_configuration_complete: \ng_core0_substate: %d\ng_core1_substate: %d\n", atomic_load(&g_core0_substate),atomic_load(&g_core1_substate));
    });
    return atomic_load(&g_initialized) &&
           atomic_load(&g_main_state) == MAIN_STATE_CONFIGURATION &&
           (atomic_load(&g_core1_substate) == CORE1_CONFIG_IDLE) &&
           (atomic_load(&g_core0_substate) == CORE0_CONFIG_COMPLETE || atomic_load(&g_core0_substate) == CORE0_CONFIG_IDLE);
}

/**
 * Check if system initialization is complete
 */
static bool check_core1_configuration_complete(void) {
    // This allows transition FROM CONFIGURATION TO OPERATIONAL from core1 code (core1 must be CORE0_INIT_IDLE)
    DEBUG_ONLY({
        printf("check_core1_configuration_complete: \ng_core0_substate: %d\ng_core1_substate: %d\n", atomic_load(&g_core0_substate),atomic_load(&g_core1_substate));
    });
    return atomic_load(&g_initialized) &&
           atomic_load(&g_main_state) == MAIN_STATE_CONFIGURATION &&
           (atomic_load(&g_core1_substate) == CORE1_CONFIG_COMPLETE || atomic_load(&g_core1_substate) == CORE1_CONFIG_IDLE) &&
           (atomic_load(&g_core0_substate) == CORE0_CONFIG_IDLE);
}


/**
 * Check if configuration is valid
 * SECURITY: This function must validate configuration integrity
 */
static bool check_configuration_valid(void) {
    // SECURITY TODO: Replace with real implementation
    // Should check: configuration checksum, bounds validation, etc.
    
    // For testing: simply return true, but only if we're actually in CONFIGURATION state
    // This allows transition FROM CONFIGURATION TO OPERATIONAL
    return atomic_load(&g_main_state) == MAIN_STATE_CONFIGURATION;
}

/**
 * Check if error recovery is complete
 * SECURITY: This function must verify all systems are operational
 */
static bool check_error_recovery_complete(void) {
    // SECURITY TODO: Replace with real implementation
    // Should verify: all hardware operational, no error conditions, etc.
    
    // For testing: verify we're currently in ERROR state  
    // This allows transition FROM ERROR TO OPERATIONAL
    return atomic_load(&g_main_state) == MAIN_STATE_ERROR;
}

/**
 * Check if UART data is available
 * SECURITY: This function must validate UART hardware status
 */
static bool check_uart_data_available(void) {
    // SECURITY TODO: Replace with real implementation
    // Should check: UART FIFO status, hardware ready, etc.
    
    // For testing: return true to allow UART processing in any main state
    return true;
}

/**
 * Check if UART processing is complete
 * SECURITY: This function must verify UART processing status
 */
static bool check_uart_processing_complete(void) {
    // SECURITY TODO: Replace with real implementation
    // Should check: UART transmission complete, buffer empty, etc.
    
    // For testing: verify we're currently in UART_ACTIVE state
    // This allows transition FROM UART_ACTIVE TO UART_IDLE
    return atomic_load(&g_core0_substate) == CORE0_UART_ACTIVE;
}

/**
 * Check if UART hardware has recovered from error
 * SECURITY: This function must verify UART hardware status
 */
static bool check_uart_hardware_recovered(void) {
    // SECURITY TODO: Replace with real implementation
    // Should verify: UART error registers clear, hardware reset complete, etc.
    
    // For testing: verify we're currently in UART_ERROR state
    // This allows transition FROM UART_ERROR TO UART_IDLE
    return atomic_load(&g_core0_substate) == CORE0_UART_ERROR;
}


/**
 * Check if main state is operational
 */
static bool check_main_state_operational(void) {
    return atomic_load(&g_main_state) == MAIN_STATE_OPERATIONAL;
}

/*
 * Cross-core wake-up via SIO doorbells (ADR-007, "Cross-Core Wake-Up").
 *
 * The inter-core FIFO is NOT used here. flash_safe_execute_core_init() hands
 * the FIFO IRQ on both cores to the SDK lockout handler, and the SDK requires
 * the FIFO to be used for nothing else once lockout is active. Doorbells are a
 * separate SIO resource with their own IRQ (SIO_IRQ_BELL), so ringing one
 * cannot interfere with flash_safe_execute().
 */

/**
 * Map a wake direction to its doorbell number.
 */
static int doorbell_for_direction(wake_direction_t direction) {
    if (direction == CORE0_WAKES_CORE1) {
        return doorbell_core0_wakes_core1;
    }
    return doorbell_core1_wakes_core0;
}

/**
 * The doorbell the calling core rings on the other core.
 * Fixed by the core number so that a caller cannot select a doorbell that
 * the receiving core's ISR would not expect (ADR-007, "Idle Wait Instruction
 * and Doorbell Selection", problem 2).
 */
static int doorbell_rung_by_this_core(void) {
    if (get_core_num() == 0) {
        return doorbell_for_direction(CORE0_WAKES_CORE1);
    }
    return doorbell_for_direction(CORE1_WAKES_CORE0);
}

/**
 * Clear every doorbell this module has claimed on the calling core.
 * Both doorbells share SIO_IRQ_BELL on each core, so clearing both is what
 * guarantees the IRQ does not re-fire.
 */
static void clear_wake_doorbells_on_this_core(void) {
    if (doorbell_core0_wakes_core1 >= 0) {
        multicore_doorbell_clear_current_core((uint)doorbell_core0_wakes_core1);
    }
    if (doorbell_core1_wakes_core0 >= 0) {
        multicore_doorbell_clear_current_core((uint)doorbell_core1_wakes_core0);
    }
}

/**
 * Wake the other core when the main state changes.
 * Both cores may sleep on a main state change, so wake whichever is "other".
 */
static void wake_other_core_after_main_state_change(main_state_t new_state) {
    (void)new_state;
    wake_other_core();
}

/**
 * Wake the other core from state_machine_wait_for_wake().
 *
 * Setting a doorbell is a single register write: non-blocking, idempotent,
 * safe while holding a mutex (ringbuffer_enqueue_entry() calls this) and
 * safe from ISR context. The woken core re-checks its work conditions; a
 * doorbell carries no payload.
 */
void wake_other_core(void) {
    int doorbell = doorbell_rung_by_this_core();
    if (doorbell < 0) {
        return;  // state_machine_init() not run or claim failed; nothing to ring
    }
    multicore_doorbell_set_other_core((uint)doorbell);
}

/**
 * Idle wait for both cores (ADR-007, "Idle Wait Instruction and Doorbell
 * Selection"). See the header for the contract.
 *
 * WFE, not WFI: every exception taken on this core sets the core's event
 * register, and WFE returns immediately (clearing the register) when it is
 * set. An IRQ serviced between the caller's work check and this call is
 * therefore not lost. WFI would only look at interrupts still pending at
 * this instant and would sleep although work is queued.
 */
void state_machine_wait_for_wake(void) {
    __wfe();
}

/**
 * Doorbell IRQ handler. Runs on the core that was rung.
 * Clears every claimed doorbell on this core, so the IRQ does not re-fire
 * even if a doorbell of the "wrong" direction was set. The purpose of the
 * interrupt is solely to terminate the core's idle wait; it does no work.
 */
static void wake_doorbell_irq_handler(void) {
    clear_wake_doorbells_on_this_core();
}

/**
 * Enable the doorbell IRQ on the calling core.
 */
void state_machine_enable_wake_irq(void) {
    if (doorbell_core0_wakes_core1 < 0 || doorbell_core1_wakes_core0 < 0) {
        return;  // state_machine_init() not run or claim failed
    }
    // Every doorbell maps to the same IRQ line on a core (SIO_IRQ_BELL).
    uint irq_num = multicore_doorbell_irq_num((uint)doorbell_core0_wakes_core1);
    // Start clean: a doorbell rung before the IRQ was enabled would otherwise
    // fire once immediately, which is harmless but noisy.
    clear_wake_doorbells_on_this_core();
    irq_set_exclusive_handler(irq_num, wake_doorbell_irq_handler);
    irq_set_enabled(irq_num, true);
}

/**
 * Doorbell number for a direction (diagnostics, tests).
 */
int state_machine_get_wake_doorbell(wake_direction_t direction) {
    return doorbell_for_direction(direction);
}