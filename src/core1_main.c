/**
 * @file core1_main.c
 * @brief Core1 main function with clean state machine architecture
 * 
 * Implements a clean state machine pattern for Core1:
 * - One main while(true) loop
 * - State update function + big switch statement
 * - Each sub-state calls exactly one function
 * - Network processing happens continuously in OPERATIONAL state
 * - WFI in idle state for power efficiency
 * 
 * Architecture:
 * 1. core1_update_states() - determines next state transitions
 * 2. switch(main_state) - handles each main state
 * 3. switch(sub_state) - handles each sub-state within main state
 * 4. Single function call per sub-state - clean separation of concerns
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 */
#include "debug.h"
#include "core1_timer.h"
#include "state_machine.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include <stdio.h>

// Forward declarations for Core1 state functions
static void core1_initialize(void);
static void core1_update_states(void);

// MAIN_STATE_INIT functions
static void core1_init_persistence(void);
static void core1_init_logging(void);
static void core1_init_hardware(void);
static void core1_init_network(void);
static void core1_wait_for_link_up(void);
static void core1_init_complete(void);

// MAIN_STATE_CONFIGURATION functions
static void core1_load_configuration(void);
static void core1_check_dhcp_status(void);
static void core1_validate_configuration(void);
static void core1_configuration_complete(void);

// MAIN_STATE_OPERATIONAL functions
static bool check_for_pending_work(void);
static void core1_work_or_idle_wait(void);
static void core1_idle_wait(void);
static void core1_process_network_link_change(void);
static void core1_process_packet_tx(void);
static void core1_process_network(void);
static void core1_process_persistence(void);
static void core1_process_logs(void);

// MAIN_STATE_ERROR functions
static void core1_handle_error(void);

// State tracking for transitions

static bool g_network_initialized = false;
static bool g_persistence_initialized = false;
static bool g_logging_initialized = false;
static bool g_configuration_loaded = false;
static uint32_t g_config_loading_cycles = 0;

/**
 * @brief Clean Core1 main function with simple state machine
 * 
 * Single while loop with state update + switch statement architecture.
 * Each sub-state calls exactly one function for clean separation.
 */
void core1_main(void) {
    // One-time initialization
    core1_initialize();
    
    // Get current states once 
    main_state_t main_state = state_machine_get_main_state();
    core1_substate_t sub_state = state_machine_get_core1_substate();
    

    while (sub_state != CORE1_SHUTDOWN) {
        
        // Get current states
        main_state = state_machine_get_main_state();
        sub_state = state_machine_get_core1_substate();
        
        // DEBUG: Print states periodically
        DEBUG_ONLY({
        static uint32_t debug_counter = 0;
        debug_counter++;
        if (debug_counter % 500 == 0) {  // Every 50k loops
            printf("DEBUG: Core1 main_state=%d, sub_state=%d\n", main_state, sub_state);
        }
        });
        
        // Big switch statement for main states
        switch (main_state) {
            case MAIN_STATE_INIT:
                // Initialization phase - set up hardware and network
                switch (sub_state) {
                    case CORE1_INIT_PERISTENCE:
                        core1_init_persistence();
                        break;
                    case CORE1_INIT_LOGGING:
                        core1_init_logging();
                        break;
                    case CORE1_INIT_NET:
                        core1_init_hardware();
                        break;
                    case CORE1_INIT_WAIT_FOR_LINK:
                        core1_wait_for_link_up();
                        break;
                    case CORE1_INIT_COMPLETE:
                        core1_init_complete();
                        break;
                    case CORE1_INIT_IDLE:
                        core1_idle_wait();  // WFI - wait for interrupts, until main state changes
                        break;
                    case CORE1_INIT_ERROR:
                        state_machine_process_main_event(MAIN_EVENT_SYSTEM_ERROR);
                        break;
                    default:
                        break;
                }
                break;
                
            case MAIN_STATE_CONFIGURATION:
                // Configuration loading phase
                switch (sub_state) {
                    case CORE1_CONFIG_NET:
                        core1_load_configuration();
                        break;
                    case CORE1_CONFIG_NET_WAIT_FOR_DHCP:
                        core1_work_or_idle_wait();
                        break;
                    case CORE1_CONFIG_NET_CHECK_DHCP:
                        core1_check_dhcp_status();
                        break;
                    case CORE1_CONFIG_COMPLETE:
                        core1_configuration_complete();
                        break;
                    case CORE1_CONFIG_IDLE:
                        core1_idle_wait();  // WFI - wait for interrupts, until main state changes
                        break;   
                    case CORE1_CONFIG_ERROR:
                        core1_idle_wait();  // wait for config changes
                        break;
                    case CORE1_CONFIG_LOG_ACTIVE:
                        core1_process_logs();                        
                        break; 
                    default:
                        break;
                }
                break;
                
            case MAIN_STATE_OPERATIONAL:
                // Normal operation - process based on sub-state
                switch (sub_state) {
                    case CORE1_NET_CONNECTED:
                        core1_process_network();  // look for work                       
                        break;
                    case CORE1_NET_DISCONNECTED:
                        core1_process_network();  // Try to bring network up                        
                        break;
                    case CORE1_NET_IDLE:
                        core1_process_network();  // Try to bring network up                        
                        break;
                    case CORE1_NET_LINK_CHANGE:
                        core1_process_network_link_change();  // process link change
                        break;
                    case CORE1_NET_ACTIVE_RECEIVE:
                        core1_process_network();  // Active network processing
                        break;
                    case CORE1_NET_ACTIVE_SEND:
                        core1_process_network();  // Active network processing
                        break;
                    case CORE1_PERSISTENCE_ACTIVE:
                        core1_process_persistence();                        
                        break;
                    case CORE1_LOG_ACTIVE:
                        core1_process_logs();                        
                        break;

                    case CORE1_IDLE: //check for work or sleep
                        core1_work_or_idle_wait();
                        break;

                    default:
                        break;
                }
                break;
                
            case MAIN_STATE_ERROR:
                // Error handling
                core1_handle_error();
                break;
        }
    }
}

// MAIN_STATE_OPERATIONAL function implementations

/**
 * @brief check if we need to do anything and fire a corresponding event
 */
static bool check_for_pending_work(void) {

    enc28j60_process_interrupts(false);

    if(network_manager_link_change_pending()) {
        DEBUG_ONLY({ printf("network has link change pending\n"); });
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_LINK_CHANGE_ACTIVE);
        return true; 
    }
    if(network_manager_receive_packets_pending()) {
        DEBUG_ONLY({ printf("network has pending receive packets\n"); });
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_RECEIVE_ACTIVE);
        network_manager_check_timeouts();
    
        return true; 
    }
    
    if(network_manager_transmit_packets_pending()) {
        DEBUG_ONLY({ printf("network has pending transmit packets\n"); });
        //state_machine_process_core1_event(CORE1_EVENT_NETWORK_SENDING_ACTIVE);
        core1_process_packet_tx();
        return true; 
    }
    
    if(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT))
    {
        network_manager_check_timeouts();
        return true;
    }

    if(log_manager_get_pending_count()) {
        DEBUG_ONLY({ printf("Logmanager has pending count %d\n",log_manager_get_pending_count()); });
        state_machine_process_core1_event(CORE1_EVENT_LOG_START);
        return true;
    }
    if(false && flash_persistence_save_needed()) {
        DEBUG_ONLY({ printf("persistence needed\n"); });
        state_machine_process_core1_event(CORE1_EVENT_PERSISTENCE_START);
        return false;
    }

    return false;
}

/**
 * @brief Idle state - wait for interrupts
 * 
 * Uses WFI (Wait For Interrupt) for power efficiency while waiting
 * for network packets or other events.
 */
static void core1_work_or_idle_wait(void) {
    
    //check if we need to do something
    if(check_for_pending_work())    
    {
        return;
    }
    
    core1_idle_wait();
    
    return;
}

/**
 * @brief Idle state - wait for interrupts
 * 
 * Uses WFI (Wait For Interrupt) for power efficiency while waiting
 * for network packets or other events.
 */
static void core1_idle_wait(void) {

    // Wait for interrupt - power efficient
    __wfi();
    DEBUG_ONLY({printf("wfi elapsed since interrupt: %d\n",to_ms_since_boot(get_absolute_time()) - get_interrupt_ms());});

}




// Core1 initialization and state management functions

/**
 * @brief One-time Core1 initialization
 */
static void core1_initialize(void) {
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_STARTING, 0);
    
    // Reset state tracking
    g_persistence_initialized = false;
    g_logging_initialized = false;
    g_network_initialized = false;
    g_configuration_loaded = false;
    g_config_loading_cycles = 0;
    
    // Make sure the doorbell_on_mainstate_change doorbell is not set for this core
    multicore_doorbell_clear_current_core(doorbell_core1_wakes_core0);

    //set up doorbell irq - all doorbells have the same irq anyway. it is shared
    uint32_t irq = multicore_doorbell_irq_num(doorbell_core1_wakes_core0);
    DEBUG_ONLY({
        printf("Core1: Setting up doorbell IRQ %u for doorbell %d\n", irq, doorbell_core1_wakes_core0);
    });
    irq_add_shared_handler(irq, shared_doorbell_irq,PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY-1);
    irq_set_enabled(irq, true);

    //init timers
    core1_timer_init();

    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 1);
}


// MAIN_STATE_INIT function implementations

/**
 * @brief Initialize peristence management system
 */
static void core1_init_persistence(void) {
    if (g_persistence_initialized) {
        return;  // Already done
    }
    
    flash_persistence_init();
    bool result = flash_persistence_load_configuration(); 
    DEBUG_ONLY({
        printf("RESULT OF flash_persistence_load_configuration on init: %d\n", result);
    });
    
    if (result) {
        g_persistence_initialized = true;
        log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_INFO, LOG_EVENT_PERSISTENCE_INIT_SUCCESS, 1);
        // Generate network up event to move to next phase
        state_machine_process_core1_event(CORE1_EVENT_INIT_PERSISTENCE_COMPLETE);
    } else {
        log_event(EVENT_SOURCE_PERSISTENCE, LOG_LEVEL_ERROR, LOG_EVENT_PERSISTENCE_INIT_FAIL, 1);
        state_machine_process_core1_event(CORE1_EVENT_INIT_PERSISTENCE_FAILED);
        // Retry after a delay
        sleep_ms(1000);
    }
}

/**
 * @brief Initialize log management system
 */
static void core1_init_logging(void) {
    if (g_logging_initialized) {
        return;  // Already done
    }
    
    bool result = true; //TBD: 

    if (result) {
        g_logging_initialized = true;
        log_event(EVENT_SOURCE_LOGGING, LOG_LEVEL_INFO, LOG_EVENT_LOGGING_INIT_SUCCESS, 1);
        // Generate network up event to move to next phase
        state_machine_process_core1_event(CORE1_EVENT_INIT_LOGGING_COMPLETE);
    } else {
        log_event(EVENT_SOURCE_LOGGING, LOG_LEVEL_ERROR, LOG_EVENT_LOGGING_INIT_FAIL, 1);
        state_machine_process_core1_event(CORE1_EVENT_INIT_LOGGING_FAILED);
        // Retry after a delay
        sleep_ms(1000);
    }
}

/**
 * @brief Initialize hardware and network stack
 */
static void core1_init_hardware(void) {
    bool result = false;
    if (g_network_initialized) {
        result = true;  // Already done
    }
    else {
        // Get default network configuration
        network_config_t config;
        network_manager_get_default_config(&config);
        
        // Initialize network manager with ENC28J60 driver to check hardware status
        result = network_manager_init(&config);
        DEBUG_ONLY({
            printf("RESULT OF network_manager_init on init: %d\n", result);
        });
    
    }

    if (result) {
        g_network_initialized = true;
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 1);
        // Generate network up event to move to next phase
        state_machine_process_core1_event(CORE1_EVENT_INIT_NET_WAIT_FOR_LINK_UP);
    } else {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 1);
        state_machine_process_core1_event(CORE1_EVENT_INIT_NET_FAILED);                                        
    }
}

/**
 * @brief wait for the link to become available
 */
static void core1_wait_for_link_up(void) {

    DEBUG_ONLY({
        printf("WAINTING FOR LINK UP!");
    });
    bool result = network_manager_is_link_up();
    
    if (result) {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_UP, 1);
        // Generate network up event to move to next phase
        state_machine_process_core1_event(CORE1_EVENT_INIT_NET_LINK_UP);
    } else {
        sleep_ms(200); //wait for link up
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DOWN, 1);
        state_machine_process_core1_event(CORE1_EVENT_INIT_NET_LINK_DOWN);                                        
    }
}


/**
 * @brief Complete initialization phase
 */
static void core1_init_complete(void) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 2);
    // Transition to configuration phase
    state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE1);
    //sleep if other core not yet ready (any event will do)
    state_machine_process_core1_event(CORE1_EVENT_INIT_NET_COMPLETE);
}

// MAIN_STATE_CONFIGURATION function implementations

/**
 * @brief Load and validate configuration
 */
static void core1_load_configuration(void) {

    // Copy loaded configuration to shared memory
    shared_memory_layout_t* layout = shared_memory_get_layout();

    bool result = network_manager_reconfigure(&layout->config.network); //config gets copied inside
    DEBUG_ONLY({
        printf("RESULT OF network_manager_reconfigure on load: %d\n", result);
    });
    
    if (result) {
        // Generate to move to next phase
        if(layout->config.network.use_dhcp) {
            state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_DHCP_REQUEST);
        }
        else {
            state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_COMPLETE);
        }
    } else {
        //signalize failure
        state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_FAILED);
    }
    
}

/**
 * @brief Wait for DHCP to complete and get IP address
 */
static void core1_check_dhcp_status(void) {
    
    DEBUG_ONLY({
        printf("CHECK IF DHCP IS COMPLETE!\n");
    });
    network_manager_check_timeouts();

    // Check if DHCP has successfully bound an IP address
    if (network_manager_check_dhcp_status()) {
        DEBUG_ONLY({
            printf("DHCP successfully bound IP address\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 1);
        state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_GOT_DHCP);
        return;
    }

    if(core1_timer_is_expired(CORE1_TIMER_DHCP_DISCOVER)) {
        DEBUG_ONLY({
            printf("DHCP TIMEOUT, retry\n");
        });
        state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_DHCP_TIMEOUT);
        return;
    }
    //not expired, not ready, keep waiting
    DEBUG_ONLY({
        printf("DHCP WAIT\n");
    });
    state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_WAIT_DHCP);
    return;
}

/**
 * @brief Complete configuration phase
 */
static void core1_configuration_complete(void) {
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 3);
    // Transition to operational phase
    // Transition to configuration phase
    state_machine_process_main_event(MAIN_EVENT_CONFIG_COMPLETE_CORE1);
    //sleep if other core not yet ready (any event will do)
    state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_COMPLETE);

}


/**
 * @brief Process connectivity change as a result of LINKIF interrupt flag
 * 
 */
static void core1_process_network_link_change(void) {

    network_manager_link_change();
    if(network_manager_is_link_up()) {
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_LINK_UP);
    }
    else {
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_LINK_DOWN);
    }
}

/**
 * @brief Process TXIF interrupt flag
 * 
 */
static void core1_process_packet_tx(void) {

    network_manager_process_tx();
    //TBD: this is wrong here: state_machine_process_core1_event(CORE1_EVENT_NETWORK_SENDING_FINISHED);
}

/**
 * @brief Process connectivity change as a result of LINKIF interrupt flag
 * 
 */
static void core1_process_network_connectivity_up(void) {

    //exit from CORE1_NET_CONNECTED, any event will do currently
    state_machine_process_core1_event(CORE1_EVENT_AUTO_TRANSITION);
    
}

/**
 * @brief Process connectivity change as a result of LINKIF interrupt flag
 * 
 */
static void core1_process_network_connectivity_down(void) {

    //exit from CORE1_NET_DISCONNECTED, any event will do currently
    state_machine_process_core1_event(CORE1_EVENT_AUTO_TRANSITION);
    
}

/**
 * @brief Process network operations
 * 
 * Handles all network processing including packet RX/TX, DHCP, TCP/IP stack.
 * This is the core function that makes network traffic work.
 */
static void core1_process_network(void) {
    static uint32_t call_counter = 0;
    call_counter++;
    
    DEBUG_ONLY({
        if (call_counter % 10000 == 0) {  // Print every 10k calls
            printf("DEBUG: core1_process_network() call #%u\n", call_counter);
        }
    });
    network_manager_process();
    if(!network_manager_receive_packets_pending())
    {
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_RECEIVE_FINISHED);
    }
}

/**
 * @brief Process flash persistence operations
 */
static void core1_process_persistence(void) {
    // Placeholder for persistence operations
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_START, 1);
    
    // Complete persistence
    state_machine_process_core1_event(CORE1_EVENT_PERSISTENCE_END);
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_END, 0);
}

/**
 * @brief Process log formatting and output
 */
static void core1_process_logs(void) {
    // Process pending log events
    log_manager_format_pending();
    
    // Complete log processing after 1 log format to run higher priorities tasks
    state_machine_process_core1_event(CORE1_EVENT_LOG_END);
}



// MAIN_STATE_ERROR function implementation

/**
 * @brief Handle error state and recovery
 */
static void core1_handle_error(void) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_ERROR_RECOVERY, 0);
    DEBUG_ONLY({
        printf("Core1: Error state - attempting recovery\n");
    });
    
    // Simple recovery: reset network and try again
    if (g_network_initialized) {
        network_manager_restart_interface();
    }
    
    // Attempt recovery after delay
    sleep_ms(2000);
    
    // Signal recovery attempt
    state_machine_process_main_event(MAIN_EVENT_ERROR_RECOVERED);
}

