/**
 * @file network_manager.c
 * @brief Network Manager Implementation for UART2ETH
 * 
 * Provides high-level network management that coordinates between
 * ENC28J60 driver, basic network functionality, and Core1 integration.
 * This is a minimal implementation focused on getting basic connectivity.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - ADR-007: Event-Driven State Machine Architecture
 */

#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// Network manager state
static bool g_network_initialized = false;
static network_status_t g_network_status = NETWORK_STATUS_UNINITIALIZED;
static network_stats_t g_network_stats = {0};
static network_config_t g_network_config = {0};

// Private function declarations
static void network_manager_update_status(void);
static void network_manager_update_stats(void);

/**
 * @brief Initialize network manager with configuration
 */
bool network_manager_init(const network_config_t* config) {
    if (g_network_initialized) {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_INIT, 1);
        return true;  // Already initialized
    }
    
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
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 1);
        g_network_status = NETWORK_STATUS_ERROR;
        return false;
    }
    
    // Set MAC address if provided
    if (g_network_config.mac_address[0] != 0 || g_network_config.mac_address[1] != 0 ||
        g_network_config.mac_address[2] != 0 || g_network_config.mac_address[3] != 0 ||
        g_network_config.mac_address[4] != 0 || g_network_config.mac_address[5] != 0) {
        enc28j60_set_mac_address(g_network_config.mac_address);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_CONFIG, 0);
    }
    
    // Initialize statistics
    memset(&g_network_stats, 0, sizeof(network_stats_t));
    g_network_stats.status = g_network_status;
    
    g_network_initialized = true;
    
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
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 0);
    
    // Deinitialize ENC28J60 driver
    enc28j60_deinit();
    
    // Clear state
    g_network_status = NETWORK_STATUS_UNINITIALIZED;
    g_network_initialized = false;
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 1);
}

/**
 * @brief Process network manager tasks (call from Core1 main loop)
 */
void network_manager_process(void) {
    if (!g_network_initialized) {
        return;
    }
    
    // Update network status based on hardware state
    network_manager_update_status();
    
    // Update statistics
    network_manager_update_stats();
    
    // Basic processing based on current status
    switch (g_network_status) {
        case NETWORK_STATUS_INITIALIZING:
            // Check if hardware is ready
            if (enc28j60_is_ready()) {
                if (enc28j60_get_link_status()) {
                    g_network_status = NETWORK_STATUS_LINK_UP;
                    g_network_stats.link_up_events++;
                    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_UP, 0);
                } else {
                    g_network_status = NETWORK_STATUS_LINK_DOWN;
                }
            }
            break;
            
        case NETWORK_STATUS_LINK_DOWN:
            // Check for link up
            if (enc28j60_get_link_status()) {
                g_network_status = NETWORK_STATUS_LINK_UP;
                g_network_stats.link_up_events++;
                log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_UP, 0);
            }
            break;
            
        case NETWORK_STATUS_LINK_UP:
            // Check for link down
            if (!enc28j60_get_link_status()) {
                g_network_status = NETWORK_STATUS_LINK_DOWN;
                g_network_stats.link_down_events++;
                log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_DOWN, 0);
            } else {
                // Link is up - for minimal implementation, consider this "ready"
                g_network_status = NETWORK_STATUS_READY;
                log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 0);
            }
            break;
            
        case NETWORK_STATUS_READY:
            // Check for link down
            if (!enc28j60_get_link_status()) {
                g_network_status = NETWORK_STATUS_LINK_DOWN;
                g_network_stats.link_down_events++;
                log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_DOWN, 0);
            }
            // In ready state - can handle network operations
            break;
            
        case NETWORK_STATUS_ERROR:
            // Try to recover from error state
            if (enc28j60_is_ready() && enc28j60_get_link_status()) {
                g_network_status = NETWORK_STATUS_LINK_UP;
                log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_UP, 1);
            }
            break;
            
        default:
            g_network_status = NETWORK_STATUS_ERROR;
            break;
    }
}

/**
 * @brief Check if network is ready for use
 */
bool network_manager_is_ready(void) {
    return g_network_initialized && (g_network_status == NETWORK_STATUS_READY);
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
    
    return enc28j60_get_link_status();
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
    
    // Default configuration
    config->use_dhcp = true;
    config->dhcp_timeout_ms = 30000;  // 30 seconds
    
    // Generate default MAC address based on flash unique ID
    // For now, use a simple default MAC address
    config->mac_address[0] = 0x02;  // Locally administered
    config->mac_address[1] = 0x00;
    config->mac_address[2] = 0x00;
    config->mac_address[3] = 0x12;
    config->mac_address[4] = 0x34;
    config->mac_address[5] = 0x56;
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
 * @brief Update network status based on current conditions
 */
static void network_manager_update_status(void) {
    g_network_stats.status = g_network_status;
    
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
 * @brief Get current IP address (simplified - returns placeholder for now)
 */
bool network_manager_get_ip_address(simple_ip_addr_t* ip_addr) {
    if (!ip_addr || !g_network_initialized) {
        return false;
    }
    
    // For now, return a placeholder IP (will be real when lwIP is integrated)
    ip_addr->addr = 0x6401A8C0;  // 192.168.1.100 in network byte order
    return g_network_status == NETWORK_STATUS_READY;
}

/**
 * @brief Check if DHCP has assigned an IP address (placeholder for future)
 */
bool network_manager_is_dhcp_bound(void) {
    // Not implemented yet - will be added when lwIP DHCP is integrated
    return false;
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