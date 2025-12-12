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
        return;
    }
    
    memset(config, 0, sizeof(network_config_t));
    
    // TEMPORARY: Use static IP to bypass DHCP ACD issue
    config->use_dhcp = false;  // DISABLE DHCP - use static IP instead
    config->dhcp_timeout_ms = 30000;  // 30 seconds (not used with static)
    
    // Generate unique MAC address based on RP2350 flash unique ID
    pico_unique_board_id_t unique_id;
    // FIXED MAC ADDRESS - no more dynamic generation to avoid ARP conflicts
    // pico_get_unique_board_id(&unique_id);  // Commented out
    
    config->mac_address[0] = 0x02;  // Locally administered, unicast
    config->mac_address[1] = 0x00;  // Fixed values
    config->mac_address[2] = 0x00; 
    config->mac_address[3] = 0x00;
    config->mac_address[4] = 0x00;
    config->mac_address[5] = 0x01;  // Simple fixed MAC: 02:00:00:00:00:01
    
    DEBUG_ONLY({
        printf("Network Manager: Using FIXED MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
               config->mac_address[0], config->mac_address[1], config->mac_address[2],
               config->mac_address[3], config->mac_address[4], config->mac_address[5]);
    });

    // Configure static IP to match your network (DHCP server was 10.10.10.1)
    IP4_ADDR(&config->static_ip, 10, 10, 10, 41);       // Static IP: 10.10.10.41
    IP4_ADDR(&config->static_gateway, 10, 10, 10, 1);   // Gateway: 10.10.10.1 (DHCP server)
    IP4_ADDR(&config->static_netmask, 255, 255, 255, 0); // Netmask: 255.255.255.0

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

    if(!g_netif) {
        return false;
    }

    core1_timer_cancel(CORE1_TIMER_NETWORK_TIMEOUT);
    core1_timer_cancel(CORE1_TIMER_DHCP_DISCOVER);
    

    //stop dhcp if running
    dhcp_stop(g_netif);
    //put if down for reconfig
    netif_set_down(g_netif);

    // Initialize lwIP network interface for ENC28J60
    ip4_addr_t ip_addr, netmask, gateway;
    //prepare ip's
    if (g_network_config.use_dhcp) {
        // Use zero addresses for DHCP
        IP4_ADDR(&ip_addr, 0, 0, 0, 0);
        IP4_ADDR(&netmask, 0, 0, 0, 0);
        IP4_ADDR(&gateway, 0, 0, 0, 0);
        DEBUG_ONLY({
            printf("Network Manager: (Re)Configured for DHCP\n");
        });
    } else {
        // Use static IP configuration
        ip_addr.addr = g_network_config.static_ip.addr;
        netmask.addr = g_network_config.static_netmask.addr;
        gateway.addr = g_network_config.static_gateway.addr;
        DEBUG_ONLY({
            printf("Network Manager: (Re)Configured for static IP\n");
        });
    }

    netif_set_ipaddr(g_netif, &ip_addr);
    netif_set_netmask(g_netif, &netmask); 
    netif_set_gw(g_netif, &gateway);
    
    //put if up after reconfig
    netif_set_up(g_netif);


    if(g_network_config.use_dhcp) {
        //start dhcp by sending a request
        return(network_manager_process_dhcp());
    }
    
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
        DEBUG_ONLY({
            printf("Network Manager: Starting DHCP client\n");
        });
        
        DEBUG_ONLY({
            printf("netif status before DHCP: up=%d, link_up=%d\n", 
            netif_is_up(g_netif), netif_is_link_up(g_netif));
        });

        err_t err = dhcp_start(g_netif);
        if (err != ERR_OK) {
            DEBUG_ONLY({
                printf("Network Manager: Failed to start DHCP client (error %d)\n", err);
            });
            return false;

        }
        DEBUG_ONLY({
            printf("DHCP start called, err=%d\n", err);
        });
        
        core1_timer_set(CORE1_TIMER_DHCP_DISCOVER, g_network_config.dhcp_timeout_ms);
        g_network_stats.dhcp_requests++;
        DEBUG_ONLY({
            printf("Network Manager: DHCP request sent\n");
        });
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
    
    // Process network interface (packet RX)
    lwip_netif_enc28j60_process();
    
    DEBUG_ONLY({
        printf("=== DHCP Debug ===\n");
        printf("netif UP: %d\n", netif_is_up(g_netif));
        printf("netif link UP: %d\n", netif_is_link_up(g_netif));

        struct dhcp *dhcp = netif_dhcp_data(g_netif);
        if (dhcp) {
            printf("DHCP state: %d\n", dhcp->state);
            printf("DHCP server IP: %s\n", ip4addr_ntoa(&dhcp->server_ip_addr));
        } else {
            printf("No DHCP data\n");
        }

        if (!ip4_addr_isany(netif_ip4_addr(g_netif))) {
            printf("Current IP: %s\n", ip4addr_ntoa(netif_ip4_addr(g_netif)));
        } else {
            printf("No IP assigned\n");
        }

        printf("dhcp_supplied_address: %d\n", dhcp_supplied_address(g_netif));
        printf("=================\n");
    });


    // Check if we got an IP address
    if (dhcp_supplied_address(g_netif)) {
        //stop the timer
        core1_timer_cancel(CORE1_TIMER_DHCP_DISCOVER);
        
        //g_network_status = NETWORK_STATUS_READY;
        
        char ip_str[16];
        sprintf(ip_str, "%u.%u.%u.%u",
                (unsigned)ip4_addr1_16(netif_ip4_addr(g_netif)),
                (unsigned)ip4_addr2_16(netif_ip4_addr(g_netif)),
                (unsigned)ip4_addr3_16(netif_ip4_addr(g_netif)),
                (unsigned)ip4_addr4_16(netif_ip4_addr(g_netif)));
        
        DEBUG_ONLY({
            printf("Network Manager: DHCP successful! IP: %s\n", ip_str);
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