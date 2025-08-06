/**
 * @file core0_main.c
 * @brief Core0 main function for UART processing
 * 
 * Implements event-driven main loop for Core0 responsible for UART processing.
 * Uses state machine queries to drive control logic and generates events based
 * on UART hardware status and data availability.
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Core 0 UART Subsystem
 */

#include "state_machine.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include <stdio.h>

// Forward declarations for UART processing functions
static bool initialize_uart_hardware(void);
static bool uart_data_available(void);
static bool process_uart_data(void);
static bool attempt_uart_recovery(void);
static bool perform_error_recovery(void);

/**
 * Core0 main function - UART processing with event-driven state machine
 * 
 * Runs the main event-driven control loop for UART processing.
 * Queries state machine and generates events based on hardware status.
 */
void core0_main(void) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE0_STARTING, 0);
    
    while (true) {
        // Query current states (non-blocking)
        main_state_t main_state = state_machine_get_main_state();
        core0_substate_t sub_state = state_machine_get_core0_substate();
        
        // State-driven control logic with event generation
        switch (main_state) {
            case MAIN_STATE_INIT:
                // System initialization phase
                log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_DEBUG, LOG_EVENT_INIT_PHASE, 0);
                if (initialize_uart_hardware()) {
                    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_HW_INIT, 0);
                    // Send event to transition main state
                    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE);
                    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
                } else {
                    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_ERROR, LOG_EVENT_UART0_ERROR, 1);
                    state_machine_process_main_event(MAIN_EVENT_SYSTEM_ERROR);
                }
                break;
                
            case MAIN_STATE_CONFIGURATION:
                // Configuration loading phase - Core0 waits for Core1 to complete
                log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_DEBUG, LOG_EVENT_CONFIG_PHASE, 0);
                // Core0 doesn't drive configuration loading, just waits
                break;
                
            case MAIN_STATE_OPERATIONAL:
                // Normal operation - process based on sub-state
                switch (sub_state) {
                    case CORE0_UART_IDLE:
                        // Check for incoming UART data
                        if (uart_data_available()) {
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_DATA_AVAIL, 0);
                            // Generate event to start UART processing
                            state_machine_process_core0_event(CORE0_EVENT_UART_DATA_READY);
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART0_DATA_RX, 0);
                        }
                        break;
                        
                    case CORE0_UART_ACTIVE:
                        // Process UART data
                        if (process_uart_data()) {
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_COMPLETE, 0);
                            // Processing complete, generate idle event
                            state_machine_process_core0_event(CORE0_EVENT_UART_IDLE);
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART0_DATA_TX, 1);
                        }
                        break;
                        
                    case CORE0_UART_ERROR:
                        // Handle UART errors
                        if (attempt_uart_recovery()) {
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_RECOVERY, 1);
                            // Recovery successful, generate recovery event
                            state_machine_process_core0_event(CORE0_EVENT_ERROR_RECOVERED);
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 0);
                        } else {
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_ERROR, LOG_EVENT_UART_RECOVERY, 0);
                            state_machine_process_main_event(MAIN_EVENT_SYSTEM_ERROR);
                            log_event(EVENT_SOURCE_UART0, LOG_LEVEL_ERROR, LOG_EVENT_UART0_ERROR, 0);
                        }
                        break;
                }
                break;
                
            case MAIN_STATE_ERROR:
                // System error recovery
                if (perform_error_recovery()) {
                    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_ERROR_RECOVERY, 1);
                    state_machine_process_main_event(MAIN_EVENT_ERROR_RECOVERED);
                    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
                } else {
                    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_ERROR_RECOVERY, 0);
                    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_WATCHDOG_RESET, 0);
                }
                break;
        }
        
        // Core0 main loop frequency: 10ms for responsive UART processing
        sleep_ms(10);
    }
}

// UART processing function implementations (stubs for now)

/**
 * Initialize UART hardware
 * @return true if successful, false otherwise
 */
static bool initialize_uart_hardware(void) {
    // Stub implementation - always succeeds for testing
    // Real implementation would:
    // - Initialize all 4 UART channels
    // - Configure baud rates, data bits, stop bits, parity
    // - Set up interrupt handlers
    // - Configure DMA channels
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_CHANNELS, 4);
    return true;
}

/**
 * Check if UART data is available
 * @return true if data available, false otherwise
 */
static bool uart_data_available(void) {
    // Stub implementation - simulate periodic data availability
    static uint32_t counter = 0;
    counter++;
    
    // Simulate data available every 50 iterations (~500ms at 10ms loop)
    if (counter % 50 == 0) {
        return true;
    }
    return false;
}

/**
 * Process available UART data
 * @return true if processing complete, false if still processing
 */
static bool process_uart_data(void) {
    // Stub implementation - simulate processing time
    static uint32_t processing_cycles = 0;
    
    if (processing_cycles == 0) {
        log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_PROCESSING, 3);
        processing_cycles = 3;  // Simulate 3 cycles of processing
        return false;
    }
    
    processing_cycles--;
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_DEBUG, LOG_EVENT_UART_PROCESSING, processing_cycles);
    
    if (processing_cycles == 0) {
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
    // Stub implementation - simulate recovery attempts
    static uint32_t recovery_attempts = 0;
    
    recovery_attempts++;
    log_event(EVENT_SOURCE_UART0, LOG_LEVEL_WARN, LOG_EVENT_UART_RECOVERY, recovery_attempts);
    
    // Simulate successful recovery after 3 attempts
    if (recovery_attempts >= 3) {
        recovery_attempts = 0;  // Reset for next error
        return true;
    }
    
    return false;
}

/**
 * Perform system error recovery
 * @return true if recovery successful, false otherwise
 */
static bool perform_error_recovery(void) {
    // Stub implementation - simulate system recovery
    static uint32_t system_recovery_attempts = 0;
    
    system_recovery_attempts++;
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_ERROR_RECOVERY, system_recovery_attempts);
    
    // Simulate successful recovery after 2 attempts
    if (system_recovery_attempts >= 2) {
        system_recovery_attempts = 0;  // Reset for next error
        return true;
    }
    
    return false;
}