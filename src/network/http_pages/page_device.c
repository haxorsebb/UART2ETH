/**
 * @file page_device.c
 * @brief Device status page generation implementation
 * 
 * Generates the main device information page with network config,
 * UART channel status, and system information.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 */

#include "network/http_pages/page_device.h"
#include "network/http_server.h"
#include "network/network_manager.h"
#include "shared_memory.h"
#include "device_mode.h"
#include "config/version.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include "timestamp.h"

// External reference to server statistics (from http_server.c)
extern http_server_stats_t g_server_stats;

/**
 * @brief Generate device status page
 * 
 * Creates complete HTTP response with device information including
 * network configuration, UART channel details, and system status.
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * 
 * Documentation Reference: ADR-018
 */
void http_generate_device_page(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    // Get current IP address
    simple_ip_addr_t ip_addr;
    bool has_ip = network_manager_get_ip_address(&ip_addr);
    char ip_str[16] = "Not Available";
    
    if (has_ip) {
        network_manager_ip_to_string(&ip_addr, ip_str);
    }
    
    // Get MAC address
    uint8_t mac_addr[6];
    network_manager_get_mac_address(mac_addr);
    
    // Get device configuration
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    // Format subnet mask
    uint32_t nm = layout->config.network.static_netmask.addr;
    char netmask_str[16];
    snprintf(netmask_str, sizeof(netmask_str), "%d.%d.%d.%d",
             (int)((nm >> 0) & 0xFF), (int)((nm >> 8) & 0xFF),
             (int)((nm >> 16) & 0xFF), (int)((nm >> 24) & 0xFF));
    
    // Format gateway
    uint32_t gw = layout->config.network.static_gateway.addr;
    char gateway_str[16];
    snprintf(gateway_str, sizeof(gateway_str), "%d.%d.%d.%d",
             (int)((gw >> 0) & 0xFF), (int)((gw >> 8) & 0xFF),
             (int)((gw >> 16) & 0xFF), (int)((gw >> 24) & 0xFF));
    
#if DEVICE_CHANNEL_2_ENABLED
    char uart2_pins[16];
    if (layout->config.channels[CHANNEL_2].enabled) {
        snprintf(uart2_pins, sizeof(uart2_pins), "GP%d/GP%d", 
                layout->config.channels[CHANNEL_2].tx_gpio,
                layout->config.channels[CHANNEL_2].rx_gpio);
    } else {
        strcpy(uart2_pins, "-");
    }
#endif
    
#if DEVICE_CHANNEL_3_ENABLED
    char uart3_pins[16];
    if (layout->config.channels[CHANNEL_3].enabled) {
        snprintf(uart3_pins, sizeof(uart3_pins), "GP%d/GP%d", 
                layout->config.channels[CHANNEL_3].tx_gpio,
                layout->config.channels[CHANNEL_3].rx_gpio);
    } else {
        strcpy(uart3_pins, "-");
    }
#endif

#if DEVICE_CHANNEL_4_ENABLED
    char uart4_pins[16];
    if (layout->config.channels[CHANNEL_4].enabled) {
        snprintf(uart4_pins, sizeof(uart4_pins), "GP%d/GP%d", 
                layout->config.channels[CHANNEL_4].tx_gpio,
                layout->config.channels[CHANNEL_4].rx_gpio);
    } else {
        strcpy(uart4_pins, "-");
    }
#endif
    
    // DEBUG: Print channel configuration
    DEBUG_ONLY({
        printf("HTTP: Device mode: %s, Data channels: %d\n", DEVICE_MODE_NAME, DEVICE_NUM_DATA_CHANNELS);
        printf("HTTP: Channel config - CH0:%s CH1:%s\n",
               layout->config.channels[CHANNEL_0].enabled ? "ON" : "OFF",
               layout->config.channels[CHANNEL_1].enabled ? "ON" : "OFF");
        printf("HTTP: Generating HTML response (buffer size: %d)\n", (int)buffer_size);
    });
    
    // Build HTML response with device-mode-aware channel list
    snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>UART2ETH Device Information</title>\n"
        "    <link rel=\"stylesheet\" href=\"/styles.css\">\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <div class=\"header\">\n"
        "            <h1>UART2ETH Device Information <span class=\"mode-badge\">%s</span>"
#ifdef FACTORY_INTERNAL_VERSION
        " <span class=\"factory-badge\">FACTORY INTERNAL</span>"
#endif
        "</h1>\n"
        "            <p>Serial to Network Bridge - Device Status | Firmware " FIRMWARE_VERSION_STRING " - " _TIMEZ_"</p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"nav-links\">\n"
        "            <a href=\"/\">Status</a>\n"
        "            <a href=\"/config\">Configuration</a>\n"
        "            <a href=\"/update\">Update</a>\n"
#ifdef FACTORY_INTERNAL_VERSION
        "            <a href=\"/factory\">FACTORY DEFAULTS</a>\n"
#endif
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <h2>Network Configuration</h2>\n"
        "            <p><span class=\"label\">IP Address:</span> <span class=\"value\">%s</span></p>\n"
        "            <p><span class=\"label\">Subnet Mask:</span> <span class=\"value\">%s</span></p>\n"
        "            <p><span class=\"label\">Gateway:</span> <span class=\"value\">%s</span></p>\n"
        "            <p><span class=\"label\">MAC Address:</span> <span class=\"value\">%02X:%02X:%02X:%02X:%02X:%02X</span></p>\n"
        "            <p><span class=\"label\">DHCP:</span> <span class=\"status-ok\">%s</span></p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <h2>UART Channel Ports</h2>\n"
        "            <table class=\"port-table\">\n"
        "                <tr>\n"
        "                    <th>Channel</th>\n"
        "                    <th>TCP Port</th>\n"
        "                    <th>Status</th>\n"
        "                    <th>GPIO Pins</th>\n"
        "                </tr>\n"
#if DEVICE_CHANNEL_4_ENABLED
        "                <tr>\n"
        "                    <td>UART4</td>\n"
        "                    <td>%d</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>%s</td>\n"
        "                </tr>\n"
#endif
        "            </table>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <h2>Device Information</h2>\n"
        "            <p><span class=\"label\">Firmware:</span> <span class=\"value\">UART2ETH v" FIRMWARE_VERSION_STRING " (%s)</span></p>\n"
        "            <p><span class=\"label\">Build Type:</span> <span class=\"value\">" FIRMWARE_BUILD_TYPE "</span></p>\n"
        "            <p><span class=\"label\">Hardware:</span> <span class=\"value\">RP2350 + ENC28J60</span></p>\n"
        "            <p><span class=\"label\">Uptime:</span> <span class=\"value\">%d seconds</span></p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <p><em>Connect to the TCP ports above using telnet, netcat, or your application to access UART data.</em></p>\n"
        "            <p><em>Example: telnet %s %d</em></p>\n"
        "        </div>\n"
        "    </div>\n"
        "</body>\n"
        "</html>\r\n",
        DEVICE_MODE_NAME,
        ip_str,
        netmask_str,
        gateway_str,
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        layout->config.network.use_dhcp ? "Enabled" : "Static",
#if DEVICE_CHANNEL_4_ENABLED
        layout->config.channels[CHANNEL_4].tcp_port,
        layout->config.channels[CHANNEL_4].enabled ? "Active" : "Disabled",
        uart4_pins,
#endif
        DEVICE_MODE_NAME,
        (int)g_server_stats.uptime_seconds,
        ip_str,
        layout->config.channels[CHANNEL_4].tcp_port
    );
}
