/**
 * @file lwip_netif_enc28j60.c
 * @brief lwIP Network Interface Implementation for ENC28J60
 * 
 * Standard lwIP netif implementation that bridges ENC28J60 SPI driver
 * to lwIP TCP/IP stack. Handles packet transmission/reception and
 * buffer management between ENC28J60 and lwIP pbuf system.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - lwIP Documentation: Network Interface API
 */
#include "debug.h"
#include "network/lwip_netif_enc28j60.h"
#include "network/enc28j60_driver.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/netifapi.h"
#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// Network interface state
static struct netif g_enc28j60_netif;
static bool g_netif_initialized = false;

// Internal buffer for packet processing
static uint8_t g_rx_buffer[1600];  // Space for maximum Ethernet frame + margin

// Forward declarations of lwIP netif callbacks
static err_t enc28j60_netif_init(struct netif *netif);
static err_t enc28j60_netif_output(struct netif *netif, struct pbuf *p);
static void enc28j60_netif_status_callback(struct netif *netif);
static void enc28j60_netif_link_callback(struct netif *netif);

/**
 * @brief Initialize lwIP network interface for ENC28J60
 */
struct netif* lwip_netif_enc28j60_init(ip4_addr_t* ip_addr, ip4_addr_t* netmask, ip4_addr_t* gateway) {
    if (g_netif_initialized) {
        DEBUG_ONLY({ printf("lwIP netif: Already initialized\n"); });
        return &g_enc28j60_netif;
    }
    
    DEBUG_ONLY({ printf("lwIP netif: Initializing network interface\n"); });
    
    // Initialize the netif structure
    memset(&g_enc28j60_netif, 0, sizeof(g_enc28j60_netif));
    
    // Add network interface to lwIP
    struct netif* netif = netif_add(&g_enc28j60_netif, 
                                   ip_addr, netmask, gateway,
                                   NULL,                    // state (not used)
                                   enc28j60_netif_init,     // init function
                                   ethernet_input);         // input function
    
    if (netif == NULL) {
        DEBUG_ONLY({ printf("lwIP netif: Failed to add network interface\n"); });
        return NULL;
    }
    
    // Set callbacks
    netif_set_status_callback(netif, enc28j60_netif_status_callback);
    netif_set_link_callback(netif, enc28j60_netif_link_callback);
    
    // Set as default interface
    netif_set_default(netif);
    
    g_netif_initialized = true;
    
    DEBUG_ONLY({ printf("lwIP netif: Network interface initialized successfully\n"); });
    return netif;
}

/**
 * @brief Deinitialize lwIP network interface
 */
void lwip_netif_enc28j60_deinit(void) {
    if (!g_netif_initialized) {
        return;
    }
    
    DEBUG_ONLY({ printf("lwIP netif: Deinitializing network interface\n"); });
    
    // Stop DHCP if running
    if (netif_is_up(&g_enc28j60_netif)) {
        dhcp_stop(&g_enc28j60_netif);
    }
    
    // Remove from lwIP
    netif_remove(&g_enc28j60_netif);
    g_netif_initialized = false;
    
    DEBUG_ONLY({ printf("lwIP netif: Network interface deinitialized\n"); });
}


/**
 * @brief Process network interface - handle incoming packets
 */
void lwip_netif_enc28j60_process(void) {
    if (!g_netif_initialized) {
        return;
    }

    // DISABLED: Too verbose. Uncomment for debugging.
#if 0
    DEBUG_ONLY({ 
        static uint32_t last_debug_time = 0;
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        
        if (current_time - last_debug_time >= 5000) {  // Debug every 5 seconds
            const enc28j60_state_t* enc_state = enc28j60_get_state();
            if (enc_state) {
                printf("ENC28J60 Debug: TX=%u, RX=%u, TX_ERR=%u, RX_ERR=%u\n", 
                        enc_state->packets_sent, enc_state->packets_received,
                        enc_state->tx_errors, enc_state->rx_errors);
            }
            
            // Check if there are packets waiting
            bool has_packets = enc28j60_has_rx_packet();
            printf("ENC28J60 Debug: Has RX packets = %s\n", has_packets ? "YES" : "NO");
            last_debug_time = current_time;
        }
    });
#endif
    
    // Check for incoming packets and process them
    int packets_processed = 0;
    while (enc28j60_has_rx_packet() && packets_processed < 2) {  // Limit to prevent infinite loop
        enc28j60_packet_t packet;
        packet.data = g_rx_buffer;
        packet.length = 0;
        packet.valid = false;
        
        // Receive packet from ENC28J60
        if (enc28j60_receive_packet(&packet, sizeof(g_rx_buffer))) {
            if (packet.valid && packet.length > 0) {
                
                // Allocate pbuf for lwIP
                struct pbuf *p = pbuf_alloc(PBUF_RAW, packet.length, PBUF_POOL);
                if (p != NULL) {
                    // Copy packet data to pbuf
                    pbuf_take(p, packet.data, packet.length);
                    
                    // Feed packet to lwIP
                    if (g_enc28j60_netif.input(p, &g_enc28j60_netif) != ERR_OK) {
                        DEBUG_ONLY({ printf("lwIP netif: Failed to input packet to lwIP\n"); });
                        pbuf_free(p);
                    }
                } else {
                    DEBUG_ONLY({ printf("lwIP netif: Failed to allocate pbuf (length=%u)\n", packet.length); });
                }
                packets_processed++;
            }
        }
    }
    // DEBUG_ONLY( {printf("ENC28J60: read %d packets\n",packets_processed );});
}

/**
 * @brief Get pointer to network interface structure
 */
struct netif* lwip_netif_enc28j60_get_netif(void) {
    if (g_netif_initialized) {
        return &g_enc28j60_netif;
    }
    return NULL;
}

/**
 * @brief Check if network interface is initialized
 */
bool lwip_netif_enc28j60_is_initialized(void) {
    return g_netif_initialized;
}

// lwIP netif callback implementations

/**
 * @brief lwIP netif initialization callback
 */
static err_t enc28j60_netif_init(struct netif *netif) {
    DEBUG_ONLY({ printf("lwIP netif: Initializing netif callbacks\n"); });
    
    // Set MAC address from ENC28J60
    uint8_t mac_addr[6];
    enc28j60_get_mac_address(mac_addr);
    
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, mac_addr, 6);
    
    DEBUG_ONLY({ printf("lwIP netif: MAC address %02X:%02X:%02X:%02X:%02X:%02X\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]); });
    
    // Set netif properties
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;           // ARP output for IPv4
    netif->linkoutput = enc28j60_netif_output; // Direct link output
    netif->mtu = 1500;                       // Standard Ethernet MTU
    
    // Set netif flags
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    
    // Set initial link status
    if (enc28j60_get_link_status()) {
        DEBUG_ONLY({ printf("lwIP netif: Initial link status: UP\n"); });
        netif_set_link_up(netif);
        
    } else {
        DEBUG_ONLY({ printf("lwIP netif: Initial link status: DOWN\n"); });
        netif_set_link_down(netif);        
    }
    
    DEBUG_ONLY({ printf("lwIP netif: Netif initialization complete\n"); });
    return ERR_OK;
}

/**
 * @brief lwIP netif output callback - send packet to ENC28J60
 */
static err_t enc28j60_netif_output(struct netif *netif, struct pbuf *p) {
    // Allocate buffer for the complete packet
    uint16_t total_len = p->tot_len;
    
    if (total_len > 1518) {  // Maximum Ethernet frame size
        DEBUG_ONLY({ printf("lwIP netif: Packet too large (%u bytes)\n", total_len); });
        return ERR_BUF;
    }
    
    // Copy pbuf chain to contiguous buffer
    static uint8_t tx_buffer[1600];
    if (pbuf_copy_partial(p, tx_buffer, total_len, 0) != total_len) {
        DEBUG_ONLY({ printf("lwIP netif: Failed to copy pbuf data\n"); });
        return ERR_BUF;
    }


    // Create packet structure for ENC28J60
    enc28j60_packet_t packet;
    packet.data = tx_buffer;
    packet.length = total_len;
    packet.valid = true;
    
    // Send packet via ENC28J60
    if (enc28j60_send_packet(&packet)) {
        return ERR_OK;
    } else {
        DEBUG_ONLY({ printf("lwIP netif: Failed to send packet via ENC28J60\n"); });
        return ERR_IF;
    }
}

/**
 * @brief lwIP netif status change callback
 */
static void enc28j60_netif_status_callback(struct netif *netif) {
    DEBUG_ONLY({ 
        if (netif_is_up(netif)) {
            printf("lwIP netif: Interface UP\n"); 
            if (!ip4_addr_isany(netif_ip4_addr(netif))) {
                char ip_str[16];
                sprintf(ip_str, "%u.%u.%u.%u",
                        (unsigned)ip4_addr1_16(netif_ip4_addr(netif)),
                        (unsigned)ip4_addr2_16(netif_ip4_addr(netif)),
                        (unsigned)ip4_addr3_16(netif_ip4_addr(netif)),
                        (unsigned)ip4_addr4_16(netif_ip4_addr(netif)));
                printf("lwIP netif: IP address assigned: %s\n", ip_str);
            }
        } else {
            printf("lwIP netif: Interface DOWN\n");
        }
    });
}

/**
 * @brief lwIP netif link change callback
 */
static void enc28j60_netif_link_callback(struct netif *netif) {
    DEBUG_ONLY({ 
        if (netif_is_link_up(netif)) {
            printf("lwIP netif: Link UP\n");
        } else {
            printf("lwIP netif: Link DOWN\n");
        }
    });
}
