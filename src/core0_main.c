/**
 * @file core0_main.c
 * @brief Core0 main function with clean state machine architecture
 * 
 * Implements a clean state machine pattern for Core0 (matching Core1 structure):
 * - One main while(true) loop
 * - State-driven switch statement architecture
 * - Each sub-state calls exactly one function
 * - UART processing happens continuously in OPERATIONAL state
 * - WFI in idle states for power efficiency
 * - Cross-core FIFO synchronization
 * 
 * Architecture:
 * 1. switch(main_state) - handles each main state
 * 2. switch(sub_state) - handles each sub-state within main state  
 * 3. Single function call per sub-state - clean separation of concerns
 * 4. FIFO wake-up mechanism for cross-core synchronization
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Core 0 UART Subsystem
 */

#include "state_machine.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "ringbuffer.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include <stdio.h>
#include "debug.h"

// Forward declarations for Core0 state functions
static void core0_initialize(void);

// MAIN_STATE_INIT functions
static void core0_init_uart_hardware(void);
static void core0_init_complete(void);

// MAIN_STATE_CONFIGURATION functions
static void core0_load_configuration(void); 
static void core0_configuration_complete(void); 

// Universal wait function
static void core0_idle_wait(void);

// MAIN_STATE_OPERATIONAL functions
void core0_process_uart(void);
static void core0_handle_uart_error(void);

// New Core0 work detection functions (ADR-012)
bool core0_check_for_pending_work(void);
void core0_work_or_idle_wait(void);
void core0_process_ringbuffer(void);

// MAIN_STATE_ERROR functions
static void core0_handle_error(void);

// UART processing helper functions
static bool initialize_uart_hardware(void);
static bool process_uart_data(void);
static bool attempt_uart_recovery(void);
static bool perform_error_recovery(void);

// State tracking for transitions
static bool g_uart_initialized = false;
static uint32_t g_uart_processing_cycles = 0;
static uint32_t g_uart_recovery_attempts = 0;
static uint32_t g_system_recovery_attempts = 0;

/**
 * @brief Clean Core0 main function with simple state machine
 * 
 * Single while loop with state-driven switch statement architecture.
 * Each sub-state calls exactly one function for clean separation.
 * STATE CHANGES SHOULD ONLY OCCUR BY state_machine_process_main_event() or state_machine_process_core0_event()
 */
void core0_main(void) {
    // One-time initialization

    DEBUG_ONLY({
        printf("ENTERING CORE0 MAIN\n");
    });

    core0_initialize();
    
    // Get current states
    main_state_t main_state = state_machine_get_main_state();
    core0_substate_t sub_state = state_machine_get_core0_substate();

    while (true) {
        // Get current states
        main_state = state_machine_get_main_state();
        sub_state = state_machine_get_core0_substate();
        
        // DEBUG: Print states periodically
        static uint32_t debug_counter = 0;
        debug_counter++;
        DEBUG_ONLY({
            if (debug_counter % 50000 == 0) {  // Every 50k loops
                printf("DEBUG: Core0 main_state=%d, sub_state=%d\n", main_state, sub_state);
            }
        });
        
        // Big switch statement for main states
        switch (main_state) {
            case MAIN_STATE_INIT:
                // Initialization phase - set up UART hardware
                switch (sub_state) {
                    case CORE0_INIT_UART:
                        core0_init_uart_hardware();
                        break;
                    case CORE0_INIT_COMPLETE:
                        core0_init_complete();
                        break;
                    case CORE0_INIT_IDLE:
                        core0_idle_wait();  // Universal wait function
                        break;
                    case CORE0_INIT_ERROR:
                        state_machine_process_main_event(MAIN_EVENT_SYSTEM_ERROR);
                        break;
                    default:
                        break;
                }
                break;
                
            case MAIN_STATE_CONFIGURATION:
                // Configuration loading phase - Core0 waits for Core1 to complete
                switch (sub_state) {
                    case CORE0_CONFIG_UART:
                        core0_load_configuration(); 
                        break;
                    case CORE0_CONFIG_COMPLETE:
                        core0_configuration_complete();
                        break;
                    case CORE0_CONFIG_IDLE:
                        core0_idle_wait();  // Universal wait function
                        break;
                    case CORE0_CONFIG_ERROR:
                        core0_idle_wait();  // wait for config changes
                        break;
                    
                    default:
                        break;
                }
                break;
                
            case MAIN_STATE_OPERATIONAL:
                // Normal operation - process based on sub-state
                switch (sub_state) {
                    case CORE0_IDLE:
                        core0_work_or_idle_wait();  // Check for work or sleep (ADR-012)
                        break;
                    case CORE0_UART_ACTIVE:
                        core0_process_uart();  // Active UART processing
                        break;
                    case CORE0_RINGBUFFER_ACTIVE:
                        core0_process_ringbuffer();  // Active ringbuffer processing
                        break;
                    case CORE0_UART_ERROR:
                        core0_handle_uart_error();  // Handle UART errors
                        break;
                    default:
                        break;
                }
                break;
                
            case MAIN_STATE_ERROR:
                // Error handling
                core0_handle_error();
                break;
        }
    }
}

// Core0 initialization and state management functions

/**
 * @brief One-time Core0 initialization
 */
static void core0_initialize(void) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE0_STARTING, 0);
    
    // Reset state tracking
    g_uart_initialized = false;
    g_uart_processing_cycles = 0;
    g_uart_recovery_attempts = 0;
    g_system_recovery_attempts = 0;
    
    enable_doorbell_irq(CORE0_WAKES_CORE1);
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
}

// MAIN_STATE_INIT function implementations

/**
 * @brief Initialize UART hardware and subsystems
 */
static void core0_init_uart_hardware(void) {
    if (g_uart_initialized) {
        return;  // Already done
    }
    
    bool result = initialize_uart_hardware();

    if (result) {
        g_uart_initialized = true;
        log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_HW_INIT, 0);
        // Generate event to move to next phase
        state_machine_process_core0_event(CORE0_EVENT_INIT_UART_COMPLETE);
    } else {
        log_event(EVENT_SOURCE_UART0, LOG_LEVEL_ERROR, LOG_EVENT_UART0_ERROR, 1);
        state_machine_process_core0_event(CORE0_EVENT_INIT_UART_FAILED);
    }
}

/**
 * @brief Complete Core0 initialization phase
 */
static void core0_init_complete(void) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
    // Signal that Core0 is ready (this may trigger main state transition)
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    //this will sleep if other core not yet ready
    state_machine_process_core0_event(CORE0_EVENT_INIT_UART_COMPLETE);
}

// Universal wait function - handles cross-core wake-up only

/**
 * @brief Universal wait function for all idle/wait states
 * 
 * Handles cross-core wake-up messages and power-efficient waiting.
 * Work detection is now handled by core0_work_or_idle_wait() per ADR-012.
 * This function only handles the actual waiting/sleeping part.
 */
static void core0_idle_wait(void) {
    // Wait for interrupt - power efficient
    // Work detection is handled elsewhere in the new architecture
    __wfi();
}

// MAIN_STATE_CONFIGURATION function implementations

/**
 * @brief Initialize UART hardware and subsystems
 */
static void core0_load_configuration(void) {
    
    bool result = true; //TBD: implement UART config validation and apply
    
    if (result) {
        // Generate event to move to next phase
        state_machine_process_core0_event(CORE0_EVENT_CONFIG_UART_COMPLETE);
    } else {
        state_machine_process_core0_event(CORE0_EVENT_CONFIG_UART_FAILED);
    }
}

/**
 * @brief Complete Core0 configuration phase
 */
static void core0_configuration_complete(void) {
    // Signal that Core0 is ready (this may trigger main state transition)
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE0);
    //this will sleep if other core not yet ready
    state_machine_process_core0_event(CORE0_EVENT_CONFIG_UART_COMPLETE);
}



/**
 * @brief Process UART hardware operations only (ADR-012)
 * 
 * Handles UART hardware processing including data RX/TX, DMA transfers,
 * and interrupt handling. Ringbuffer processing has been moved to
 * core0_process_ringbuffer() per ADR-012 separation of concerns.
 * 
 * Documentation Reference:
 * - ADR-012: Core0 Ringbuffer Processing Separation  
 */
void core0_process_uart(void) {
    // TBD: implement after rinbuffer intergration sucessfull
}

/**
 * @brief Handle UART error conditions
 */
static void core0_handle_uart_error(void) {
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_WARN, LOG_EVENT_UART_RECOVERY, g_uart_recovery_attempts);
    
    if (attempt_uart_recovery()) {
        log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_RECOVERY, 1);
        // Recovery successful, generate recovery event
        state_machine_process_core0_event(CORE0_EVENT_ERROR_RECOVERED);
        log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 0);
        g_uart_recovery_attempts = 0;  // Reset counter
    } else {
        g_uart_recovery_attempts++;
        if (g_uart_recovery_attempts > 5) {
            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_ERROR, LOG_EVENT_UART_RECOVERY, 0);
            state_machine_process_main_event(MAIN_EVENT_SYSTEM_ERROR);
            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_ERROR, LOG_EVENT_UART0_ERROR, 0);
        }
        sleep_ms(100);  // Brief delay before retry
    }
}

// MAIN_STATE_ERROR function implementation

/**
 * @brief Handle error state and recovery
 */
static void core0_handle_error(void) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_ERROR_RECOVERY, g_system_recovery_attempts);
    DEBUG_ONLY({
        printf("Core0: Error state - attempting recovery\n");
    });
    
    if (perform_error_recovery()) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_ERROR_RECOVERY, 1);
        state_machine_process_main_event(MAIN_EVENT_ERROR_RECOVERED);
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
        g_system_recovery_attempts = 0;  // Reset counter
    } else {
        g_system_recovery_attempts++;
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_ERROR_RECOVERY, 0);
        if (g_system_recovery_attempts > 3) {
            log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_WATCHDOG_RESET, 0);
        }
        sleep_ms(2000);  // Delay before retry
    }
}

// UART processing helper function implementations

/**
 * Initialize UART hardware
 * @return true if successful, false otherwise
 */
static bool initialize_uart_hardware(void) {
    // Real implementation would:
    // - Initialize all 4 UART channels
    // - Configure baud rates, data bits, stop bits, parity
    // - Set up interrupt handlers for UART RX data available
    // - Configure DMA channels
    // - Set up interrupt to change substate from UART_IDLE to UART_ACTIVE when data arrives
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_CHANNELS, 4);
    return true;  // Stub - always succeeds for testing
}

/**
 * Process available UART data
 * @return true if processing complete, false if still processing
 */
static bool process_uart_data(void) {
    // Simulate processing time
    if (g_uart_processing_cycles == 0) {
        log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_PROCESSING, 3);
        g_uart_processing_cycles = 3;  // Simulate 3 cycles of processing
        return false;
    }
    
    g_uart_processing_cycles--;
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_PROCESSING, g_uart_processing_cycles);
    
    if (g_uart_processing_cycles == 0) {
        log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_COMPLETE, 0);
        return true;  // Processing complete
    }
    
    return false;  // Still processing
}

/**
 * Attempt UART hardware recovery
 * @return true if recovery successful, false otherwise
 */
static bool attempt_uart_recovery(void) {
    // Simulate recovery attempts with success after 3 attempts
    static uint32_t local_attempts = 0;
    local_attempts++;
    
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_WARN, LOG_EVENT_UART_RECOVERY, local_attempts);
    
    if (local_attempts >= 3) {
        local_attempts = 0;  // Reset for next error
        return true;
    }
    
    return false;
}

/**
 * Perform system error recovery
 * @return true if recovery successful, false otherwise
 */
static bool perform_error_recovery(void) {
    // Simulate system recovery with success after 2 attempts
    static uint32_t local_attempts = 0;
    local_attempts++;
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_ERROR_RECOVERY, local_attempts);
    
    if (local_attempts >= 2) {
        local_attempts = 0;  // Reset for next error
        return true;
    }
    
    return false;
}

// ==================== NEW CORE0 WORK DETECTION FUNCTIONS (ADR-012) ====================

/**
 * @brief Check for pending work and fire appropriate events
 * 
 * Follows Core1 pattern for work detection. Prioritizes UART work over ringbuffer work
 * because UART has limited FIFO while ringbuffer is already buffered.
 * 
 * @return true if work found and event fired, false if no work pending
 */
bool core0_check_for_pending_work(void) {
    // Priority 1: Check for UART hardware work (limited FIFO, data might be lost)
    // TODO: Replace with real UART hardware check when available
    // if (uart_hardware_has_pending_work()) {
    //     state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
    //     return true;
    // }
    
    // Priority 2: Check for ringbuffer messages to process (buffered, safer to delay)
    if (ringbuffer_get_count(RX_TCP_TO_UART) > 0) {
        state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_DATA_READY);
        return true;
    }
    
    // No work pending
    return false;
}

/**
 * @brief Check for work or go to idle wait (following Core1 pattern)
 * 
 * This is the main work dispatcher for Core0 operational state.
 * Matches the Core1 architecture with work_or_idle_wait pattern.
 */
void core0_work_or_idle_wait(void) {

    // Check for work first
    if (core0_check_for_pending_work()) {
        return; // Work found, event fired, let state machine handle it
    }
    
    // No work, go to sleep
    core0_idle_wait();
}

/**
 * @brief Process ringbuffer messages only (separated from UART hardware)
 * 
 * Moved from core0_process_uart() per ADR-012 separation of concerns.
 * Processes TCP→UART messages from ringbuffer and echoes them back as UART→TCP.
 * Only processes ONE message per call to prevent blocking.
 */
void core0_process_ringbuffer(void) {
    static uint32_t call_counter = 0;
    call_counter++;

    DEBUG_ONLY({
        printf("DEBUG: core0_process_ringbuffer() call #%u\n", call_counter);
    });

    // Ring Buffer Processing: Turn around TCP→UART messages (Issue #68)
    // Process only ONE message per main loop execution (test requirement #7)
    ring_entry_t* tcp_to_uart_msg = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
    if (tcp_to_uart_msg) {
    
        // "Turn around" the message: change direction from TCP→UART to UART→TCP
        tcp_to_uart_msg->direction = RX_UART_TO_TCP;
        tcp_to_uart_msg->status = ENTRY_STATUS_FILLING;

        // Re-enqueue the message for Core1 to transmit back to TCP client
        bool enqueue_result = ringbuffer_enqueue_entry(tcp_to_uart_msg);
        if (enqueue_result) {
            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_COMPLETE, 1);
        } else {
            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_WARN, LOG_EVENT_UART0_ERROR, 1);
        }
    }
    
    // Work complete - return to IDLE to allow main loop to check for more work
    state_machine_process_core0_event(CORE0_EVENT_RINGBUFFER_WORK_COMPLETE);
}
