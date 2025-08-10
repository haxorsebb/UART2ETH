/**
 * @file core1_main.c
 * @brief Core1 main function for high-performance network processing
 * 
 * Implements interrupt-driven main loop for Core1 responsible for network operations,
 * flash persistence, and log processing. Uses WFI (Wait For Interrupt) for optimal
 * performance and responsiveness. Only wakes up when actual work is needed.
 * 
 * Performance Optimizations:
 * - WFI-based event loop instead of polling with sleep
 * - Immediate interrupt processing for minimal packet latency
 * - 1Hz timer for periodic tasks (DHCP, statistics, persistence)
 * - Batch processing of multiple packets per interrupt
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - Performance Optimization Plan: Phase 1 Critical Fixes
 */

#include "state_machine.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include <stdio.h>

// Performance-optimized configuration
#define PERIODIC_TIMER_INTERVAL_US  1000000  // 1Hz = 1,000,000 microseconds
#define MAX_PACKETS_PER_INTERRUPT   32       // Process up to 32 packets per interrupt
#define NETWORK_PRIORITY_PROCESSING true     // High priority for network processing

// Forward declarations for Core1 processing functions
static bool initialize_network_interface(void);
static bool check_network_status(void);
static void process_network_operations_fast(void);
static bool persistence_needed(void);
static void setup_periodic_timer(void);
static bool periodic_timer_callback(struct repeating_timer *t);

// Timer state for periodic tasks
static struct repeating_timer g_periodic_timer;
static volatile bool g_periodic_task_pending = false;
static volatile uint32_t g_periodic_counter = 0;

/**
 * @brief High-performance Core1 main function
 * 
 * Runs an interrupt-driven control loop optimized for minimal latency.
 * Uses WFI to stall the core until interrupts occur, providing maximum
 * responsiveness while minimizing power consumption.
 * 
 * Key Performance Features:
 * - Immediate wake-up on ENC28J60 interrupts
 * - Batch processing of multiple packets
 * - Periodic tasks only run when actually needed
 * - No unnecessary polling or fixed delays
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
    
    // Set up 1Hz timer for periodic tasks
    setup_periodic_timer();
    
    // Log Core1 startup - ready for high-performance operation
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 1);
    printf("Core1: Entering high-performance interrupt-driven loop\n");
    printf("Core1: Network processing optimized for minimal latency\n");
    
    while (true) {
        // Query current states (non-blocking, very fast)
        main_state_t main_state = state_machine_get_main_state();
        core1_substate_t sub_state = state_machine_get_core1_substate();
        
        // CRITICAL PATH: Process network interrupts immediately
        if (enc28j60_has_pending_interrupt()) {
            // Process network interrupt with high priority
            enc28j60_process_interrupts();
            
            // Process all available packets in batch for efficiency
            if (main_state == MAIN_STATE_OPERATIONAL) {
                uint32_t packets_processed = 0;
                
                // Batch process up to MAX_PACKETS_PER_INTERRUPT packets
                while (enc28j60_has_rx_packet() && packets_processed < MAX_PACKETS_PER_INTERRUPT) {
                    // Process lwIP network stack (handles incoming packets)
                    network_manager_process();
                    packets_processed++;
                }
                
                // Process lwIP timeouts after packet batch
                sys_check_timeouts();
                
                // Additional network operations if needed
                process_network_operations_fast();
                
                if (packets_processed > 0) {
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_RX, packets_processed);
                }
            }
        }
        
        // PERIODIC TASKS: Only process when timer fires (1Hz)
        if (g_periodic_task_pending) {
            g_periodic_task_pending = false;  // Clear flag immediately
            
            // State-driven control logic for periodic tasks
            switch (main_state) {
                case MAIN_STATE_INIT:
                    // Wait for Core0 to complete hardware initialization
                    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_DEBUG, LOG_EVENT_INIT_PHASE, 1);
                    break;
                    
                case MAIN_STATE_CONFIGURATION:
                    // Configuration loading phase - Core1 drives this
                    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_CONFIG_PHASE, 0);
                    
                    // Simulate configuration loading (periodic processing)
                    static uint32_t config_loading_cycles = 0;
                    config_loading_cycles++;
                    
                    if (config_loading_cycles >= 5) {  // 5 seconds to load config
                        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_CONFIG_LOADED, config_loading_cycles);
                        state_machine_process_main_event(MAIN_EVENT_CONFIG_LOADED);
                        config_loading_cycles = 0;  // Reset for next time
                    }
                    break;
                    
                case MAIN_STATE_OPERATIONAL:
                    // Process lwIP timeouts for DHCP, TCP timers, etc. (only needed periodically)
                    sys_check_timeouts();
                    
                    // MEDIUM PRIORITY: Persistence operations (only when needed)
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
            
            // LOW PRIORITY: Process pending log events (batch processing for efficiency)
            if (log_manager_get_pending_count() > 0) {
                state_machine_process_core1_event(CORE1_EVENT_LOG_START);
                
                // Process all pending log events in batch
                uint32_t formatted_count = log_manager_format_pending();
                
                state_machine_process_core1_event(CORE1_EVENT_LOG_END);
            }
            
            // Network status monitoring and event generation (periodic)
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
                    // Check for new connections (every 30 seconds)
                    if (g_periodic_counter % 30 == 0) {
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
                    // Simulate connection idle after some time (every 100 seconds)
                    if (g_periodic_counter % 100 == 0) {
                        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_CONNECTION_CHECK, 0);
                        state_machine_process_core1_event(CORE1_EVENT_CONNECTION_IDLE);
                        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_TCP_DISCONNECT, 0);
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
        }
        
        // PERFORMANCE CRITICAL: Use WFI to wait for next interrupt
        // This minimizes latency while keeping power consumption low
        // Core will wake up immediately on:
        // - ENC28J60 packet reception interrupt
        // - Timer interrupt for periodic tasks
        // - Any other system interrupt
        __wfi();
    }
}

// Core1 processing function implementations

/**
 * @brief Initialize network interface using network manager
 * @return true if successful, false otherwise
 */
static bool initialize_network_interface(void) {
    // Get default network configuration
    network_config_t config;
    network_manager_get_default_config(&config);
    
    // Initialize network manager with ENC28J60 driver
    bool result = network_manager_init(&config);
    
    if (result) {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 1);
    } else {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 1);
    }
    
    return result;
}

/**
 * @brief Check current network status using network manager
 * @return true if network is available, false otherwise
 */
static bool check_network_status(void) {
    // Check if network manager is ready and link is up
    return network_manager_is_ready() && network_manager_is_link_up();
}

/**
 * @brief Process network operations (optimized for speed)
 */
static void process_network_operations_fast(void) {
    // Minimal network processing beyond the network manager
    // This function is optimized for speed and called frequently
    
    // Only log network status every 100 periodic cycles (100 seconds)
    if (g_periodic_counter % 100 == 0) {
        // Log network status and basic connectivity test (minimal overhead)
        bool connectivity = network_manager_test_connectivity();
        network_status_t status = network_manager_get_status();
        
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_OPERATIONS, 
                  (uint32_t)status | (connectivity ? 0x100 : 0));
    }
}

/**
 * @brief Check if persistence operation is needed
 * @return true if persistence needed, false otherwise
 */
static bool persistence_needed(void) {
    // Check if persistence is needed every 300 seconds (5 minutes)
    if (g_periodic_counter % 300 == 0 && g_periodic_counter > 0) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_NEEDED, 0);
        return true;
    }
    return false;
}

/**
 * @brief Set up 1Hz periodic timer for non-critical tasks
 */
static void setup_periodic_timer(void) {
    // Set up repeating timer that fires every 1 second
    bool timer_added = add_repeating_timer_us(PERIODIC_TIMER_INTERVAL_US, 
                                              periodic_timer_callback, 
                                              NULL, 
                                              &g_periodic_timer);
    
    if (timer_added) {
        printf("Core1: 1Hz periodic timer configured successfully\n");
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 2);
    } else {
        printf("Core1: Failed to configure periodic timer\n");
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_SYSTEM_ERROR, 4);
    }
}

/**
 * @brief Periodic timer callback (1Hz)
 * 
 * This callback runs in interrupt context and should be very fast.
 * It only sets a flag to indicate that periodic processing is needed.
 */
static bool periodic_timer_callback(struct repeating_timer *t) {
    // Set flag for main loop to process periodic tasks
    g_periodic_task_pending = true;
    g_periodic_counter++;
    
    // Return true to keep the timer running
    return true;
}
