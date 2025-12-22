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
#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <hardware/timer.h>
#include <hardware/irq.h>
#include <hardware/gpio.h>

#include "debug.h"
#include "core1_timer.h"
#include "state_machine.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "ringbuffer.h"
#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "network/tcp_socket_server.h"
#include "network/multi_tcp_server.h"
#include "network/http_server.h"



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
static bool core1_check_for_pending_work(void);
static void core1_apply_configuration_changes(void);
static void core1_work_or_idle_wait(void);
static void core1_idle_wait(void);
static void core1_process_network_link_change(void);
static void core1_process_packet_tx(void);
static void core1_process_network(void);
static void core1_process_persistence(void);
static void core1_process_logs(void);
static void core1_process_ringbuffer(void);

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
// Core1 debug counter - shared memory for debugging without printf conflicts
static volatile uint32_t g_core1_loop_counter = 0;
static volatile uint32_t g_core1_state_reads = 0;

void core1_main(void) {
    // One-time initialization - minimal printf to avoid conflicts
    core1_initialize();
    
    // Get current states once 
    main_state_t main_state = state_machine_get_main_state();
    core1_substate_t sub_state = state_machine_get_core1_substate();
        
    while (sub_state != CORE1_SHUTDOWN) {
        // Increment loop counter for debugging (no printf)
        g_core1_loop_counter++;
        
        // Get current states - track this separately
        main_state = state_machine_get_main_state();
        sub_state = state_machine_get_core1_substate();
        g_core1_state_reads++;
        
        // Minimal debug output - only every 10000 loops to avoid printf floods
        if (g_core1_loop_counter % 200000 == 0) {
            // printf("DEBUG: Core1 loop=%u, states=%u, main=%d, sub=%d\n", g_core1_loop_counter, g_core1_state_reads, main_state, sub_state);
            //enc28j60_dump_signal_quality_registers();
        }
        
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
                    case CORE1_RINGBUFFER_ACTIVE:
                        core1_process_ringbuffer();
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
static bool core1_check_for_pending_work(void) {

    enc28j60_process_interrupts(false);

    // Check for configuration changes (high priority)
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (layout && layout->config_change_pending) {
        printf("Core1: ⚙️ Configuration change detected - updating TCP servers ONLY\n");
        printf("Core1: 🌐 HTTP server (Port 80) will remain active and untouched\n");
        layout->config_change_pending = false;  // Clear the flag
        
        // Apply configuration changes (TCP servers only - HTTP protected)
        core1_apply_configuration_changes();
        return true;
    }

    if(network_manager_link_change_pending()) {
        DEBUG_ONLY({ 
            printf("network has link change pending\n"); 
        });
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_LINK_CHANGE_ACTIVE);
        return true; 
    }

    if(network_manager_receive_packets_pending()) {
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_RECEIVE_ACTIVE);
        return true; 
    }
    
    if(network_manager_transmit_packets_pending()) {
        DEBUG_ONLY({ 
            printf("network has pending transmit packets\n"); 
        });
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_SENDING_ACTIVE);
        core1_process_packet_tx();
        return true; 
    }

    if(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT))
    {
        network_manager_check_timeouts();
        return true;
    }

    // Check for ringbuffer messages to transmit over network (medium priority, messages are cached)
    if(ringbuffer_get_count(RX_UART_TO_TCP) > 0) {
        //DEBUG_ONLY({ 
            printf("Core1: ringbuffer has %u pending messages for network transmission\n", 
                             ringbuffer_get_count(RX_UART_TO_TCP)); 
        //});
        state_machine_process_core1_event(CORE1_EVENT_RINGBUFFER_DATA_READY);
        return true;
    }

    //low priority tasks
    if(false && log_manager_get_pending_count()) {
        //DEBUG_ONLY({ 
            printf("Logmanager has pending count %d\n",log_manager_get_pending_count()); 
        //});
        state_machine_process_core1_event(CORE1_EVENT_LOG_START);
        return true;
    }
    if(false && flash_persistence_save_needed()) {
        //DEBUG_ONLY({ 
            printf("persistence needed\n"); 
        //});
        state_machine_process_core1_event(CORE1_EVENT_PERSISTENCE_START);
        return true;
    }

    //core1_timer_set(CORE1_TIMER_WATCHDOG_FEED, 500);
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
    if(core1_check_for_pending_work())    
    {
        return;
    }
    
    core1_idle_wait();
    
    return;
}

/**
 * @brief Idle state - short sleep for polling
 * 
 * CRITICAL FIX: Do NOT use __wfi() - it causes deadlocks with edge-triggered interrupts.
 * The ENC28J60 uses edge-triggered GPIO interrupts. If the INT pin is already LOW
 * when we enter WFI, no new edge will occur and we hang forever.
 * 
 * Use 1ms sleep - this provides responsive networking (~1ms latency) while
 * avoiding the SPI bus contention issues that occur with faster polling.
 * The ENC28J60 needs time between register accesses for reliable operation.
 */
static void core1_idle_wait(void) {
    // 1ms sleep provides good balance between responsiveness and reliability
    // Faster polling can cause SPI timing issues with the ENC28J60
    sleep_ms(1);
}




// Core1 initialization and state management functions

/**
 * @brief One-time Core1 initialization
 */
static void core1_initialize(void) {
    // Minimal printf to avoid dual-core conflicts
    printf("DEBUG: Core1 initialize() starting\n");
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_STARTING, 0);
    
    // Reset state tracking
    g_persistence_initialized = false;
    g_logging_initialized = false;
    g_network_initialized = false;
    g_configuration_loaded = false;
    g_config_loading_cycles = 0;
    
    enable_doorbell_irq(CORE1_WAKES_CORE0);
    core1_timer_init();
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 1);
    
    printf("DEBUG: Core1 initialize() completed\n");
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
            DEBUG_ONLY({
                printf("DEBUG: Using DHCP - sending DHCP_REQUEST event\n");
            });
            state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_DHCP_REQUEST);
        }
        else {
            DEBUG_ONLY({
                printf("DEBUG: Using STATIC IP - sending CONFIG_NET_COMPLETE event\n");
            });
            state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_COMPLETE);
        }
    } else {
        DEBUG_ONLY({
            printf("DEBUG: Network reconfigure FAILED - sending CONFIG_NET_FAILED event\n");
        });
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
    
    // DEBUG: Always log network status and try TCP init regardless
    network_status_t net_status = network_manager_get_status();
    DEBUG_ONLY({
        printf("Core1: Network status = %d\n", net_status);
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_STATUS, net_status);
    
    // Try to get IP address regardless of status
    simple_ip_addr_t ip_addr;
    bool has_ip = network_manager_get_ip_address(&ip_addr);
    DEBUG_ONLY({
        printf("Core1: Has IP address = %d\n", has_ip);
    });
    
    if (has_ip) {
        char ip_str[16];
        network_manager_ip_to_string(&ip_addr, ip_str);
        DEBUG_ONLY({
            printf("Core1: Device IP address: %s\n", ip_str);
        });
        
        // Log IP components as individual events for debugging
        uint32_t addr = ip_addr.addr;
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_IP_CONFIGURED_1, (addr >> 0) & 0xFF);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_IP_CONFIGURED_2, (addr >> 8) & 0xFF);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_IP_CONFIGURED_3, (addr >> 16) & 0xFF);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_IP_CONFIGURED_4, (addr >> 24) & 0xFF);
        
        // Initialize TCP servers for enabled UART channels
        network_config_t net_config;
        network_manager_get_default_config(&net_config);
        
        printf("Core1: Initializing TRUE MULTI-INSTANCE TCP servers for enabled channels\n");
        
        int successful_channels = 0;
        int failed_channels = 0;
        
        // Initialize TCP servers for Channels (all PIO UART channels)
        
        for(int channel_idx=CHANNEL_0; channel_idx < CHANNEL_MAX; channel_idx++)
        {
            // Channel 1: Standard UART1 on port 4002
            channel_config_t channel_config = shared_memory_get_layout()->config.channels[channel_idx];
            if(channel_config.enabled)
            {
                uint16_t port = channel_config.tcp_port;
                printf("Core1: Initializing TCP server for Channel %d (UART1) on port %u\n", channel_idx, port);

                if (multi_tcp_server_init_channel(channel_idx, port)) {
                    printf("Core1: ✅ Channel %d (UART1) initialized successfully on port %u\n", channel_idx, port);
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, port);
                    successful_channels++;
                } else {
                    printf("Core1: ❌ Channel %d (UART1) initialization FAILED on port %u\n", channel_idx, port);
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, port);
                    failed_channels++;
                }
            }
        }            
        // All channels are now active simultaneously - no switching needed!
//        DEBUG_ONLY({
            printf("Core1: Multi-instance initialization complete: %d successful, %d failed\n", successful_channels, failed_channels);
//        });

        // Initialize HTTP server for device information web interface
        printf("Core1: Initializing HTTP server for web interface on port 80\n");
        if (http_server_init()) {
            printf("Core1: ✅ HTTP server initialized successfully on port 80\n");
            printf("Core1: 📄 Device information available at http://%s\n", ip_str);
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 80);
        } else {
            printf("Core1: ❌ HTTP server initialization FAILED on port 80\n");
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 80);
        }

    } else {
        DEBUG_ONLY({
            printf("Core1: No IP address available, skipping TCP and HTTP server init\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_ERROR, 999);
    }
    
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
    //printf("?");
    network_manager_process();
    
    // Process multi-port TCP socket servers (handle connections, data)
    multi_tcp_server_process();
    
    // Process HTTP server (handle web requests for device information)
    http_server_process();
    
    network_manager_check_timeouts();

    if(!network_manager_receive_packets_pending())
    {
        //printf("!");
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

/**
 * @brief Process ringbuffer messages for network transmission
 * 
 * Fetches messages from the ringbuffer (populated by Core0 UART processing)
 * and transmits them over the network via TCP socket server.
 * This implements the Core1 side of the UART→ringbuffer→network pipeline.
 */
static void core1_process_ringbuffer(void) {
    static uint32_t call_counter = 0;
    call_counter++;
    
    log_event(EVENT_SOURCE_RINGBUFFER, LOG_LEVEL_DEBUG, LOG_EVENT_RINGBUFFER_WORK_START, 1);
    
    // Process ringbuffer messages - fetch from ringbuffer and send over network
    // Implementation: fetch one message per call to avoid blocking other tasks
    
    ring_entry_t* entry = ringbuffer_dequeue_entry(RX_UART_TO_TCP, CHANNEL_ANY, ENTRY_STATUS_READY);
    
    if (entry != NULL) {
        printf("[CORE1-RING] Processing UART->TCP message: Channel %u, %u bytes\n", 
               entry->channel, entry->fill_index);
        
        // Send the message over the network via TCP socket server
        bool sent = tcp_socket_server_send_to_channel(entry->channel, entry->payload, entry->fill_index);
        
        if (sent) {
            printf("[CORE1-RING] Message sent to TCP - SUCCESS\n");
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_TX, entry->fill_index);
        } else {
            printf("[CORE1-RING] Message send to TCP - FAILED\n");
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_ERROR, entry->channel);
        }
        
        // Mark the entry as consumed to return it to the free pool
        ringbuffer_mark_consumed(entry);
    } else if (call_counter <= 5 || call_counter % 1000 == 0) {
        printf("[CORE1-RING] No UART->TCP messages to process (call #%u)\n", call_counter);
    }
        
    // Complete ringbuffer processing and return to idle for next work check
    state_machine_process_core1_event(CORE1_EVENT_RINGBUFFER_WORK_COMPLETE);
    log_event(EVENT_SOURCE_RINGBUFFER, LOG_LEVEL_DEBUG, LOG_EVENT_RINGBUFFER_WORK_COMPLETE, 0);
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

/**
 * @brief Apply configuration changes at runtime
 * 
 * This function handles runtime configuration updates by:
 * 1. Detecting network configuration changes (IP, DHCP, MAC)
 * 2. Applying network interface reconfiguration if needed
 * 3. Stopping existing TCP servers (but NOT HTTP server to preserve web access)
 * 4. Restarting TCP servers with new configuration
 */
static void core1_apply_configuration_changes(void) {
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        printf("Core1: ERROR - Cannot access shared memory for config update\n");
        return;
    }
    
    printf("Core1: 🔧 Applying configuration changes...\n");
    
    // Step 1: Apply network interface configuration 
    printf("Core1: 🌐 Applying network interface configuration\n");
    printf("Core1: 📍 Target IP: %d.%d.%d.%d | DHCP: %s\n",
           (int)((layout->config.network.static_ip.addr >> 0) & 0xFF),
           (int)((layout->config.network.static_ip.addr >> 8) & 0xFF),
           (int)((layout->config.network.static_ip.addr >> 16) & 0xFF),
           (int)((layout->config.network.static_ip.addr >> 24) & 0xFF),
           layout->config.network.use_dhcp ? "ENABLED" : "DISABLED");
    
    printf("Core1: ⚠️ Network interface may be briefly unavailable during reconfiguration\n");
    
    bool reconfig_success = network_manager_reconfigure(&layout->config.network);
    if (reconfig_success) {
        printf("Core1: ✅ Network interface reconfigured successfully\n");
        
        // Brief delay to allow network to stabilize (PERFORMANCE: Reduced from 500ms to 100ms)
        sleep_ms(100);
        
        // Log new network state
        simple_ip_addr_t new_ip;
        if (network_manager_get_ip_address(&new_ip)) {
            printf("Core1: 📍 Active IP address: %d.%d.%d.%d\n",
                   (new_ip.addr >> 0) & 0xFF,
                   (new_ip.addr >> 8) & 0xFF,
                   (new_ip.addr >> 16) & 0xFF,
                   (new_ip.addr >> 24) & 0xFF);
        }
    } else {
        printf("Core1: ❌ Network reconfiguration FAILED - keeping old network settings\n");
    }
    
    // Step 3: Update TCP servers (HTTP server remains untouched)
    printf("Core1: 🔧 Updating TCP servers for UART channels\n");
    printf("Core1: 🚨 HTTP Server (Port 80) remains completely untouched\n");
    
    // Stop all TCP servers 
    printf("Core1: Stopping all TCP servers (Ports 4001-4004 range)\n");
    multi_tcp_server_deinit_all();
    
    // Small delay to allow TCP sockets to properly close (PERFORMANCE: Reduced from 200ms to 50ms)
    sleep_ms(50);
    
    // Step 4: Start TCP servers for enabled UART channels with new ports
    int successful_channels = 0;
    int failed_channels = 0;
    
    printf("Core1: Starting TCP servers for enabled channels\n");
    
    for (int ch = 1; ch <= 3; ch++) {
        if (layout->config.channels[ch].enabled) {
            uint16_t tcp_port = layout->config.channels[ch].tcp_port;
            
            printf("Core1: Initializing TCP server for Channel %d on port %d\n", ch, tcp_port);
            
            bool success = multi_tcp_server_init_channel(ch, tcp_port);
            if (success) {
                printf("Core1: ✅ Channel %d TCP server active on port %d\n", ch, tcp_port);
                successful_channels++;
            } else {
                printf("Core1: ❌ Failed to start Channel %d TCP server on port %d\n", ch, tcp_port);
                failed_channels++;
            }
        } else {
            printf("Core1: Channel %d disabled - no TCP server\n", ch);
        }
    }
    
    // Step 5: Summary
    printf("Core1: ✅ Configuration update complete\n");
    printf("Core1: 📊 Network: %s | TCP servers: %d active, %d failed | HTTP: PROTECTED\n",
           reconfig_success ? "RECONFIGURED" : "failed",
           successful_channels, failed_channels);
    
    if (failed_channels > 0) {
        printf("Core1: ⚠️ Some TCP servers failed to start - check port conflicts\n");
    }
    
    if (reconfig_success) {
        printf("Core1: 🌐 Network configuration applied immediately - web interface available on updated IP\n");
    }
}

