/**
 * @file lwip_netif_enc28j60.h
 * @brief lwIP Network Interface for ENC28J60 Ethernet Controller
 * 
 * Provides standard lwIP netif implementation that bridges the ENC28J60
 * SPI driver to the lwIP TCP/IP stack. Handles packet transmission/reception
 * and integration with lwIP's buffer management (pbuf).
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - lwIP Documentation: Network Interface API
 */

#ifndef LWIP_NETIF_ENC28J60_H
#define LWIP_NETIF_ENC28J60_H

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize lwIP network interface for ENC28J60
 * 
 * Creates and initializes a lwIP netif structure, configures it for
 * ENC28J60 Ethernet controller, and adds it to the lwIP stack.
 * 
 * @param ip_addr Initial IP address (use 0.0.0.0 for DHCP)
 * @param netmask Initial netmask (use 0.0.0.0 for DHCP)
 * @param gateway Initial gateway (use 0.0.0.0 for DHCP)
 * @return Pointer to initialized netif structure, NULL on failure
 */
struct netif* lwip_netif_enc28j60_init(ip4_addr_t* ip_addr, ip4_addr_t* netmask, ip4_addr_t* gateway);

/**
 * @brief Deinitialize lwIP network interface for ENC28J60
 * 
 * Removes the network interface from lwIP stack and cleans up resources.
 */
void lwip_netif_enc28j60_deinit(void);

/**
 * @brief Process network interface (call from main loop)
 * 
 * Processes incoming packets from ENC28J60 and forwards them to lwIP.
 * Should be called regularly from the main network processing loop.
 */
void lwip_netif_enc28j60_process(void);

/**
 * @brief Get pointer to the network interface structure
 * 
 * @return Pointer to netif structure, NULL if not initialized
 */
struct netif* lwip_netif_enc28j60_get_netif(void);

/**
 * @brief Check if network interface is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool lwip_netif_enc28j60_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif // LWIP_NETIF_ENC28J60_H
