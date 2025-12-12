/**
 * @file http_server.c
 * @brief HTTP Server Implementation for UART2ETH Device Information
 * 
 * Implements a simple HTTP server using lwIP Raw API that serves device
 * information including IP address, port numbers, and device status
 * when accessed via web browser on port 80.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Optional WebUI Module
 */

#include "network/http_server.h"
#include "network/network_manager.h"
#include "shared_memory.h"
#include "log_manager.h"
#include "debug.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include <string.h>
#include <stdio.h>

// HTTP server configuration
#define HTTP_SERVER_PORT 80
#define HTTP_SERVER_MAX_CONNECTIONS 2
#define HTTP_RESPONSE_BUFFER_SIZE 2048

// HTTP server state
static struct tcp_pcb* g_http_server_pcb = NULL;
static http_server_status_t g_server_status = HTTP_SERVER_STATUS_UNINITIALIZED;
static http_server_stats_t g_server_stats;
static absolute_time_t g_server_start_time;

// Connection tracking
typedef struct http_connection {
    struct tcp_pcb* pcb;
    bool active;
    uint32_t start_time_ms;
} http_connection_t;

static http_connection_t g_http_connections[HTTP_SERVER_MAX_CONNECTIONS];

// Forward declarations
static err_t http_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
static err_t http_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
static void http_connection_error_callback(void* arg, err_t err);
static err_t http_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len);
static void http_close_connection(http_connection_t* conn);
static void http_send_response(http_connection_t* conn, const char* response, size_t response_len);
static void http_generate_device_page(char* buffer, size_t buffer_size);

/**
 * @brief Initialize HTTP server on port 80
 */
bool http_server_init(void) {
    if (g_server_status == HTTP_SERVER_STATUS_READY) {
        return true; // Already initialized
    }
    
    printf("HTTP Server: Initializing on port %d\n", HTTP_SERVER_PORT);
    g_server_status = HTTP_SERVER_STATUS_INITIALIZING;
    
    // Initialize statistics
    memset(&g_server_stats, 0, sizeof(g_server_stats));
    memset(g_http_connections, 0, sizeof(g_http_connections));
    g_server_start_time = get_absolute_time();
    
    // Create new TCP PCB
    g_http_server_pcb = tcp_new();
    if (!g_http_server_pcb) {
        printf("HTTP Server: Failed to create PCB\n");
        g_server_status = HTTP_SERVER_STATUS_ERROR;
        return false;
    }
    
    // Bind to port 80
    err_t err = tcp_bind(g_http_server_pcb, IP_ADDR_ANY, HTTP_SERVER_PORT);
    if (err != ERR_OK) {
        printf("HTTP Server: Failed to bind to port %d (error %d)\n", HTTP_SERVER_PORT, err);
        tcp_close(g_http_server_pcb);
        g_http_server_pcb = NULL;
        g_server_status = HTTP_SERVER_STATUS_ERROR;
        return false;
    }
    
    // Start listening
    g_http_server_pcb = tcp_listen(g_http_server_pcb);
    if (!g_http_server_pcb) {
        printf("HTTP Server: Failed to listen on port %d\n", HTTP_SERVER_PORT);
        g_server_status = HTTP_SERVER_STATUS_ERROR;
        return false;
    }
    
    // Set accept callback
    tcp_accept(g_http_server_pcb, http_server_accept_callback);
    
    g_server_status = HTTP_SERVER_STATUS_READY;
    printf("HTTP Server: Ready and listening on port %d\n", HTTP_SERVER_PORT);
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, HTTP_SERVER_PORT);
    
    return true;
}

/**
 * @brief Deinitialize HTTP server
 */
void http_server_deinit(void) {
    if (g_server_status == HTTP_SERVER_STATUS_UNINITIALIZED) {
        return;
    }
    
    printf("HTTP Server: Deinitializing\n");
    
    // Close all active connections
    for (int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        if (g_http_connections[i].active) {
            http_close_connection(&g_http_connections[i]);
        }
    }
    
    // Close server PCB
    if (g_http_server_pcb) {
        tcp_close(g_http_server_pcb);
        g_http_server_pcb = NULL;
    }
    
    g_server_status = HTTP_SERVER_STATUS_UNINITIALIZED;
    printf("HTTP Server: Deinitialized\n");
}

/**
 * @brief Process HTTP server tasks
 */
void http_server_process(void) {
    // Update uptime
    if (g_server_status == HTTP_SERVER_STATUS_READY) {
        g_server_stats.uptime_seconds = absolute_time_diff_us(g_server_start_time, get_absolute_time()) / 1000000;
    }
    
    // Update current connections count
    g_server_stats.current_connections = 0;
    for (int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        if (g_http_connections[i].active) {
            g_server_stats.current_connections++;
        }
    }
    
    // The actual HTTP processing is handled by lwIP callbacks
}

/**
 * @brief Check if HTTP server is running
 */
bool http_server_is_running(void) {
    return g_server_status == HTTP_SERVER_STATUS_READY;
}

/**
 * @brief Get HTTP server status
 */
http_server_status_t http_server_get_status(void) {
    return g_server_status;
}

/**
 * @brief Get HTTP server statistics
 */
void http_server_get_stats(http_server_stats_t* stats) {
    if (stats) {
        memcpy(stats, &g_server_stats, sizeof(http_server_stats_t));
    }
}

/**
 * @brief Reset HTTP server statistics
 */
void http_server_reset_stats(void) {
    memset(&g_server_stats, 0, sizeof(g_server_stats));
    g_server_start_time = get_absolute_time();
}

// Private function implementations

/**
 * @brief HTTP accept callback
 */
static err_t http_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err) {
    LWIP_UNUSED_ARG(arg);
    
    if (err != ERR_OK || !newpcb) {
        return ERR_VAL;
    }
    
    printf("HTTP Server: New connection accepted\n");
    
    // Find free connection slot
    http_connection_t* conn = NULL;
    for (int i = 0; i < HTTP_SERVER_MAX_CONNECTIONS; i++) {
        if (!g_http_connections[i].active) {
            conn = &g_http_connections[i];
            break;
        }
    }
    
    if (!conn) {
        printf("HTTP Server: No free connection slots\n");
        tcp_close(newpcb);
        return ERR_MEM;
    }
    
    // Initialize connection
    conn->pcb = newpcb;
    conn->active = true;
    conn->start_time_ms = to_ms_since_boot(get_absolute_time());
    
    // Set callbacks
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, http_connection_recv_callback);
    tcp_err(newpcb, http_connection_error_callback);
    tcp_sent(newpcb, http_connection_sent_callback);
    
    // Update statistics
    g_server_stats.requests_served++;
    if (g_server_stats.current_connections > g_server_stats.max_connections) {
        g_server_stats.max_connections = g_server_stats.current_connections;
    }
    
    return ERR_OK;
}

/**
 * @brief HTTP receive callback
 */
static err_t http_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
    http_connection_t* conn = (http_connection_t*)arg;
    
    if (err != ERR_OK || !conn) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_ARG;
    }
    
    // Connection closed by remote
    if (!p) {
        http_close_connection(conn);
        return ERR_OK;
    }
    
    printf("HTTP Server: Received %d bytes\n", p->tot_len);
    
    // Simple HTTP processing - just send device page for any request
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    http_generate_device_page(response_buffer, sizeof(response_buffer));
    
    http_send_response(conn, response_buffer, strlen(response_buffer));
    
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    
    // Close connection after sending response (HTTP/1.0 style)
    http_close_connection(conn);
    
    return ERR_OK;
}

/**
 * @brief HTTP error callback
 */
static void http_connection_error_callback(void* arg, err_t err) {
    http_connection_t* conn = (http_connection_t*)arg;
    
    LWIP_UNUSED_ARG(err);
    
    printf("HTTP Server: Connection error %d\n", err);
    
    if (conn) {
        conn->pcb = NULL; // PCB already deallocated by lwIP
        conn->active = false;
    }
}

/**
 * @brief HTTP sent callback
 */
static err_t http_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len) {
    http_connection_t* conn = (http_connection_t*)arg;
    
    LWIP_UNUSED_ARG(tpcb);
    
    if (conn) {
        g_server_stats.bytes_sent += len;
        printf("HTTP Server: Sent %d bytes\n", len);
    }
    
    return ERR_OK;
}

/**
 * @brief Close HTTP connection
 */
static void http_close_connection(http_connection_t* conn) {
    if (!conn || !conn->active) {
        return;
    }
    
    if (conn->pcb) {
        tcp_arg(conn->pcb, NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_err(conn->pcb, NULL);
        tcp_sent(conn->pcb, NULL);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }
    
    conn->active = false;
    printf("HTTP Server: Connection closed\n");
}

/**
 * @brief Send HTTP response
 */
static void http_send_response(http_connection_t* conn, const char* response, size_t response_len) {
    if (!conn || !conn->pcb || !response) {
        return;
    }
    
    err_t err = tcp_write(conn->pcb, response, response_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(conn->pcb);
        printf("HTTP Server: Response sent (%d bytes)\n", response_len);
    } else {
        printf("HTTP Server: Failed to send response (error %d)\n", err);
    }
}

/**
 * @brief Generate device information HTML page
 */
static void http_generate_device_page(char* buffer, size_t buffer_size) {
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
    
    // Generate HTML response
    snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>UART2ETH Device Information</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }\n"
        "        .container { background-color: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        "        .header { color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 10px; margin-bottom: 30px; }\n"
        "        .section { margin-bottom: 25px; }\n"
        "        .label { font-weight: bold; color: #34495e; }\n"
        "        .value { color: #2980b9; font-family: monospace; }\n"
        "        .status-ok { color: #27ae60; font-weight: bold; }\n"
        "        .port-table { border-collapse: collapse; width: 100%%; }\n"
        "        .port-table th, .port-table td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n"
        "        .port-table th { background-color: #3498db; color: white; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <div class=\"header\">\n"
        "            <h1>UART2ETH Device Information</h1>\n"
        "            <p>Serial to Network Bridge - Device Status</p>\n"
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
        "                    <td>GP16/GP17</td>\n"
        "                </tr>\n"
        "                <tr>\n"
        "                    <td>UART1</td>\n"
        "                    <td>4002</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>%s</td>\n"
        "                </tr>\n"
        "                <tr>\n"
        "                    <td>UART2</td>\n"
        "                    <td>4003</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>%s</td>\n"
        "                </tr>\n"
        "                <tr>\n"
        "                    <td>UART3</td>\n"
        "                    <td>4004</td>\n"
        "                    <td>%s</td>\n"
        "                    <td>%s</td>\n"
        "                </tr>\n"
        "            </table>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <h2>Device Information</h2>\n"
        "            <p><span class=\"label\">Firmware:</span> <span class=\"value\">UART2ETH v1.0</span></p>\n"
        "            <p><span class=\"label\">Hardware:</span> <span class=\"value\">RP2350 + ENC28J60</span></p>\n"
        "            <p><span class=\"label\">Uptime:</span> <span class=\"value\">%d seconds</span></p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <p><em>Connect to the TCP ports above using telnet, netcat, or your application to access UART data.</em></p>\n"
        "            <p><em>Example: telnet %s 4002</em></p>\n"
        "        </div>\n"
        "    </div>\n"
        "</body>\n"
        "</html>\r\n",
        ip_str,
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        layout->config.network.use_dhcp ? "Enabled" : "Static",
        layout->config.channels[CHANNEL_0].enabled ? "Active" : "Disabled",
        layout->config.channels[CHANNEL_1].enabled ? "Active" : "Disabled",
        layout->config.channels[CHANNEL_1].enabled ? "GP4/GP5" : "-",
        layout->config.channels[CHANNEL_2].enabled ? "Active" : "Disabled",
        layout->config.channels[CHANNEL_2].enabled ? "GP4/GP5" : "-",
        layout->config.channels[CHANNEL_3].enabled ? "Active" : "Disabled",
        layout->config.channels[CHANNEL_3].enabled ? "GP4/GP5" : "-",
        (int)g_server_stats.uptime_seconds,
        ip_str
    );
}
