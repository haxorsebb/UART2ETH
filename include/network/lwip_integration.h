/**
 * @file lwip_integration.h
 * @brief lwIP TCP/IP Stack Integration for RP2350 with ENC28J60
 * 
 * Provides integration layer between lwIP TCP/IP stack and ENC28J60 
 * Ethernet controller. Handles network interface initialization,
 * packet routing, and DHCP client functionality.
 * 
 * Architecture:
 * - Uses lwIP in NO_SYS=1 mode (polling)
 * - Integrates with async_context for periodic processing
 * - Provides netif driver for ENC28J60
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - ADR-002: Ethernet Controller Selection  
 */

#ifndef LWIP_INTEGRATION_H
#define LWIP_INTEGRATION_H

#include <stdint.h>
#include <stdbool.h>
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "pico/async_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network interface configuration
 */
typedef struct {
    ip4_addr_t ip_addr;         // Static IP address (if not using DHCP)
    ip4_addr_t netmask;         // Subnet mask
    ip4_addr_t gateway;         // Gateway address
    bool use_dhcp;              // Enable DHCP client
    uint8_t mac_addr[6];        // MAC address
} lwip_network_config_t;

/**
 * @brief Network interface statistics
 */
typedef struct {
    uint32_t packets_sent;      // Total packets transmitted
    uint32_t packets_received;  // Total packets received
    uint32_t bytes_sent;        // Total bytes transmitted  
    uint32_t bytes_received;    // Total bytes received
    uint32_t tx_errors;         // Transmission errors
    uint32_t rx_errors;         // Reception errors
    uint32_t link_up_count;     // Link up event count
    uint32_t link_down_count;   // Link down event count
} lwip_interface_stats_t;

/**
 * @brief DHCP client status
 */
typedef enum {
    LWIP_DHCP_DISABLED,         // DHCP not enabled
    LWIP_DHCP_REQUESTING,       // DHCP requesting IP
    LWIP_DHCP_BOUND,            // DHCP IP address bound
    LWIP_DHCP_RENEWING,         // DHCP renewing lease
    LWIP_DHCP_REBINDING,        // DHCP rebinding lease
    LWIP_DHCP_FAILED            // DHCP failed to get IP
} lwip_dhcp_status_t;

/**
 * @brief Network interface initialization and control
 */

/**
 * Initialize lwIP stack with ENC28J60 driver
 * @param config Network configuration parameters
 * @return true if initialization successful, false otherwise
 */
bool lwip_integration_init(const lwip_network_config_t* config);

/**
 * Deinitialize lwIP stack and network interface
 */
void lwip_integration_deinit(void);

/**
 * Check if network interface is ready
 * @return true if interface is up and configured, false otherwise
 */
bool lwip_integration_is_ready(void);

/**
 * Get current network interface status
 * @param ip_addr Output for current IP address (can be NULL)
 * @param netmask Output for current netmask (can be NULL)  
 * @param gateway Output for current gateway (can be NULL)
 * @return true if interface is up, false if down
 */
bool lwip_integration_get_status(ip4_addr_t* ip_addr, ip4_addr_t* netmask, ip4_addr_t* gateway);

/**
 * Process pending lwIP work (call from main loop)
 * This function must be called periodically for lwIP operation
 */
void lwip_integration_poll(void);

/**
 * @brief DHCP client functions
 */

/**
 * Start DHCP client on network interface
 * @return true if DHCP started successfully, false otherwise
 */
bool lwip_dhcp_start(void);

/**
 * Stop DHCP client
 */
void lwip_dhcp_stop(void);

/**
 * Get DHCP client status
 * @return Current DHCP status
 */
lwip_dhcp_status_t lwip_dhcp_get_status(void);

/**
 * Check if DHCP has acquired an IP address
 * @param ip_addr Output for DHCP assigned IP address (can be NULL)
 * @return true if IP address acquired, false otherwise
 */
bool lwip_dhcp_is_bound(ip4_addr_t* ip_addr);

/**
 * @brief Network interface statistics and diagnostics
 */

/**
 * Get network interface statistics
 * @param stats Output structure for statistics
 */
void lwip_integration_get_stats(lwip_interface_stats_t* stats);

/**
 * Reset network interface statistics
 */
void lwip_integration_reset_stats(void);

/**
 * Check link status
 * @return true if physical link is up, false if down
 */
bool lwip_integration_get_link_status(void);

/**
 * Get MAC address of network interface
 * @param mac_addr Output buffer for 6-byte MAC address
 */
void lwip_integration_get_mac_address(uint8_t mac_addr[6]);

/**
 * Set MAC address of network interface
 * @param mac_addr 6-byte MAC address to set
 * @return true if successful, false otherwise
 */
bool lwip_integration_set_mac_address(const uint8_t mac_addr[6]);

/**
 * @brief Network utility functions
 */

/**
 * Convert IP address to string
 * @param addr IP address to convert
 * @param buffer Output string buffer (minimum 16 bytes)
 * @return Pointer to buffer
 */
char* lwip_integration_ip_to_string(const ip4_addr_t* addr, char* buffer);

/**
 * Parse IP address from string
 * @param str IP address string (e.g., "192.168.1.100")
 * @param addr Output IP address structure
 * @return true if parsing successful, false otherwise  
 */
bool lwip_integration_string_to_ip(const char* str, ip4_addr_t* addr);

/**
 * @brief Default network configuration helpers
 */

/**
 * Get default network configuration
 * @param config Output configuration structure
 */
void lwip_integration_get_default_config(lwip_network_config_t* config);

/**
 * Generate default MAC address based on chip ID
 * @param mac_addr Output buffer for 6-byte MAC address
 */
void lwip_integration_generate_mac_address(uint8_t mac_addr[6]);

#ifdef __cplusplus
}
#endif

#endif // LWIP_INTEGRATION_H