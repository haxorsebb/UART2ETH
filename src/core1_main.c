/**
 * @file core1_main.c
 * @brief Core1 main function for network, persistence, and log processing
 * 
 * Implements event-driven main loop for Core1 responsible for network operations,
 * flash persistence, and log processing. Uses real log manager functions for
 * efficient batch processing of pending log events.
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 */

#include "state_machine.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include <stdio.h>

// Forward declarations for Core1 processing functions
static bool initialize_network_interface(void);
static bool check_network_status(void);
static void process_network_operations(void);
static bool persistence_needed(void);

/**
 * Core1 main function - Network, persistence, and log processing
 * 
 * Runs the main event-driven control loop for Core1 operations.
 * Handles network processing, configuration persistence, and log formatting.
 * Uses real log manager functions for efficient batch processing.
 */
void core1_main(void) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_STARTING, 0);
    
    // Initialize network interface
    if (!initialize_network_interface()) {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 1);
        state_machine_process_main_event(MAIN_EVENT_SYSTEM_ERROR);
    } else {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 0);
        // Generate network up event
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_UP);
    }
    
    // Log Core1 startup
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 1);
    
    while (true) {
        // Query current states (non-blocking)
        main_state_t main_state = state_machine_get_main_state();
        core1_substate_t sub_state = state_machine_get_core1_substate();
        
        // State-driven control logic with event generation
        switch (main_state) {
            case MAIN_STATE_INIT:
                // Wait for Core0 to complete hardware initialization
                log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_DEBUG, LOG_EVENT_INIT_PHASE, 1);
                break;
                
            case MAIN_STATE_CONFIGURATION:
                // Configuration loading phase - Core1 drives this
                log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_CONFIG_PHASE, 0);
                
                // Simulate configuration loading
                static uint32_t config_loading_cycles = 0;
                config_loading_cycles++;
                
                if (config_loading_cycles >= 5) {  // Simulate 5 cycles to load config
                    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_CONFIG_LOADED, config_loading_cycles);
                    state_machine_process_main_event(MAIN_EVENT_CONFIG_LOADED);
                    config_loading_cycles = 0;  // Reset for next time
                }
                break;
                
            case MAIN_STATE_OPERATIONAL:
                // HIGH PRIORITY: Network processing
                process_network_operations();
                
                // MEDIUM PRIORITY: Persistence operations
                if (persistence_needed()) {
                    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_START, 0);
                    state_machine_process_core1_event(CORE1_EVENT_PERSISTENCE_START);
                    
                    // Perform persistence operation
                    flash_persistence_save_configuration_if_needed();
                    
                    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_END, 0);
                    state_machine_process_core1_event(CORE1_EVENT_PERSISTENCE_END);
                    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_CONFIG_SAVED, 0);
                }
                break;
                
            case MAIN_STATE_ERROR:
                // Error state - limited operations
                log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_ERROR_RECOVERY, 0);
                break;
        }
        
        // LOW PRIORITY: Process all pending log events (efficient batch processing)
        if (log_manager_get_pending_count() > 0) {
            state_machine_process_core1_event(CORE1_EVENT_LOG_START);
            
            // Process all pending log events using real log manager function
            uint32_t formatted_count = log_manager_format_pending();
            
            state_machine_process_core1_event(CORE1_EVENT_LOG_END);
            
            
        }
        
        // Network status monitoring and event generation
        switch (sub_state) {
            case CORE1_NET_DISCONNECTED:
                if (check_network_status()) {
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_UP, 0);
                    state_machine_process_core1_event(CORE1_EVENT_NETWORK_UP);
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_CONNECT, 0);
                }
                break;
                
            case CORE1_NET_IDLE:
                if (!check_network_status()) {
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_DOWN, 0);
                    state_machine_process_core1_event(CORE1_EVENT_NETWORK_DOWN);
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_ERROR, 0);
                }
                // Check for new connections
                static uint32_t connection_check_counter = 0;
                connection_check_counter++;
                if (connection_check_counter % 30 == 0) {  // Every 3 seconds
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_CONNECTION_CHECK, 1);
                    state_machine_process_core1_event(CORE1_EVENT_CONNECTION_ACTIVE);
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_CONNECT, 0);
                }
                break;
                
            case CORE1_NET_ACTIVE:
                if (!check_network_status()) {
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_DOWN, 0);
                    state_machine_process_core1_event(CORE1_EVENT_NETWORK_DOWN);
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_ERROR, 0);
                }
                // Simulate connection idle after some time
                static uint32_t active_counter = 0;
                active_counter++;
                if (active_counter % 100 == 0) {  // Every 10 seconds
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_CONNECTION_CHECK, 0);
                    state_machine_process_core1_event(CORE1_EVENT_CONNECTION_IDLE);
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_DISCONNECT, 0);
                    active_counter = 0;
                }
                break;
                
            case CORE1_PERSISTENCE_ACTIVE:
                // Persistence operation active - limited other processing
                log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_START, 1);
                break;
                
            case CORE1_LOG_ACTIVE:
                // Log processing active - this should be very brief
                break;
        }
        
        // Core1 main loop frequency: 100ms as specified in requirements
        sleep_ms(100);
    }
}

// Core1 processing function implementations (stubs for now)

/**
 * Initialize network interface
 * @return true if successful, false otherwise
 */
static bool initialize_network_interface(void) {
    // Stub implementation - always succeeds for testing
    // Real implementation would:
    // - Initialize ENC28J60 SPI interface
    // - Configure lwIP TCP/IP stack
    // - Set up socket management
    // - Configure network parameters
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_INIT, 1);
    return true;
}

/**
 * Check current network status
 * @return true if network is available, false otherwise
 */
static bool check_network_status(void) {
    // Stub implementation - simulate network availability
    static uint32_t network_counter = 0;
    network_counter++;
    
    // Simulate network going up/down periodically
    return (network_counter / 200) % 2 == 0;  // Up for 20s, down for 20s
}

/**
 * Process network operations (high priority)
 */
static void process_network_operations(void) {
    // Stub implementation - simulate network processing
    // Real implementation would:
    // - Process TCP/IP packets
    // - Handle socket connections
    // - Process ring buffer data
    // - Manage HTTP server for management interface
    
    static uint32_t net_counter = 0;
    net_counter++;
    
    if (net_counter % 50 == 0) {  // Every 5 seconds
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_OPERATIONS, 0);
    }
}

/**
 * Check if persistence operation is needed
 * @return true if persistence needed, false otherwise
 */
static bool persistence_needed(void) {
    // Stub implementation - simulate periodic persistence needs
    static uint32_t persistence_counter = 0;
    persistence_counter++;
    
    // Need persistence every 300 iterations (~30 seconds at 100ms loop)
    if (persistence_counter % 300 == 0) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_NEEDED, 0);
        return true;
    }
    return false;
}
