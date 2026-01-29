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
#include "network/network_manager.h"
#include "shared_memory.h"
#include "device_mode.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

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
    
    // Generate GPIO pin strings for each UART channel
    char uart1_pins[16];
    
    if (layout->config.channels[CHANNEL_1].enabled) {
        snprintf(uart1_pins, sizeof(uart1_pins), "GP%d/GP%d", 
                layout->config.channels[CHANNEL_1].tx_gpio,
                layout->config.channels[CHANNEL_1].rx_gpio);
    } else {
        strcpy(uart1_pins, "-");
    }
    
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
        "            <h1>UART2ETH Device Information <span class=\"mode-badge\">%s</span></h1>\n"
        "            <p>Serial to Network Bridge - Device Status</p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"nav-links\">\n"
        "            <a href=\"/\">Status</a>\n"
        "            <a href=\"/config\">Configuration</a>\n"
        "            <a href=\"/update\">Update</a>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <h2>Network Configuration</h2>\n"
        "            <p><span class=\"label\">IP Address:</span> <span class=\"value\">%s</span></p>\n"
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
        "                <tr>\n"
        "                    <td>UART0 (Debug)</td>\n"
        "                    <td>4001</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>GP0/GP1</td>\n"
        "                </tr>\n"
        "                <tr>\n"
        "                    <td>UART1</td>\n"
        "                    <td>%d</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>%s</td>\n"
        "                </tr>\n"
#if DEVICE_CHANNEL_2_ENABLED
        "                <tr>\n"
        "                    <td>UART2</td>\n"
        "                    <td>%d</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>%s</td>\n"
        "                </tr>\n"
#endif
#if DEVICE_CHANNEL_3_ENABLED
        "                <tr>\n"
        "                    <td>UART3</td>\n"
        "                    <td>%d</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>%s</td>\n"
        "                </tr>\n"
#endif
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
        "            <p><span class=\"label\">Firmware:</span> <span class=\"value\">UART2ETH v1.0 (%s)</span></p>\n"
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
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        layout->config.network.use_dhcp ? "Enabled" : "Static",
        layout->config.channels[CHANNEL_0].enabled ? "Active" : "Disabled",
        layout->config.channels[CHANNEL_1].tcp_port,
        layout->config.channels[CHANNEL_1].enabled ? "Active" : "Disabled",
        uart1_pins,
#if DEVICE_CHANNEL_2_ENABLED
        layout->config.channels[CHANNEL_2].tcp_port,
        layout->config.channels[CHANNEL_2].enabled ? "Active" : "Disabled",
        uart2_pins,
#endif
#if DEVICE_CHANNEL_3_ENABLED
        layout->config.channels[CHANNEL_3].tcp_port,
        layout->config.channels[CHANNEL_3].enabled ? "Active" : "Disabled",
        uart3_pins,
#endif
#if DEVICE_CHANNEL_4_ENABLED
        layout->config.channels[CHANNEL_4].tcp_port,
        layout->config.channels[CHANNEL_4].enabled ? "Active" : "Disabled",
        uart4_pins,
#endif
        DEVICE_MODE_NAME,
        (int)g_server_stats.uptime_seconds,
        ip_str,
        layout->config.channels[CHANNEL_1].tcp_port
    );
}
