/**
 * @file network_manager.h
 * @brief Network Manager for UART2ETH Core1 Network Subsystem
 * 
 * Provides high-level network management functions that coordinate
 * between ENC28J60 driver and application layer.
 * Handles initialization, status monitoring, and basic connectivity.
 * 
 * This is the main interface used by Core1 main loop for network
 * operations and forms the foundation for future TCP server functionality.
 * 
 * Documentation Reference:  
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - ADR-007: Event-Driven State Machine Architecture
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Simple IP address structure (for basic testing)
 * Will be replaced with lwIP ip4_addr_t when TCP/IP stack is implemented
 */
typedef struct {
    uint32_t addr;  // IP address in network byte order
} simple_ip_addr_t;

/**
 * @brief Network manager status enumeration
 */
typedef enum {
    NETWORK_STATUS_UNINITIALIZED,   // Not initialized
    NETWORK_STATUS_INITIALIZING,    // Initialization in progress
    NETWORK_STATUS_LINK_DOWN,       // Physical link down
    NETWORK_STATUS_LINK_UP,         // Physical link up, no IP
    NETWORK_STATUS_DHCP_REQUESTING, // DHCP requesting IP address
    NETWORK_STATUS_READY,           // Network ready for use
    NETWORK_STATUS_ERROR            // Error state
} network_status_t;

/**
 * @brief Network configuration structure (simplified for testing)
 */
typedef struct {
    bool use_dhcp;                      // Enable DHCP client (for future use)
    simple_ip_addr_t static_ip;         // Static IP (if DHCP disabled)
    simple_ip_addr_t static_netmask;    // Static netmask (if DHCP disabled)
    simple_ip_addr_t static_gateway;    // Static gateway (if DHCP disabled)
    uint8_t mac_address[6];             // MAC address (auto-generated if all zeros)
    uint32_t dhcp_timeout_ms;           // DHCP timeout in milliseconds
} network_config_t;

/**
 * @brief Network statistics structure (simplified for testing)
 */
typedef struct {
    network_status_t status;            // Current network status
    simple_ip_addr_t current_ip;        // Current IP address
    simple_ip_addr_t current_netmask;   // Current netmask  
    simple_ip_addr_t current_gateway;   // Current gateway
    uint8_t current_mac[6];             // Current MAC address
    uint32_t uptime_seconds;            // Network uptime in seconds
    uint32_t link_up_events;            // Number of link up events
    uint32_t link_down_events;          // Number of link down events
    uint32_t dhcp_requests;             // Number of DHCP requests sent
    uint32_t packets_tx;                // Packets transmitted
    uint32_t packets_rx;                // Packets received
    uint32_t bytes_tx;                  // Bytes transmitted
    uint32_t bytes_rx;                  // Bytes received
} network_stats_t;

/**
 * @brief Network manager initialization and control
 */

/**
 * Initialize network manager with configuration
 * @param config Network configuration parameters
 * @return true if initialization started successfully, false otherwise
 */
bool network_manager_init(const network_config_t* config);

/**
 * Deinitialize network manager
 */
void network_manager_deinit(void);

/**
 * Process network manager tasks (call from Core1 main loop)
 * This function handles link monitoring, DHCP processing, and driver polling
 */
void network_manager_process(void);

/**
 * Check if network is ready for use
 * @return true if network is ready (has link), false otherwise
 */
bool network_manager_is_ready(void);

/**
 * Get current network status
 * @return Current network status enumeration
 */
network_status_t network_manager_get_status(void);

/**
 * @brief Network configuration and status functions
 */

/**
 * Get current network statistics
 * @param stats Output structure for network statistics
 */
void network_manager_get_stats(network_stats_t* stats);

/**
 * Reset network statistics counters
 */
void network_manager_reset_stats(void);

/**
 * Get current IP address (simplified for testing)
 * @param ip_addr Output for current IP address
 * @return true if IP address is valid, false otherwise
 */
bool network_manager_get_ip_address(simple_ip_addr_t* ip_addr);

/**
 * Get current MAC address
 * @param mac_addr Output buffer for 6-byte MAC address
 */
void network_manager_get_mac_address(uint8_t mac_addr[6]);

/**
 * @brief Network connectivity functions
 */

/**
 * Check if physical link is up
 * @return true if link is up, false if down
 */
bool network_manager_is_link_up(void);

/**
 * Check if DHCP has assigned an IP address (placeholder for future)
 * @return false for now (no DHCP implementation yet)
 */
bool network_manager_is_dhcp_bound(void);

/**
 * Restart network interface (useful for recovery)
 * @return true if restart initiated successfully, false otherwise
 */
bool network_manager_restart_interface(void);

/**
 * @brief Utility functions
 */

/**
 * Get default network configuration
 * @param config Output configuration structure with defaults
 */
void network_manager_get_default_config(network_config_t* config);

/**
 * Convert network status to human-readable string
 * @param status Network status enumeration
 * @return String representation of status
 */
const char* network_manager_status_to_string(network_status_t status);

/**
 * Convert IP address to string representation (simplified for testing)
 * @param ip_addr IP address to convert
 * @param buffer Output string buffer (minimum 16 bytes)
 * @return Pointer to buffer containing IP string
 */
char* network_manager_ip_to_string(const simple_ip_addr_t* ip_addr, char* buffer);

/**
 * @brief Diagnostic and testing functions
 */

/**
 * Test basic network connectivity (ping localhost equivalent)
 * @return true if basic stack functionality works, false otherwise
 */
bool network_manager_test_connectivity(void);

/**
 * Get detailed network interface information for diagnostics
 * @param info_buffer Output buffer for diagnostic information
 * @param buffer_size Size of output buffer
 * @return Number of bytes written to buffer
 */
int network_manager_get_diagnostic_info(char* info_buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_MANAGER_H