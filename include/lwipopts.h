/**
 * @file lwipopts.h
 * @brief lwIP Configuration Options for UART2ETH Project
 * 
 * Configuration for lwIP TCP/IP stack optimized for ENC28J60 Ethernet controller
 * on RP2350 microcontroller. Configured for DHCP client and ICMP (ping) support.
 * 
 * Documentation Reference:
 * - lwIP Documentation: Configuration Options
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/**
 * @brief System-specific configuration
 */

/* No operating system - bare metal */
#define NO_SYS                     1

/* Platform-specific includes */
#include <stdint.h>
#include "pico/stdlib.h"

/**
 * @brief Memory management configuration
 */

/* Memory alignment */
#define MEM_ALIGNMENT              4

/* Heap memory size for lwIP (8KB) */
#define MEM_SIZE                   8192

/* Enable custom memory pools */
#define MEMP_MEM_MALLOC            0

/* Number of concurrent TCP connections - increased for 4 UART channels */
#define MEMP_NUM_TCP_PCB           8

/* Number of listening TCP connections - must support 4 UART channels */
#define MEMP_NUM_TCP_PCB_LISTEN    4

/* Number of UDP connections */
#define MEMP_NUM_UDP_PCB           4

/* Number of network buffers */
#define MEMP_NUM_PBUF              16

/* Number of network buffer pools */
#define MEMP_NUM_PBUF_POOL         16

/**
 * @brief Protocol support configuration
 */

/* Enable IPv4 */
#define LWIP_IPV4                  1

/* Disable IPv6 for simplicity */
#define LWIP_IPV6                  0

/* Enable ICMP (required for ping) */
#define LWIP_ICMP                  1

/* Enable DHCP client */
#define LWIP_DHCP                  1

/* Enable ARP (required for Ethernet) */
#define LWIP_ARP                   1

/* Enable Ethernet */
#define LWIP_ETHERNET              1

/**
 * @brief TCP configuration
 */

/* Enable TCP */
#define LWIP_TCP                   1

/* TCP Maximum Segment Size */
#define TCP_MSS                    1460

/* TCP send buffer size */
#define TCP_SND_BUF                2920

/* TCP receive window size */
#define TCP_WND                    2920

/* Enable TCP keepalive */
#define LWIP_TCP_KEEPALIVE         1

/**
 * @brief UDP configuration
 */

/* Enable UDP */
#define LWIP_UDP                   1

/**
 * @brief Network buffer configuration
 */

/* Network buffer size for single packet */
#define PBUF_POOL_SIZE             16

/* Size of each buffer in the pool */
#define PBUF_POOL_BUFSIZE          592

/**
 * @brief Network interface configuration
 */

/* Enable netif hostname support */
#define LWIP_NETIF_HOSTNAME        1

/* Enable link callback */
#define LWIP_NETIF_LINK_CALLBACK   1

/* Enable status callback */
#define LWIP_NETIF_STATUS_CALLBACK 1

/**
 * @brief DHCP configuration
 */

/* DHCP timeout options - SURGICAL DISABLE APPROACH */
#define DHCP_DOES_ARP_CHECK        0      // Disable DHCP ARP checking

/* ARP conflict detection - KEEP ENABLED but make it VERY fast */
#define LWIP_IPV4_ACD              1      // Keep enabled for compilation
#define ACD_TMR_INTERVAL           10     // Very fast timer (10ms instead of 100ms)
#define ACD_PROBE_WAIT             100    // Very short initial delay (100ms instead of 1000ms) 
#define ACD_PROBE_NUM              1      // Only 1 probe packet instead of 3
#define ACD_PROBE_MIN              50     // Very short delay between probes (50ms)
#define ACD_PROBE_MAX              100    // Very short max delay (100ms)
#define ACD_ANNOUNCE_WAIT          100    // Very short announce wait (100ms)
#define ACD_ANNOUNCE_NUM           1      // Only 1 announcement instead of 2
#define ACD_ANNOUNCE_INTERVAL      100    // Very short interval (100ms)
#define ACD_MAX_CONFLICTS          1      // Give up after 1 conflict

/* AutoIP disabled to prevent conflicts */
#define LWIP_AUTOIP                0      // Disable AutoIP completely

/* DHCP fine timers (milliseconds) */
#define DHCP_FINE_TIMER_MSECS      500

/* DHCP coarse timers (seconds) */
#define DHCP_COARSE_TIMER_SECS     60

/**
 * @brief Statistics and debugging
 */

/* Enable lwIP statistics */
#define LWIP_STATS                 1

/* Enable link statistics */
#define LWIP_STATS_DISPLAY         1

/* Disable debug output for production */
#define LWIP_DEBUG                 0

/* Disable specific debug modules */
#define ETHARP_DEBUG               LWIP_DBG_OFF
#define NETIF_DEBUG                LWIP_DBG_OFF
#define PBUF_DEBUG                 LWIP_DBG_OFF
#define API_LIB_DEBUG              LWIP_DBG_OFF
#define API_MSG_DEBUG              LWIP_DBG_OFF
#define SOCKETS_DEBUG              LWIP_DBG_OFF
#define ICMP_DEBUG                 LWIP_DBG_OFF
#define IGMP_DEBUG                 LWIP_DBG_OFF
#define INET_DEBUG                 LWIP_DBG_OFF
#define IP_DEBUG                   LWIP_DBG_OFF
#define IP_REASS_DEBUG             LWIP_DBG_OFF
#define RAW_DEBUG                  LWIP_DBG_OFF
#define MEM_DEBUG                  LWIP_DBG_OFF
#define MEMP_DEBUG                 LWIP_DBG_OFF
#define SYS_DEBUG                  LWIP_DBG_OFF
#define TIMERS_DEBUG               LWIP_DBG_OFF
#define TCP_DEBUG                  LWIP_DBG_OFF
#define TCP_INPUT_DEBUG            LWIP_DBG_OFF
#define TCP_FR_DEBUG               LWIP_DBG_OFF
#define TCP_RTO_DEBUG              LWIP_DBG_OFF
#define TCP_CWND_DEBUG             LWIP_DBG_OFF
#define TCP_WND_DEBUG              LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG           LWIP_DBG_OFF
#define TCP_RST_DEBUG              LWIP_DBG_OFF
#define TCP_QLEN_DEBUG             LWIP_DBG_OFF
#define UDP_DEBUG                  LWIP_DBG_OFF
#define TCPIP_DEBUG                LWIP_DBG_OFF
#define SLIP_DEBUG                 LWIP_DBG_OFF
#define DHCP_DEBUG                 LWIP_DBG_OFF
#define AUTOIP_DEBUG               LWIP_DBG_OFF
#define DNS_DEBUG                  LWIP_DBG_OFF
#define IP6_DEBUG                  LWIP_DBG_OFF

/**
 * @brief Performance optimizations
 */

/* Enable checksum offloading (if supported by hardware) */
#define CHECKSUM_GEN_IP            1
#define CHECKSUM_GEN_UDP           1
#define CHECKSUM_GEN_TCP           1
#define CHECKSUM_GEN_ICMP          1
#define CHECKSUM_CHECK_IP          1
#define CHECKSUM_CHECK_UDP         1
#define CHECKSUM_CHECK_TCP         1
#define CHECKSUM_CHECK_ICMP        1

/**
 * @brief Feature disables (not needed for basic DHCP + ping)
 */

/* Disable socket API (we're using raw API) */
#define LWIP_SOCKET                0

/* Disable netconn API (we're using raw API) */
#define LWIP_NETCONN               0

/* Disable network name resolution */
#define LWIP_DNS                   0

/* Disable SNMP */
#define LWIP_SNMP                  0

/* Disable PPP */
#define PPP_SUPPORT                0

/* Disable slip */
#define LWIP_SLIP                  0

/* Disable auto IP */
#define LWIP_AUTOIP                0

/* Disable IGMP (multicast) */
#define LWIP_IGMP                  0

/**
 * @brief System architecture specific configurations
 */

/* Define platform byte order (RP2350 is little endian) */
#define BYTE_ORDER                 LITTLE_ENDIAN

/* System tick resolution */
#define SYS_LIGHTWEIGHT_PROT       0

/**
 * @brief lwIP system interface functions (NO_SYS = 1)
 */

/* Time functions */
#define LWIP_TIMERS                1

/* Timer resolution in milliseconds */
#define LWIP_TIMERS_CUSTOM         0

/**
 * @brief Custom malloc/free for lwIP (optional optimization)
 */

/* Use standard malloc/free */
#define MEM_LIBC_MALLOC            1

/**
 * @brief Assertion handling
 */

/* Define assertion macro */
#define LWIP_PLATFORM_ASSERT(x) do { \
    printf("LWIP ASSERT: %s at %s:%d\n", x, __FILE__, __LINE__); \
    while(1); \
} while(0)

#endif /* LWIPOPTS_H */
