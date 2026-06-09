/**
 * @file network_manager.c
 * @brief Network Manager Implementation for UART2ETH
 * 
 * Provides high-level network management that coordinates between
 * ENC28J60 driver, lwIP TCP/IP stack with DHCP, and Core1 integration.
 * Implements complete TCP/IP networking with automatic IP address assignment.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - ADR-007: Event-Driven State Machine Architecture
 * - lwIP Documentation for TCP/IP stack integration
 */

#include "core1_timer.h"
#include "debug.h"
#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "network/lwip_netif_enc28j60.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"  // For DHCP_STATE_OFF and other DHCP states
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "factory_defaults.h"

// Network manager state
static bool g_network_initialized = false;
static bool g_lwip_initialized = false;
static network_status_t g_network_status = NETWORK_STATUS_UNINITIALIZED;
static network_stats_t g_network_stats = {0};
static network_config_t g_network_config = {0};
static struct netif* g_netif = NULL;
static uint32_t g_dhcp_start_time = 0;

// Private function declarations
static void network_manager_update_status(void);
static void network_manager_update_stats(void);
static bool network_manager_init_lwip(void);
static bool network_manager_process_dhcp(void);


/**
 * @brief Initialize network manager with configuration
 */
bool network_manager_init(const network_config_t* config) {
    if (g_network_initialized) {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_INIT, 1);
        return true;  // Already initialized
    }
    
    DEBUG_ONLY({
        printf("Network Manager: Starting initialization\n");
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 0);
    
    // Store configuration
    if (config) {
        memcpy(&g_network_config, config, sizeof(network_config_t));
    } else {
        // Use default configuration
        network_manager_get_default_config(&g_network_config);
    }
    
    // Set initial status
    g_network_status = NETWORK_STATUS_INITIALIZING;
    
    // Initialize ENC28J60 driver
    if (!enc28j60_init()) {
        DEBUG_ONLY({
            printf("Network Manager: ENC28J60 initialization failed\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 1);
        g_network_status = NETWORK_STATUS_ERROR;
        return false;
    }
    
    // Set MAC address if provided
    if (g_network_config.mac_address[0] != 0 || g_network_config.mac_address[1] != 0 ||
        g_network_config.mac_address[2] != 0 || g_network_config.mac_address[3] != 0 ||
        g_network_config.mac_address[4] != 0 || g_network_config.mac_address[5] != 0) {
        enc28j60_set_mac_address(g_network_config.mac_address);
        DEBUG_ONLY({
            printf("Network Manager: MAC address configured\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_CONFIG, 0);
    }
    
    // Initialize lwIP TCP/IP stack
    if (!network_manager_init_lwip()) {
        DEBUG_ONLY({
            printf("Network Manager: lwIP initialization failed\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 2);
        g_network_status = NETWORK_STATUS_ERROR;
        return false;
    }
    
    // Initialize statistics
    memset(&g_network_stats, 0, sizeof(network_stats_t));
    g_network_stats.status = g_network_status;
    
    g_network_initialized = true;
    
    DEBUG_ONLY({
        printf("Network Manager: Initialization complete\n");
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 2);
    return true;
}

/**
 * @brief Deinitialize network manager
 */
void network_manager_deinit(void) {
    if (!g_network_initialized) {
        return;
    }
    
    DEBUG_ONLY({
        printf("Network Manager: Starting deinitialization\n");
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 0);
    
    // Stop DHCP if running
    if (g_netif && netif_is_up(g_netif) && dhcp_supplied_address(g_netif)) {
        dhcp_stop(g_netif);
        DEBUG_ONLY({
            printf("Network Manager: DHCP stopped\n");
        });
    }
    
    // Deinitialize lwIP network interface
    lwip_netif_enc28j60_deinit();
    g_netif = NULL;
    g_lwip_initialized = false;
    
    // Deinitialize ENC28J60 driver
    enc28j60_deinit();
    
    // Clear state
    g_network_status = NETWORK_STATUS_UNINITIALIZED;
    g_network_initialized = false;
    g_dhcp_start_time = 0;
    
    DEBUG_ONLY({
        printf("Network Manager: Deinitialization complete\n");
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 1);
}

/**
 * @brief Helper function to schedule work
 */
bool network_manager_receive_packets_pending(void) {

    if (!g_netif) {
        return false;
    }
    return (enc28j60_has_rx_packet() /*TBD: this should not be needed|| (g_network_status!=NETWORK_STATUS_READY)*/);
}

/**
 * @brief Helper function to schedule work
 */
bool network_manager_link_change_pending(void) {

    if (!g_netif) {
        return false;
    }
    return (enc28j60_has_link_change_pending() /*TBD: this should not be needed|| (g_network_status!=NETWORK_STATUS_READY)*/);
}

/**
 * @brief Helper function to schedule work
 */
bool network_manager_transmit_packets_pending(void) {
    
    return enc28j60_has_txif_pending(); //TBD: have a flag for packets that need to be sent
}



/**
 * @brief Process network manager tasks (call from Core1 main loop)
 */

void network_manager_process(void) {

    if (!g_network_initialized) {
        DEBUG_ONLY({
            printf("DEBUG: network_manager_process() - not initialized!\n");
        });
        return;
    }
    
    // Process network interface (packet RX)
    if (g_netif) {
        lwip_netif_enc28j60_process();
    }
    
    return;
}


/**
 * @brief Check if network is ready for use
 */
bool network_manager_is_ready(void) {
    return g_network_initialized;
}

/**
 * @brief Get current network status
 */
network_status_t network_manager_get_status(void) {
    return g_network_status;
}

/**
 * @brief Get current network statistics
 */
void network_manager_get_stats(network_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    network_manager_update_stats();
    memcpy(stats, &g_network_stats, sizeof(network_stats_t));
}

/**
 * @brief Reset network statistics counters
 */
void network_manager_reset_stats(void) {
    // Preserve status and uptime, reset counters
    network_status_t current_status = g_network_stats.status;
    uint32_t current_uptime = g_network_stats.uptime_seconds;
    
    memset(&g_network_stats, 0, sizeof(network_stats_t));
    g_network_stats.status = current_status;
    g_network_stats.uptime_seconds = current_uptime;
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_STATUS, 0);
}

/**
 * @brief Get current MAC address
 */
void network_manager_get_mac_address(uint8_t mac_addr[6]) {
    if (!mac_addr || !g_network_initialized) {
        return;
    }
    
    enc28j60_get_mac_address(mac_addr);
}

/**
 * @brief Check if physical link is up
 */
bool network_manager_is_link_up(void) {
    if (!g_network_initialized) {
        return false;
    }

    bool link_up = enc28j60_get_link_status();
    if(link_up) {
        if(!netif_is_link_up(g_netif)){
            netif_set_link_up(g_netif);
        }
    }
    else {
        if(netif_is_link_up(g_netif)){
            netif_set_link_down(g_netif);
        }
    }
    return link_up; 
}

/**
 * @brief Restart network interface (useful for recovery)
 */
bool network_manager_restart_interface(void) {
    if (!g_network_initialized) {
        return false;
    }
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_RESET, 0);
    
    // Reset the ENC28J60
    enc28j60_reset();
    
    // Wait for reset to complete
    sleep_ms(10);
    
    // Check if it's ready again
    if (enc28j60_is_ready()) {
        g_network_status = NETWORK_STATUS_INITIALIZING;
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_RESET, 1);
        return true;
    } else {
        g_network_status = NETWORK_STATUS_ERROR;
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 2);
        return false;
    }
}

/**
 * @brief Get default network configuration
 */
void network_manager_get_default_config(network_config_t* config) {
    if (!config) {
        printf("UNABLE TO GET DEFAULT NETWORK CONFIG!");
        return;
    }
    
    printf("GETTING DEFAULT NETWORK CONFIG!");

    memset(config, 0, sizeof(network_config_t));
    
    if (factory_defaults_is_valid()) {
        // Factory defaults present: use unique MAC and factory network settings
        const factory_defaults_t* defaults = factory_defaults_get();
        memcpy(config->mac_address, defaults->mac_address, 6);
        config->use_dhcp = defaults->default_dhcp_enable;
        config->static_ip.addr = defaults->default_ip;
        config->static_netmask.addr = defaults->default_netmask;
        config->static_gateway.addr = defaults->default_gateway;
        config->dhcp_timeout_ms = config->use_dhcp ? 30000 : 0;
    } else {
        // No valid factory defaults: use fixed MAC, disable DHCP.
        // DHCP is unsafe without a unique MAC — multiple devices would
        // share the same address and cause ARP conflicts on the network.
        config->mac_address[0] = 0x34;  // Locally administered, unicast
        config->mac_address[1] = 0xD7;
        config->mac_address[2] = 0xF5;
        config->mac_address[3] = 0x30;
        config->mac_address[4] = 0x00;
        config->mac_address[5] = 0x01;  // Fixed MAC: 34:D7:F5:30:00:01
        config->use_dhcp = false;
        config->dhcp_timeout_ms = 0;
        IP4_ADDR(&config->static_ip, 192, 168, 1, 201);
        IP4_ADDR(&config->static_netmask, 255, 255, 255, 0);
        IP4_ADDR(&config->static_gateway, 192, 168, 1, 1);
    }

    DEBUG_ONLY({
        printf("Network Manager: MAC: %02X:%02X:%02X:%02X:%02X:%02X, DHCP: %s\n",
               config->mac_address[0], config->mac_address[1], config->mac_address[2],
               config->mac_address[3], config->mac_address[4], config->mac_address[5],
               config->use_dhcp ? "ON" : "OFF");
    });

    config->management_port = 80;
}

/**
 * @brief Convert network status to human-readable string
 */
const char* network_manager_status_to_string(network_status_t status) {
    switch (status) {
        case NETWORK_STATUS_UNINITIALIZED: return "Uninitialized";
        case NETWORK_STATUS_INITIALIZING:  return "Initializing";
        case NETWORK_STATUS_LINK_DOWN:     return "Link Down";
        case NETWORK_STATUS_LINK_UP:       return "Link Up";
        case NETWORK_STATUS_DHCP_REQUESTING: return "DHCP Requesting";
        case NETWORK_STATUS_READY:         return "Ready";
        case NETWORK_STATUS_ERROR:         return "Error";
        default:                           return "Unknown";
    }
}

/**
 * @brief Test basic network connectivity
 */
bool network_manager_test_connectivity(void) {
    if (!g_network_initialized) {
        return false;
    }
    
    // Basic test: check if hardware is responsive
    bool hardware_ready = enc28j60_is_ready();
    bool link_up = enc28j60_get_link_status();
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_STATUS, 
              (hardware_ready ? 1 : 0) | (link_up ? 2 : 0));
    
    return hardware_ready && link_up;
}

/** 
 * @brief manage network link connectivity
 */
void network_manager_link_change(void) {

    bool link_up = enc28j60_process_linkif_interrupt();
    if (link_up) {
        netif_set_link_up(g_netif);
    } else {
        netif_set_link_down(g_netif);
    }
}

/** 
 * @brief gets called when lwIP timer expired
 */
void network_manager_check_timeouts(void) {
    /* Cyclic lwIP timers check */
    sys_check_timeouts();
    uint32_t next_timeout = sys_timeouts_sleeptime();
    core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, next_timeout);    
}



/**
 * @brief Get detailed network interface information for diagnostics
 */
int network_manager_get_diagnostic_info(char* info_buffer, size_t buffer_size) {
    if (!info_buffer || buffer_size == 0) {
        return 0;
    }
    
    uint8_t mac_addr[6];
    network_manager_get_mac_address(mac_addr);
    
    const enc28j60_state_t* enc_state = enc28j60_get_state();
    
    int written = snprintf(info_buffer, buffer_size,
        "Network Manager Diagnostics:\n"
        "Status: %s\n"
        "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n"
        "Link Up Events: %u\n"
        "Link Down Events: %u\n"
        "Hardware Ready: %s\n"
        "Link Status: %s\n"
        "ENC28J60 Packets TX: %u\n"
        "ENC28J60 Packets RX: %u\n"
        "ENC28J60 TX Errors: %u\n"
        "ENC28J60 RX Errors: %u\n",
        network_manager_status_to_string(g_network_status),
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        g_network_stats.link_up_events,
        g_network_stats.link_down_events,
        enc28j60_is_ready() ? "Yes" : "No",
        enc28j60_get_link_status() ? "Up" : "Down",
        enc_state ? enc_state->packets_sent : 0,
        enc_state ? enc_state->packets_received : 0,
        enc_state ? enc_state->tx_errors : 0,
        enc_state ? enc_state->rx_errors : 0
    );
    
    return written;
}

// Private function implementations

/**
 * @brief Initialize lwIP TCP/IP stack and network interface
 */
static bool network_manager_init_lwip(void) {
    DEBUG_ONLY({
        printf("Network Manager: Initializing lwIP TCP/IP stack\n");
    });
    
    // Initialize lwIP
    lwip_init();
    g_lwip_initialized = true;
    
    // Initialize lwIP network interface for ENC28J60
    ip4_addr_t ip_addr, netmask, gateway;
    
    if (g_network_config.use_dhcp) {
        // Use zero addresses for DHCP
        IP4_ADDR(&ip_addr, 0, 0, 0, 0);
        IP4_ADDR(&netmask, 0, 0, 0, 0);
        IP4_ADDR(&gateway, 0, 0, 0, 0);
        DEBUG_ONLY({
            printf("Network Manager: Configured for DHCP\n");
        });
    } else {
        // Use static IP configuration
        ip_addr.addr = g_network_config.static_ip.addr;
        netmask.addr = g_network_config.static_netmask.addr;
        gateway.addr = g_network_config.static_gateway.addr;
        DEBUG_ONLY({
            printf("Network Manager: Configured for static IP\n");
        });
    }
    
    // Initialize the network interface
    g_netif = lwip_netif_enc28j60_init(&ip_addr, &netmask, &gateway);
    if (!g_netif) {
        DEBUG_ONLY({
            printf("Network Manager: Failed to initialize network interface\n");
        });
        return false;
    }
    
    DEBUG_ONLY({
        printf("Network Manager: lwIP initialization complete\n");
    });
    return true;
}

/**
 * @brief Reconfigure the network interface after a config change
 */
bool network_manager_reconfigure(const network_config_t* config) {

    if(!g_netif || !config) {
        return false;
    }

    // Check if DHCP is already in progress - don't restart it!
    struct dhcp *dhcp = netif_dhcp_data(g_netif);
    bool dhcp_in_progress = (dhcp != NULL && dhcp->state != DHCP_STATE_OFF && dhcp->state != DHCP_STATE_BOUND);
    
    if (dhcp_in_progress && config->use_dhcp) {
        printf("Network Manager: DHCP already in progress (state=%d), not restarting\n", dhcp->state);
        // Just update the config but don't restart DHCP
        memcpy(&g_network_config, config, sizeof(network_config_t));
        return true;
    }

    printf("Network Manager: 🔧 Reconfiguring interface with new settings\n");
    printf("Network Manager: 📍 Target IP: %d.%d.%d.%d | DHCP: %s\n",
           (int)((config->static_ip.addr >> 0) & 0xFF),
           (int)((config->static_ip.addr >> 8) & 0xFF),
           (int)((config->static_ip.addr >> 16) & 0xFF),
           (int)((config->static_ip.addr >> 24) & 0xFF),
           config->use_dhcp ? "ENABLED" : "DISABLED");

    // Update global configuration with new settings
    memcpy(&g_network_config, config, sizeof(network_config_t));

    // Update MAC address on ENC28J60 and lwIP netif if it changed
    if (g_netif) {
        if (memcmp(g_netif->hwaddr, config->mac_address, 6) != 0) {
            enc28j60_set_mac_address(config->mac_address);
            memcpy(g_netif->hwaddr, config->mac_address, 6);
            printf("Network Manager: MAC updated to %02X:%02X:%02X:%02X:%02X:%02X\n",
                   config->mac_address[0], config->mac_address[1], config->mac_address[2],
                   config->mac_address[3], config->mac_address[4], config->mac_address[5]);
        }
    }

    core1_timer_cancel(CORE1_TIMER_NETWORK_TIMEOUT);
    core1_timer_cancel(CORE1_TIMER_DHCP_DISCOVER);

    //stop dhcp if running
    dhcp_stop(g_netif);
    //put interface down for reconfig
    netif_set_down(g_netif);

    // Initialize lwIP network interface for ENC28J60
    ip4_addr_t ip_addr, netmask, gateway;
    
    // CRITICAL FIX: Use NEW config parameter instead of old g_network_config
    if (config->use_dhcp) {
        // Use zero addresses for DHCP
        IP4_ADDR(&ip_addr, 0, 0, 0, 0);
        IP4_ADDR(&netmask, 0, 0, 0, 0);
        IP4_ADDR(&gateway, 0, 0, 0, 0);
        printf("Network Manager: ✅ Configured for DHCP\n");
    } else {
        // Use NEW static IP configuration from parameter
        ip_addr.addr = config->static_ip.addr;        // FIXED: Use config parameter
        netmask.addr = config->static_netmask.addr;   // FIXED: Use config parameter  
        gateway.addr = config->static_gateway.addr;   // FIXED: Use config parameter
        printf("Network Manager: ✅ Configured for static IP: %d.%d.%d.%d\n",
               (int)((config->static_ip.addr >> 0) & 0xFF),
               (int)((config->static_ip.addr >> 8) & 0xFF),
               (int)((config->static_ip.addr >> 16) & 0xFF),
               (int)((config->static_ip.addr >> 24) & 0xFF));
    }

    netif_set_ipaddr(g_netif, &ip_addr);
    netif_set_netmask(g_netif, &netmask); 
    netif_set_gw(g_netif, &gateway);
    
    //put interface back up after reconfig
    netif_set_up(g_netif);

    // FIXED: Use NEW config parameter for DHCP decision
    if(config->use_dhcp) {
        printf("Network Manager: 🔄 Starting DHCP client\n");
        //start dhcp by sending a request
        return(network_manager_process_dhcp());
    }
    
    printf("Network Manager: ✅ Static IP configuration applied successfully\n");
    core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, sys_timeouts_sleeptime());
        
    return true;
}

/**
 * @brief Process DHCP client operations
 */
bool network_manager_process_dhcp(void) {
    if (!g_netif) {
        return false;
    }
    
    // Check if DHCP is already running
    if (dhcp_supplied_address(g_netif)) {
        DEBUG_ONLY({
            printf("Network Manager: DHCP bound, network ready\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 0);
        return true;
    }
    
    // Start DHCP if not already started
    struct dhcp *dhcp = netif_dhcp_data(g_netif);
    if (dhcp == NULL || dhcp->state == DHCP_STATE_OFF) {
        printf("DHCP: Starting DHCP client...\n");
        printf("DHCP: netif status: up=%d, link_up=%d\n", 
            netif_is_up(g_netif), netif_is_link_up(g_netif));
        
        // Print MAC address being used
        printf("DHCP: MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
            g_netif->hwaddr[0], g_netif->hwaddr[1], g_netif->hwaddr[2],
            g_netif->hwaddr[3], g_netif->hwaddr[4], g_netif->hwaddr[5]);

        err_t err = dhcp_start(g_netif);
        if (err != ERR_OK) {
            printf("DHCP: Failed to start DHCP client (error %d)\n", err);
            return false;
        }
        printf("DHCP: dhcp_start() returned OK\n");
        
        // Record DHCP start time for general timeout detection
        g_dhcp_start_time = to_ms_since_boot(get_absolute_time());
        core1_timer_set(CORE1_TIMER_DHCP_DISCOVER, g_network_config.dhcp_timeout_ms);
        // CRITICAL: Also set network timeout timer so sys_check_timeouts() gets called
        // This is needed for DHCP and ACD timers to advance properly
        core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, 10);  // 10ms initial, will be updated
        g_network_stats.dhcp_requests++;
        printf("DHCP: DHCP DISCOVER should be sent, timeout=%u ms\n", g_network_config.dhcp_timeout_ms);
    } else {
        printf("DHCP: Already running, state=%d\n", dhcp ? dhcp->state : -1);
    }
    return true;
}

/**
 * @brief Check DHCP status and handle timeouts
 */
bool network_manager_check_dhcp_status(void) {
    if (!g_netif) {
        return false;
    }
    
    // CRITICAL: Poll actual hardware link status and update netif
    // This is needed when Ethernet is connected AFTER boot
    static bool last_link_status = false;
    bool current_link_status = enc28j60_get_link_status();
    
    if (current_link_status != last_link_status) {
        printf("DHCP: Link status changed: %d -> %d\n", last_link_status, current_link_status);
        last_link_status = current_link_status;
        
        if (current_link_status) {
            // Link just came up - update netif and restart DHCP
            netif_set_link_up(g_netif);
            printf("DHCP: Link UP - restarting DHCP discovery\n");
            
            // Stop any existing DHCP and restart fresh
            struct dhcp *dhcp = netif_dhcp_data(g_netif);
            if (dhcp) {
                dhcp_stop(g_netif);
            }
            dhcp_start(g_netif);
            // Record DHCP start time for general timeout detection
            g_dhcp_start_time = to_ms_since_boot(get_absolute_time());
            core1_timer_set(CORE1_TIMER_DHCP_DISCOVER, g_network_config.dhcp_timeout_ms);
            g_network_stats.dhcp_requests++;
        } else {
            // Link went down
            netif_set_link_down(g_netif);
            printf("DHCP: Link DOWN\n");
        }
    }
    
    // If link is down, don't bother checking DHCP status
    // BUT still check for general DHCP timeout to apply static IP fallback
    if (!current_link_status) {
        // Even with link down, check if we should fall back to static IP
        // This ensures we have an IP configured when link eventually comes up
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (g_dhcp_start_time != 0 && !dhcp_supplied_address(g_netif)) {
            uint32_t dhcp_elapsed = current_time - g_dhcp_start_time;
            if (dhcp_elapsed > g_network_config.dhcp_timeout_ms) {
                printf("DHCP: General timeout after %u ms (link down, no DHCP response)\n", dhcp_elapsed);
                printf("DHCP: Falling back to configured static IP\n");
                
                // Stop DHCP
                dhcp_stop(g_netif);
                
                // Apply configured static IP as fallback
                ip4_addr_t static_ip, gateway, netmask;
                static_ip.addr = g_network_config.static_ip.addr;
                gateway.addr = g_network_config.static_gateway.addr;
                netmask.addr = g_network_config.static_netmask.addr;
                
                netif_set_ipaddr(g_netif, &static_ip);
                netif_set_netmask(g_netif, &netmask);
                netif_set_gw(g_netif, &gateway);
                
                // Set interface administratively UP so TCP listeners can be created.
                // Do NOT fake netif_set_link_up() here — the link is physically down.
                // netif_set_link_up() must only be called when the PHY reports real link.
                // Faking it prevents lwIP from detecting the real link-up transition
                // later, which breaks TCP (SYN-ACK never sent or ARP table stale).
                netif_set_up(g_netif);
                
                printf("DHCP: Static IP fallback applied: %d.%d.%d.%d (link still down)\n",
                       (int)((static_ip.addr >> 0) & 0xFF),
                       (int)((static_ip.addr >> 8) & 0xFF),
                       (int)((static_ip.addr >> 16) & 0xFF),
                       (int)((static_ip.addr >> 24) & 0xFF));
                
                // Clear DHCP start time to prevent repeated fallback attempts
                g_dhcp_start_time = 0;
                
                // Cancel DHCP timer and start network timeout timer
                core1_timer_cancel(CORE1_TIMER_DHCP_DISCOVER);
                core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, sys_timeouts_sleeptime());
                
                return true;  // Network is now ready with static IP
            }
        }
        return false;
    }
    
    // Process network interface (packet RX)
    lwip_netif_enc28j60_process();
    
    // CRITICAL: Process lwIP timers to advance DHCP and ACD state machines
    // Without this, ACD (Address Conflict Detection in state 8) will hang!
    sys_check_timeouts();
    
    // Track DHCP state 8 hang detection
    static uint32_t state_8_start_time = 0;
    static bool state_8_timeout_triggered = false;
    const uint32_t STATE_8_TIMEOUT_MS = 5000;  // 5 seconds max in state 8
    
    struct dhcp *dhcp = netif_dhcp_data(g_netif);
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Detect DHCP state 8 hang and implement fallback
    if (dhcp && dhcp->state == 8) {  // DHCP_STATE_CHECKING (ACD)
        if (state_8_start_time == 0) {
            state_8_start_time = current_time;
            DEBUG_ONLY({
                printf("DHCP: Entered state 8 (ACD checking) at %u ms\n", current_time);
            });
        } else if (!state_8_timeout_triggered && (current_time - state_8_start_time) > STATE_8_TIMEOUT_MS) {
            // State 8 timeout detected - fall back to static IP
            state_8_timeout_triggered = true;
            
            DEBUG_ONLY({
                printf("DHCP: State 8 hang detected after %u ms! Falling back to static IP\n", 
                       current_time - state_8_start_time);
            });
            
            // Stop DHCP
            dhcp_stop(g_netif);
            
            // FIXED: Use configured static IP as fallback (not hardcoded IP)
            ip4_addr_t static_ip, gateway, netmask;
            static_ip.addr = g_network_config.static_ip.addr;      // Use CONFIGURED static IP
            gateway.addr = g_network_config.static_gateway.addr;   // Use CONFIGURED gateway
            netmask.addr = g_network_config.static_netmask.addr;   // Use CONFIGURED netmask
            
            // Apply configured static IP as fallback
            netif_set_ipaddr(g_netif, &static_ip);
            netif_set_netmask(g_netif, &netmask);
            netif_set_gw(g_netif, &gateway);
            
            // Set interface administratively UP — do NOT fake link-up.
            // Link state must reflect actual PHY status.
            netif_set_up(g_netif);

            printf("DHCP: DHCP timeout - falling back to CONFIGURED static IP: %d.%d.%d.%d\n",
                   (static_ip.addr >> 0) & 0xFF,
                   (static_ip.addr >> 8) & 0xFF,
                   (static_ip.addr >> 16) & 0xFF,
                   (static_ip.addr >> 24) & 0xFF);
            printf("DHCP: Network now operational with user-configured IP\n");
            
            // Cancel DHCP timer and start network timeout timer instead
            core1_timer_cancel(CORE1_TIMER_DHCP_DISCOVER);
            core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, sys_timeouts_sleeptime());
            
            return true;  // Network is now ready with static IP
        }
    } else {
        // Reset state 8 tracking when not in state 8
        if (state_8_start_time != 0) {
            state_8_start_time = 0;
            state_8_timeout_triggered = false;
        }
    }
    
    // General DHCP timeout check - falls back to static IP if no DHCP response
    // This handles the case where no DHCP server is present at all
    if (g_dhcp_start_time != 0 && !dhcp_supplied_address(g_netif)) {
        uint32_t dhcp_elapsed = current_time - g_dhcp_start_time;
        if (dhcp_elapsed > g_network_config.dhcp_timeout_ms) {
            printf("DHCP: General timeout after %u ms (no DHCP server response)\n", dhcp_elapsed);
            printf("DHCP: Falling back to configured static IP\n");
            
            // Stop DHCP
            dhcp_stop(g_netif);
            
            // Apply configured static IP as fallback
            ip4_addr_t static_ip, gateway, netmask;
            static_ip.addr = g_network_config.static_ip.addr;
            gateway.addr = g_network_config.static_gateway.addr;
            netmask.addr = g_network_config.static_netmask.addr;
            
            netif_set_ipaddr(g_netif, &static_ip);
            netif_set_netmask(g_netif, &netmask);
            netif_set_gw(g_netif, &gateway);
            
            // Set interface administratively UP — do NOT fake link-up.
            // Link state must reflect actual PHY status.
            netif_set_up(g_netif);
            
            printf("DHCP: Static IP fallback applied (link still down): %d.%d.%d.%d\n",
                   (int)((static_ip.addr >> 0) & 0xFF),
                   (int)((static_ip.addr >> 8) & 0xFF),
                   (int)((static_ip.addr >> 16) & 0xFF),
                   (int)((static_ip.addr >> 24) & 0xFF));
            printf("DHCP: netif flags=0x%02X, up=%d, link_up=%d\n", 
                   g_netif->flags, netif_is_up(g_netif), netif_is_link_up(g_netif));
            
            // Clear DHCP start time to prevent repeated fallback attempts
            g_dhcp_start_time = 0;
            
            // Cancel DHCP timer and start network timeout timer
            core1_timer_cancel(CORE1_TIMER_DHCP_DISCOVER);
            core1_timer_set(CORE1_TIMER_NETWORK_TIMEOUT, sys_timeouts_sleeptime());
            
            return true;  // Network is now ready with static IP
        }
    }
    
    // DHCP status debug (always print during DHCP debugging)
    static uint32_t last_dhcp_debug = 0;
    if (current_time - last_dhcp_debug > 2000) {  // Print every 2 seconds
        last_dhcp_debug = current_time;
        printf("=== DHCP Status @%ums ===\n", current_time);
        printf("  netif: up=%d, link=%d\n", netif_is_up(g_netif), netif_is_link_up(g_netif));
        if (dhcp) {
            printf("  DHCP state: %d", dhcp->state);
            if (dhcp->state == 8 && state_8_start_time > 0) {
                printf(" (ACD for %u ms)", current_time - state_8_start_time);
            }
            printf("\n");
            if (!ip4_addr_isany(&dhcp->server_ip_addr)) {
                printf("  Server: %s\n", ip4addr_ntoa(&dhcp->server_ip_addr));
            }
            printf("  Offered IP: %s\n", ip4addr_ntoa(&dhcp->offered_ip_addr));
        } else {
            printf("  No DHCP data yet\n");
        }
        if (!ip4_addr_isany(netif_ip4_addr(g_netif))) {
            printf("  Current IP: %s\n", ip4addr_ntoa(netif_ip4_addr(g_netif)));
        }
        printf("========================\n");
    }

    // Check if we got an IP address (either from DHCP or static fallback)
    if (dhcp_supplied_address(g_netif) || !ip4_addr_isany(netif_ip4_addr(g_netif))) {
        //stop the timer
        core1_timer_cancel(CORE1_TIMER_DHCP_DISCOVER);
        
        //g_network_status = NETWORK_STATUS_READY;
        
        char ip_str[16];
        sprintf(ip_str, "%u.%u.%u.%u",
                (unsigned)ip4_addr1_16(netif_ip4_addr(g_netif)),
                (unsigned)ip4_addr2_16(netif_ip4_addr(g_netif)),
                (unsigned)ip4_addr3_16(netif_ip4_addr(g_netif)),
                (unsigned)ip4_addr4_16(netif_ip4_addr(g_netif)));
        
        const char* source = dhcp_supplied_address(g_netif) ? "DHCP" : "Static Fallback";
        DEBUG_ONLY({
            printf("Network Manager: %s successful! IP: %s\n", source, ip_str);
            printf("Network Manager: Network ready for ping and TCP/IP operations\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 1);
        
        return true;
    }

    return false;
    /*
    // Check for timeout
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_time - g_dhcp_start_time > g_network_config.dhcp_timeout_ms) {
        printf("Network Manager: DHCP timeout after %u ms\n", g_network_config.dhcp_timeout_ms);
        
        // Stop DHCP and try again
        dhcp_stop(g_netif);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_ERROR, 3);
    }
    */
}

/**
 * @brief Update network status based on current conditions
 */
static void network_manager_update_status(void) {
    g_network_stats.status = g_network_status;
    
    // Update IP address information if available
    if (g_netif && netif_is_up(g_netif)) {
        g_network_stats.current_ip.addr = netif_ip4_addr(g_netif)->addr;
        g_network_stats.current_netmask.addr = netif_ip4_netmask(g_netif)->addr;
        g_network_stats.current_gateway.addr = netif_ip4_gw(g_netif)->addr;
        memcpy(g_network_stats.current_mac, g_netif->hwaddr, 6);
    }
    
    // Update uptime (approximately)
    static uint32_t last_update_time = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if (last_update_time == 0) {
        last_update_time = current_time;
    } else if (current_time > last_update_time + 1000) {  // Update every second
        g_network_stats.uptime_seconds += (current_time - last_update_time) / 1000;
        last_update_time = current_time;
    }
}

/**
 * @brief Update network statistics from hardware
 */
static void network_manager_update_stats(void) {
    if (!g_network_initialized) {
        return;
    }
    
    const enc28j60_state_t* enc_state = enc28j60_get_state();
    if (enc_state) {
        g_network_stats.packets_tx = enc_state->packets_sent;
        g_network_stats.packets_rx = enc_state->packets_received;
        
        // For bytes, estimate based on packets (rough approximation)
        g_network_stats.bytes_tx = enc_state->packets_sent * 64;  // Assume 64 bytes average
        g_network_stats.bytes_rx = enc_state->packets_received * 64;
    }
}

/**
 * @brief Get current IP address from lwIP
 */
bool network_manager_get_ip_address(simple_ip_addr_t* ip_addr) {
    if (!ip_addr || !g_network_initialized || !g_netif) {
        return false;
    }
    
    // Get real IP address from lwIP interface
    if (netif_is_up(g_netif) && !ip4_addr_isany(netif_ip4_addr(g_netif))) {
        ip_addr->addr = netif_ip4_addr(g_netif)->addr;
        return true;
    }
    
    return false;
}

/**
 * @brief Check if DHCP has assigned an IP address
 */
bool network_manager_is_dhcp_bound(void) {
    if (!g_network_initialized || !g_netif) {
        return false;
    }
    
    // Check if DHCP has supplied a valid IP address
    return dhcp_supplied_address(g_netif);
}

/**
 * @brief Check what to do after packet was sent
 */
void network_manager_process_tx(void)
{
    //enc28j60_process_txif_interrupt();
}

/**
 * @brief Convert IP address to string representation (simplified for testing)
 */
char* network_manager_ip_to_string(const simple_ip_addr_t* ip_addr, char* buffer) {
    if (!ip_addr || !buffer) {
        return NULL;
    }
    
    uint32_t addr = ip_addr->addr;
    snprintf(buffer, 16, "%d.%d.%d.%d",
             (int)((addr >> 0) & 0xFF),
             (int)((addr >> 8) & 0xFF),
             (int)((addr >> 16) & 0xFF),
             (int)((addr >> 24) & 0xFF));
             
    return buffer;
}