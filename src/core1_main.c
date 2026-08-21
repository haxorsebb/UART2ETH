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
#include <pico/flash.h>
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
#include "device_mode.h"
#include "flash_persistence.h"
#include "update/update_manager.h"
#include "update/deferred_reboot.h"

// PHY link status poll period (ADR-007, "Core 1 idle wait")
#define CORE1_LINK_POLL_INTERVAL_MS 500



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

// Update/Reboot functions (ADR-017)
static void core1_buy_update(void);
static void core1_reboot_flush(void);
static void core1_reboot_execute(void);

// MAIN_STATE_CONFIGURATION functions
static void core1_load_configuration(void);
static void core1_check_dhcp_status(void);
static void core1_validate_configuration(void);
static void core1_configuration_complete(void);

// MAIN_STATE_OPERATIONAL functions
static bool core1_check_for_pending_work(void);
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
        DEBUG_ONLY({
        if (g_core1_loop_counter % 200000 == 0) {
            printf("DEBUG: Core1 loop=%u, states=%u, main=%d, sub=%d\n", g_core1_loop_counter, g_core1_state_reads, main_state, sub_state);
            enc28j60_dump_signal_quality_registers();
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
                        core1_idle_wait();  // sleep until IRQ or doorbell (main state change)
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
                        core1_idle_wait();  // sleep until IRQ or doorbell (main state change)
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
                        
                    case CORE1_BUY_UPDATE:
                        // Buy the current firmware image (ADR-017)
                        core1_buy_update();
                        break;

                    default:
                        break;
                }
                break;
                
            case MAIN_STATE_ERROR:
                // Error handling
                core1_handle_error();
                break;
                
            case MAIN_STATE_REBOOT:
                // Reboot handling (ADR-017)
                switch (sub_state) {
                    case CORE1_REBOOT_FLUSH:
                        core1_reboot_flush();
                        break;
                    case CORE1_REBOOT_EXECUTE:
                        core1_reboot_execute();
                        break;
                    default:
                        // Wait in other states
                        core1_idle_wait();
                        break;
                }
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

    // ===== Link status polling =====
    // The ENC28J60 LINKIF interrupt path is unreliable (PHIE/PHIR 
    // interaction issues). Instead, poll the PHY link status register
    // directly every 500ms. This is cheap (one SPI register read) and
    // guarantees cable insertion/removal is always detected.
    // The poll is driven by CORE1_TIMER_LINK_POLL so that its alarm IRQ
    // terminates __wfi() in core1_idle_wait() (ADR-007, "Core 1 idle wait").
    {
        static bool last_polled_link = false;
        if (!core1_timer_is_active(CORE1_TIMER_LINK_POLL) ||
            core1_timer_is_expired(CORE1_TIMER_LINK_POLL)) {
            core1_timer_set(CORE1_TIMER_LINK_POLL, CORE1_LINK_POLL_INTERVAL_MS);
            bool phy_link = enc28j60_get_link_status();
            if (phy_link != last_polled_link) {
                /*
                printf("Core1: PHY link changed: %s -> %s\n",
                       last_polled_link ? "UP" : "DOWN",
                       phy_link ? "UP" : "DOWN");
                */
                last_polled_link = phy_link;
                // Update lwIP netif link state to match PHY reality
                network_manager_is_link_up();
            }
        }
    }
    // ===== End link status polling =====

    // Configuration changes are persisted only and take effect at the next
    // boot; there is no runtime apply (ADR-019). A reboot requested over the
    // network is executed here once its grace period has expired, after the
    // HTTP acknowledgement had time to leave the device.
    deferred_reboot_poll();

    if(network_manager_link_change_pending()) {
        /*
        printf("Core1: LINKIF interrupt detected — processing link change\n");
        */
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_LINK_CHANGE_ACTIVE);
        return true; 
    }

    if(network_manager_receive_packets_pending()) {
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_RECEIVE_ACTIVE);
        return true; 
    }
    
    // Network TX must be higher priority than ringbuffer to actually send queued packets
    // and receive TCP ACKs, otherwise TCP buffer fills up
    if(network_manager_transmit_packets_pending()) {
        state_machine_process_core1_event(CORE1_EVENT_NETWORK_SENDING_ACTIVE);
        core1_process_packet_tx();
        return true; 
    }

    // Check for ringbuffer messages to transmit over network
    if(ringbuffer_get_count(RX_UART_TO_TCP) > 0) {
        state_machine_process_core1_event(CORE1_EVENT_RINGBUFFER_DATA_READY);
        return true;
    }

    if(core1_timer_is_expired(CORE1_TIMER_NETWORK_TIMEOUT))
    {
        network_manager_check_timeouts();
        
        // During DHCP wait, trigger a DHCP status check after processing timeouts
        // This allows ACD timers to advance and then check if DHCP is complete
        core1_substate_t sub_state = state_machine_get_core1_substate();
        if (sub_state == CORE1_CONFIG_NET_WAIT_FOR_DHCP || sub_state == CORE1_CONFIG_NET_CHECK_DHCP) {
            state_machine_process_core1_event(CORE1_EVENT_NETWORK_RECEIVE_ACTIVE);
        }
        return true;
    }

    //low priority tasks
    if(log_manager_get_pending_count()) {
        DEBUG_ONLY({ 
            printf("Logmanager has pending count %d\n",log_manager_get_pending_count()); 
        });
        state_machine_process_core1_event(CORE1_EVENT_LOG_START);
        return true;
    }
    if(flash_persistence_save_needed()) {
        DEBUG_ONLY({ 
            printf("persistence needed\n"); 
        });
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
 * @brief Idle state - sleep until an interrupt
 *
 * Wake sources, all of which raise an IRQ on core 1 (ADR-007, "Core 1 idle wait"):
 * - the wake doorbell rung by core 0 (ringbuffer data, main state change),
 * - the core 1 timer alarm (network timeouts, CORE1_TIMER_LINK_POLL),
 * - the ENC28J60 INT falling edge.
 *
 * The ENC28J60 INT pin is edge-triggered but the chip holds INT low until its
 * flags are serviced, so no new edge arrives while INT is already low. Do not
 * sleep in that case (risk R-010). An edge between the guard and __wfi() sets
 * the NVIC pending bit and __wfi() returns at once, so that race is benign.
 *
 * The inter-core FIFO is never read here; it belongs to the SDK lockout
 * mechanism behind flash_safe_execute().
 */
static void core1_idle_wait(void) {
    if (enc28j60_wake_pending()) {
        return;
    }
    __wfi();
}




// Core1 initialization and state management functions

/**
 * @brief One-time Core1 initialization
 */
static void core1_initialize(void) {
    // Minimal printf to avoid dual-core conflicts
    bool flash_safe = flash_safe_execute_core_init();
    /* printf("DEBUG: Core1 initialize() starting. flash_safe: %d\n",flash_safe); */
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_STARTING, 0);
    
    // Reset state tracking
    g_persistence_initialized = false;
    g_logging_initialized = false;
    g_network_initialized = false;
    g_configuration_loaded = false;
    g_config_loading_cycles = 0;
    
    // Let core 0 terminate our __wfi() via doorbell (ADR-007, "Cross-Core Wake-Up")
    state_machine_enable_wake_irq();
    core1_timer_init();
    deferred_reboot_init();  // No reboot pending after start (ADR-019)
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 1);
    
    /* printf("DEBUG: Core1 initialize() completed\n"); */
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
 * @brief wait for the link to become available (with timeout)
 * 
 * Waits for Ethernet link to come up, but proceeds after a timeout to allow
 * UART processing to work even without network connectivity.
 * Network will be configured later when link becomes available.
 */
static void core1_wait_for_link_up(void) {
    static int link_wait_attempts = 0;
    const int MAX_LINK_WAIT_ATTEMPTS = 15;  // 15 * 200ms = 3 seconds max wait
    
    bool result = network_manager_is_link_up();
    
    if (result) {
        /* printf("CORE1: Ethernet link UP - proceeding with network init\n"); */
        link_wait_attempts = 0;  // Reset for future use
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_UP, 1);
        state_machine_process_core1_event(CORE1_EVENT_INIT_NET_LINK_UP);
    } else {
        link_wait_attempts++;
        
        if (link_wait_attempts >= MAX_LINK_WAIT_ATTEMPTS) {
            // Timeout - proceed without link to allow UART to work
            /* printf("CORE1: Ethernet link DOWN after %d attempts - proceeding anyway (UART will work)\n", 
                   link_wait_attempts); */
            link_wait_attempts = 0;  // Reset for future use
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_DOWN, 1);
            // Proceed to next phase even without link - network will be set up later
            state_machine_process_core1_event(CORE1_EVENT_INIT_NET_LINK_UP);
        } else {
            /* printf("CORE1: Waiting for Ethernet link (%d/%d)...\n", link_wait_attempts, MAX_LINK_WAIT_ATTEMPTS); */
            sleep_ms(200);
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DOWN, 1);
            state_machine_process_core1_event(CORE1_EVENT_INIT_NET_LINK_DOWN);
        }
    }
}


/**
 * @brief Complete initialization phase
 */
static void core1_init_complete(void) {
    // Initialize the update module (ADR-017)
    update_init();

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
    static int dhcp_retry_count = 0;
    const int MAX_DHCP_RETRIES = 3;  // After 3 DHCP timeouts, proceed without network
    
    DEBUG_ONLY({
        printf("CHECK IF DHCP IS COMPLETE!\n");
    });
    
    // Poll link status - this updates netif link state if cable was connected after boot
    // This is critical when device boots without Ethernet and cable is connected later
    network_manager_is_link_up();
    
    network_manager_check_timeouts();

    // Check if DHCP has successfully bound an IP address
    if (network_manager_check_dhcp_status()) {
        DEBUG_ONLY({
            printf("DHCP successfully bound IP address\n");
        });
        dhcp_retry_count = 0;  // Reset for future use
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 1);
        state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_GOT_DHCP);
        return;
    }

    // Check if link is actually up before waiting for DHCP
    bool link_up = network_manager_is_link_up();
    if (!link_up) {
        // No point waiting for DHCP if link is down
        static uint32_t last_no_link_msg = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_no_link_msg > 5000) {  // Print every 5 seconds
            /* printf("CORE1: DHCP waiting but link is DOWN - connect Ethernet cable\n"); */
            last_no_link_msg = now;
        }
        // Don't count this against DHCP retries - just wait for link
        return;
    }
    
    if(core1_timer_is_expired(CORE1_TIMER_DHCP_DISCOVER)) {
        dhcp_retry_count++;
        /* printf("CORE1: DHCP timer expired, retry %d/%d\n", dhcp_retry_count, MAX_DHCP_RETRIES); */
        
        if (dhcp_retry_count >= MAX_DHCP_RETRIES) {
            // Too many retries - proceed without network to allow UART to work
            /* printf("CORE1: DHCP failed after %d retries - proceeding without network (UART will work)\n",dhcp_retry_count); */
            dhcp_retry_count = 0;  // Reset for future use
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_ERROR, 1);
            // Proceed to OPERATIONAL state even without DHCP
            state_machine_process_core1_event(CORE1_EVENT_CONFIG_NET_COMPLETE);
            return;
        }
        
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
        
        /* printf("Core1: Initializing TRUE MULTI-INSTANCE TCP servers for enabled channels\n"); */
        
        int successful_channels = 0;
        int failed_channels = 0;
        
        // Initialize TCP servers for Channels (all PIO UART channels)
        
        for(int channel_idx=CHANNEL_0; channel_idx < CHANNEL_MAX; channel_idx++)
        {
            // Skip channels not available in current device mode
            if (!DEVICE_CHANNEL_AVAILABLE(channel_idx)) {
                /* printf("Core1: Channel %d not available in %s mode, skipping\n", channel_idx, DEVICE_MODE_NAME); */
                continue;
            }
            
            channel_config_t channel_config = shared_memory_get_layout()->config.channels[channel_idx];
            if(channel_config.enabled)
            {
                uint16_t port = channel_config.tcp_port;
                /* printf("Core1: Initializing TCP server for Channel %d (UART1) on port %u\n", channel_idx, port); */

                if (multi_tcp_server_init_channel(channel_idx, port)) {
                    /* printf("Core1: ✅ Channel %d (UART1) initialized successfully on port %u\n", channel_idx, port); */
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, port);
                    successful_channels++;
                } else {
                    /* printf("Core1: ❌ Channel %d (UART1) initialization FAILED on port %u\n", channel_idx, port); */
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, port);
                    failed_channels++;
                }
            }
        }            
        // All channels are now active simultaneously - no switching needed!
        DEBUG_ONLY({
            printf("Core1: Multi-instance initialization complete: %d successful, %d failed\n", successful_channels, failed_channels);
        });

        // Initialize HTTP server for device information web interface
        /* printf("Core1: Initializing HTTP server for web interface on port 80\n"); */
        if (http_server_init()) {
            /* printf("Core1: ✅ HTTP server initialized successfully on port 80\n");
            printf("Core1: 📄 Device information available at http://%s\n", ip_str); */
            log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 80);
        } else {
            /* printf("Core1: ❌ HTTP server initialization FAILED on port 80\n"); */
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
 * @brief Buy the current firmware image (ADR-017)
 * 
 * Called when entering OPERATIONAL state to confirm the firmware is good.
 * If this is a flash update boot, performs explicit_buy to mark image as permanent.
 */
static void core1_buy_update(void) {
    /* printf("Core1: Attempting to buy current firmware image\n"); */
    
    bool buy_result = update_buy_current_image();
    
    if (buy_result) {
        /* printf("Core1: ✅ Firmware buy succeeded (or not needed)\n"); */
        state_machine_process_core1_event(CORE1_EVENT_BUY_SUCCESS);
    } else {
        /* printf("Core1: ❌ Firmware buy FAILED - triggering reboot to old image\n"); */
        update_set_reboot_reason(REBOOT_REASON_UPDATE_BUY_FAILED);
        state_machine_process_main_event(MAIN_EVENT_REBOOT_REQUESTED);
    }
}

/**
 * @brief Flush logs and configuration before reboot (ADR-017)
 */
static void core1_reboot_flush(void) {
    /* printf("Core1: Preparing for reboot - flushing data\n"); */
    
    // Log the reboot reason
    reboot_reason_t reason = update_get_reboot_reason();
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_WARN, LOG_EVENT_SYSTEM_REBOOT, (uint32_t)reason);
    
    // Force save configuration to flash
    /* printf("Core1: Flushing configuration to flash\n"); */
    flash_persistence_force_save_configuration();
    
    // Format and output any pending logs
    /* printf("Core1: Flushing pending log entries\n"); */
    while (log_manager_get_pending_count() > 0) {
        log_manager_format_pending();
    }
    
    /* printf("Core1: Flush complete - ready for reboot\n"); */
    state_machine_process_core1_event(CORE1_EVENT_REBOOT_FLUSH_COMPLETE);
}

/**
 * @brief Execute the system reboot (ADR-017)
 * 
 * This function does not return - the system will reboot.
 */
static void core1_reboot_execute(void) {
    printf("Core1: Executing reboot...\n");
    
    // Small delay to ensure printf is output
    sleep_ms(100);
    
    // Execute reboot via update module
    update_execute_reboot();
    
    // Should never reach here
    while (1) {
        tight_loop_contents();
    }
}


/**
 * @brief Process connectivity change as a result of LINKIF interrupt flag
 * 
 */
static void core1_process_network_link_change(void) {

    network_manager_link_change();
    bool link_up = network_manager_is_link_up();
    printf("Core1: LINK CHANGE detected — link is now %s\n", link_up ? "UP" : "DOWN");
    if(link_up) {
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
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_DEBUG, LOG_EVENT_PERSISTENCE_START, 1);
    
    flash_persistence_save_configuration_if_needed();
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
    // Process ringbuffer messages - fetch from ringbuffer and send over network
    // Process up to 8 messages, then flush to ENC28J60, repeat
    // NOTE: No log_event() in hot path - uses spin_lock which disables interrupts
    
    const int MAX_MESSAGES_PER_BATCH = 8;
    const int MAX_BATCHES = 4;
    int total_processed = 0;
    
    for (int batch = 0; batch < MAX_BATCHES; batch++) {
        int batch_processed = 0;
        bool tcp_full = false;
        
        while (batch_processed < MAX_MESSAGES_PER_BATCH) {
            ring_entry_t* entry = ringbuffer_dequeue_entry(RX_UART_TO_TCP, CHANNEL_ANY, ENTRY_STATUS_READY);
            
            if (entry == NULL) {
                break;  // No more messages
            }
            
            // Send the message over the network via TCP socket server
            bool sent = tcp_socket_server_send_to_channel(entry->channel, entry->payload, entry->fill_index);
            
            if (sent) {
                // Mark the entry as consumed to return it to the free pool
                ringbuffer_mark_consumed(entry);
                batch_processed++;
                total_processed++;
            } else {
                // TCP buffer full - put entry back to READY state
                entry->status = ENTRY_STATUS_READY;
                tcp_full = true;
                break;
            }
        }
        
        // Flush queued packets to ENC28J60 after each batch
        // This is critical - tcp_write only queues data, network_manager_process_tx actually sends it
        if (batch_processed > 0 || tcp_full) {
            network_manager_process_tx();
        }
        
        // If no messages were processed this batch, we're done
        if (batch_processed == 0) {
            break;
        }
        
        // Also process incoming packets (TCP ACKs) to free up send buffer
        if (network_manager_receive_packets_pending()) {
            network_manager_process();  // Handles RX via lwip_netif_enc28j60_process()
        }
    }
        
    // Complete ringbuffer processing and return to idle for next work check
    state_machine_process_core1_event(CORE1_EVENT_RINGBUFFER_WORK_COMPLETE);
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

