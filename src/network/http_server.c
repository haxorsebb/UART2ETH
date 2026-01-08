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
#include "device_mode.h"
#include "log_manager.h"
#include "debug.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// HTTP server configuration
#define HTTP_SERVER_PORT 80
#define HTTP_SERVER_MAX_CONNECTIONS 2
#define HTTP_RESPONSE_BUFFER_SIZE 8192  // Increased to 8KB for configuration page

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

// HTTP request types
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_UNKNOWN
} http_request_type_t;

// Forward declarations
static err_t http_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
static err_t http_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
static void http_connection_error_callback(void* arg, err_t err);
static err_t http_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len);
static void http_close_connection(http_connection_t* conn);
static void http_send_response(http_connection_t* conn, const char* response, size_t response_len);
static void http_generate_device_page(char* buffer, size_t buffer_size);
static void http_generate_config_page(char* buffer, size_t buffer_size);
static http_request_type_t http_parse_request_type(const char* request_data);
static bool http_parse_post_data(const char* post_data, size_t data_len);
static void http_send_redirect(http_connection_t* conn, const char* location);

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
    
    // Copy request data to null-terminated string for parsing
    static char request_buffer[1024];
    size_t copy_len = (p->tot_len < sizeof(request_buffer) - 1) ? p->tot_len : sizeof(request_buffer) - 1;
    pbuf_copy_partial(p, request_buffer, copy_len, 0);
    request_buffer[copy_len] = '\0';
    
    // Parse request type
    http_request_type_t request_type = http_parse_request_type(request_buffer);
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    
    if (request_type == HTTP_POST) {
        // Handle configuration update
        printf("HTTP Server: Processing configuration update\n");
        
        // Parse POST data and update configuration
        if (http_parse_post_data(request_buffer, copy_len)) {
            // Configuration updated successfully - redirect to main page
            http_send_redirect(conn, "/");
        } else {
            // Error updating configuration - show error page
            snprintf(response_buffer, sizeof(response_buffer),
                "HTTP/1.0 400 Bad Request\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "\r\n"
                "<html><body><h1>Configuration Error</h1>"
                "<p>Failed to update configuration. Please check your input values.</p>"
                "<p><a href=\"/\">Return to main page</a></p>"
                "</body></html>\r\n");
            http_send_response(conn, response_buffer, strlen(response_buffer));
        }
    } else if (strstr(request_buffer, "GET /config") != NULL) {
        // Show configuration page
        http_generate_config_page(response_buffer, sizeof(response_buffer));
        http_send_response(conn, response_buffer, strlen(response_buffer));
    } else {
        // Default - show device status page
        http_generate_device_page(response_buffer, sizeof(response_buffer));
        http_send_response(conn, response_buffer, strlen(response_buffer));
    }
    
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    
    // Close connection after sending response (HTTP/1.0 style)
    if (request_type != HTTP_POST) {  // Don't close immediately for POST redirect
        http_close_connection(conn);
    }
    
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
 * @brief Parse HTTP request type (GET/POST)
 */
static http_request_type_t http_parse_request_type(const char* request_data) {
    if (!request_data) {
        return HTTP_UNKNOWN;
    }
    
    if (strncmp(request_data, "GET", 3) == 0) {
        return HTTP_GET;
    } else if (strncmp(request_data, "POST", 4) == 0) {
        return HTTP_POST;
    }
    
    return HTTP_UNKNOWN;
}

/**
 * @brief Send HTTP redirect response
 */
static void http_send_redirect(http_connection_t* conn, const char* location) {
    static char redirect_buffer[256];
    int len = snprintf(redirect_buffer, sizeof(redirect_buffer),
        "HTTP/1.0 302 Found\r\n"
        "Location: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        location);
    
    http_send_response(conn, redirect_buffer, len);
    http_close_connection(conn);
}

/**
 * @brief Parse POST form data and update configuration
 */
static bool http_parse_post_data(const char* post_data, size_t data_len) {
    // Find start of form data (after double CRLF)
    const char* form_start = strstr(post_data, "\r\n\r\n");
    if (!form_start) {
        return false;
    }
    form_start += 4; // Skip past \r\n\r\n
    
    printf("HTTP: Parsing form data: %s\n", form_start);
    
    // Get current configuration
    shared_memory_layout_t* layout = shared_memory_get_layout();
    bool config_changed = false;
    
    printf("HTTP: Parsing configuration form data\n");
    
    // CRITICAL FIX: Reset ALL checkbox flags first 
    // (unchecked checkboxes don't appear in POST data)
    
    // Save previous states for change detection
    bool channel_enabled_before[4] = {
        layout->config.channels[0].enabled,
        layout->config.channels[1].enabled, 
        layout->config.channels[2].enabled,
        layout->config.channels[3].enabled
    };
    bool dhcp_enabled_before = layout->config.network.use_dhcp;
    
    // Reset all checkboxes to false first, then enable only checked ones
    layout->config.network.use_dhcp = false;  // CRITICAL FIX: Reset DHCP checkbox
    for (int ch = 1; ch <= 3; ch++) {
        layout->config.channels[ch].enabled = false;  // Reset UART channel checkboxes
    }
    
    // Parse form fields - simple key=value&key=value parsing
    char* form_copy = malloc(strlen(form_start) + 1);
    if (!form_copy) return false;
    strcpy(form_copy, form_start);
    
    char* token = strtok(form_copy, "&");
    while (token) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char* key = token;
            char* value = equals + 1;
            
            // Parse network settings
            if (strcmp(key, "static_ip") == 0) {
                // Parse IP address (format: 10.10.10.41)
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                    IP4_ADDR(&layout->config.network.static_ip, a, b, c, d);
                    config_changed = true;
                    printf("HTTP: Updated static IP to %d.%d.%d.%d\n", a, b, c, d);
                }
            } else if (strcmp(key, "use_dhcp") == 0 && strcmp(value, "1") == 0) {
                // Enable DHCP only if checkbox was checked (appears in POST data with value "1")
                layout->config.network.use_dhcp = true;
                printf("HTTP: DHCP ENABLED (checkbox checked)\n");
            } else if (strcmp(key, "mac_addr") == 0) {
                // Parse MAC address (format: 02:00:00:00:00:01)
                int m[6];
                if (sscanf(value, "%02x:%02x:%02x:%02x:%02x:%02x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
                    for (int i = 0; i < 6; i++) {
                        layout->config.network.mac_address[i] = (uint8_t)m[i];
                    }
                    config_changed = true;
                    printf("HTTP: Updated MAC address\n");
                }
            }
            
            // Parse UART channel settings (ch1_port, ch1_enabled, etc.)
            for (int ch = 1; ch <= 3; ch++) {
                char field_name[16];
                
                snprintf(field_name, sizeof(field_name), "ch%d_port", ch);
                if (strcmp(key, field_name) == 0) {
                    int port = atoi(value);
                    if (port >= 1024 && port <= 65535) {
                        layout->config.channels[ch].tcp_port = port;
                        config_changed = true;
                        printf("HTTP: Updated channel %d port to %d\n", ch, port);
                    }
                }
                
                // Enable channel if checkbox was checked (appears in POST data)
                snprintf(field_name, sizeof(field_name), "ch%d_enabled", ch);
                if (strcmp(key, field_name) == 0 && strcmp(value, "1") == 0) {
                    layout->config.channels[ch].enabled = true;
                    printf("HTTP: Channel %d ENABLED (checkbox checked)\n", ch);
                }
            }
        }
        token = strtok(NULL, "&");
    }
    
    // Check which settings changed state and mark config as changed
    
    // Check DHCP setting change
    if (dhcp_enabled_before != layout->config.network.use_dhcp) {
        config_changed = true;
        printf("HTTP: DHCP changed: %s -> %s\n",
               dhcp_enabled_before ? "ENABLED" : "DISABLED",
               layout->config.network.use_dhcp ? "ENABLED" : "DISABLED");
    }
    
    // Check channel changes
    for (int ch = 1; ch <= 3; ch++) {
        if (channel_enabled_before[ch] != layout->config.channels[ch].enabled) {
            config_changed = true;
            printf("HTTP: Channel %d changed: %s -> %s\n", ch,
                   channel_enabled_before[ch] ? "ENABLED" : "DISABLED",
                   layout->config.channels[ch].enabled ? "ENABLED" : "DISABLED");
        }
    }
    
    free(form_copy);
    
    // Save configuration to flash if changes were made
    if (config_changed) {
        layout->revision_counter++;
        layout->config_change_pending = true;  // Signal Core1 to apply changes
        
        bool save_result = flash_persistence_force_save_configuration();
        printf("HTTP: Configuration save result: %s\n", save_result ? "success" : "failed");
        printf("HTTP: ⚙️ Configuration change signaled to Core1 for runtime update\n");
        printf("HTTP: 🌐 Note: Network changes (IP/DHCP/MAC) will be applied immediately\n");
    }
    
    return config_changed;
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
    
    // DEBUG: Print channel configuration 
    DEBUG_ONLY({
        printf("HTTP: Device mode: %s, Data channels: %d\n", DEVICE_MODE_NAME, DEVICE_NUM_DATA_CHANNELS);
        printf("HTTP: Channel config - CH0:%s CH1:%s\n",
               layout->config.channels[CHANNEL_0].enabled ? "ON" : "OFF",
               layout->config.channels[CHANNEL_1].enabled ? "ON" : "OFF");
        printf("HTTP: Generating HTML response (buffer size: %d)\n", (int)buffer_size);
    });
    
    // Build HTML response with device-mode-aware channel list
    int html_len = snprintf(buffer, buffer_size,
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
        "        .nav-links { margin: 20px 0; text-align: center; }\n"
        "        .nav-links a { display: inline-block; margin: 0 10px; padding: 10px 20px; background-color: #3498db; color: white; text-decoration: none; border-radius: 4px; }\n"
        "        .nav-links a:hover { background-color: #2980b9; }\n"
        "        .mode-badge { display: inline-block; padding: 4px 12px; background-color: #9b59b6; color: white; border-radius: 12px; font-size: 12px; margin-left: 10px; }\n"
        "    </style>\n"
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
        DEVICE_MODE_NAME,
        (int)g_server_stats.uptime_seconds,
        ip_str,
        layout->config.channels[CHANNEL_1].tcp_port
    );
    
    // DEBUG: Check if HTML generation was successful
    DEBUG_ONLY({
        printf("HTTP: Generated HTML length: %d bytes (max: %d)\n", html_len, (int)buffer_size);
        if (html_len >= buffer_size) {
            printf("HTTP: WARNING - HTML truncated! Increase buffer size\n");
        }
        printf("HTTP: HTML contains all UART rows: %s\n", 
               strstr(buffer, "UART3") ? "YES" : "NO");
    });
}

/**
 * @brief Generate configuration HTML page
 */
static void http_generate_config_page(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Get current configuration
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    // Get current IP address for display
    simple_ip_addr_t ip_addr;
    bool has_ip = network_manager_get_ip_address(&ip_addr);
    char current_ip_str[16] = "Not Available";
    if (has_ip) {
        network_manager_ip_to_string(&ip_addr, current_ip_str);
    }
    
    // Format static IP for form
    uint32_t static_ip = layout->config.network.static_ip.addr;
    char static_ip_str[16];
    snprintf(static_ip_str, sizeof(static_ip_str), "%d.%d.%d.%d",
             (int)((static_ip >> 0) & 0xFF),
             (int)((static_ip >> 8) & 0xFF),
             (int)((static_ip >> 16) & 0xFF),
             (int)((static_ip >> 24) & 0xFF));
    
    // Format MAC address for form
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             layout->config.network.mac_address[0], layout->config.network.mac_address[1],
             layout->config.network.mac_address[2], layout->config.network.mac_address[3],
             layout->config.network.mac_address[4], layout->config.network.mac_address[5]);
    
    // Generate HTML response
    int html_len = snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>UART2ETH Configuration</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }\n"
        "        .container { background-color: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 800px; }\n"
        "        .header { color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 10px; margin-bottom: 30px; }\n"
        "        .section { margin-bottom: 30px; padding: 20px; border: 1px solid #ddd; border-radius: 4px; }\n"
        "        .section h3 { margin-top: 0; color: #2c3e50; }\n"
        "        .form-group { margin-bottom: 15px; }\n"
        "        .form-group label { display: block; margin-bottom: 5px; font-weight: bold; color: #34495e; }\n"
        "        .form-group input, .form-group select { width: 100%%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n"
        "        .checkbox-group { display: flex; align-items: center; }\n"
        "        .checkbox-group input[type=checkbox] { width: auto; margin-right: 10px; }\n"
        "        .button { background-color: #3498db; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }\n"
        "        .button:hover { background-color: #2980b9; }\n"
        "        .button-secondary { background-color: #95a5a6; }\n"
        "        .button-secondary:hover { background-color: #7f8c8d; }\n"
        "        .nav-links { margin: 20px 0; text-align: center; }\n"
        "        .nav-links a { display: inline-block; margin: 0 10px; padding: 10px 20px; background-color: #95a5a6; color: white; text-decoration: none; border-radius: 4px; }\n"
        "        .nav-links a:hover { background-color: #7f8c8d; }\n"
        "        .nav-links a.active { background-color: #3498db; }\n"
        "        .current-status { background-color: #ecf0f1; padding: 10px; border-radius: 4px; margin-bottom: 15px; }\n"
        "        .uart-row { display: flex; align-items: center; gap: 15px; margin-bottom: 15px; }\n"
        "        .uart-row > * { flex: 1; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <div class=\"header\">\n"
        "            <h1>UART2ETH Configuration</h1>\n"
        "            <p>Configure network settings and UART channels</p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"nav-links\">\n"
        "            <a href=\"/\">Status</a>\n"
        "            <a href=\"/config\" class=\"active\">Configuration</a>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"current-status\">\n"
        "            <strong>Current Status:</strong> IP Address: %s | MAC: %s | DHCP: %s\n"
        "        </div>\n"
        "        \n"
        "        <form method=\"POST\" action=\"/\">\n"
        "            \n"
        "            <div class=\"section\">\n"
        "                <h3>Network Configuration</h3>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"use_dhcp\" name=\"use_dhcp\" value=\"1\" %s>\n"
        "                        <label for=\"use_dhcp\">Use DHCP (automatic IP assignment)</label>\n"
        "                    </div>\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"static_ip\">Static IP Address (used when DHCP disabled):</label>\n"
        "                    <input type=\"text\" id=\"static_ip\" name=\"static_ip\" value=\"%s\" placeholder=\"10.10.10.41\">\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"mac_addr\">MAC Address:</label>\n"
        "                    <input type=\"text\" id=\"mac_addr\" name=\"mac_addr\" value=\"%s\" placeholder=\"02:00:00:00:00:01\">\n"
        "                </div>\n"
        "            </div>\n"
        "            \n"
        "            <div class=\"section\">\n"
        "                <h3>UART Channel Configuration</h3>\n"
        "                <p><em>UART0 is reserved for debug output and cannot be configured.</em></p>\n"
        "                \n"
        "                <div class=\"uart-row\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"ch1_enabled\" name=\"ch1_enabled\" value=\"1\" %s>\n"
        "                        <label for=\"ch1_enabled\">UART1 Enabled</label>\n"
        "                    </div>\n"
        "                    <div class=\"form-group\" style=\"margin-bottom: 0;\">\n"
        "                        <label for=\"ch1_port\">TCP Port:</label>\n"
        "                        <input type=\"number\" id=\"ch1_port\" name=\"ch1_port\" value=\"%d\" min=\"1024\" max=\"65535\">\n"
        "                    </div>\n"
        "                    <div style=\"flex: 0.5; font-size: 14px; color: #7f8c8d;\">GP4/GP5</div>\n"
        "                </div>\n"
#if DEVICE_CHANNEL_2_ENABLED
        "                \n"
        "                <div class=\"uart-row\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"ch2_enabled\" name=\"ch2_enabled\" value=\"1\" %s>\n"
        "                        <label for=\"ch2_enabled\">UART2 Enabled</label>\n"
        "                    </div>\n"
        "                    <div class=\"form-group\" style=\"margin-bottom: 0;\">\n"
        "                        <label for=\"ch2_port\">TCP Port:</label>\n"
        "                        <input type=\"number\" id=\"ch2_port\" name=\"ch2_port\" value=\"%d\" min=\"1024\" max=\"65535\">\n"
        "                    </div>\n"
        "                    <div style=\"flex: 0.5; font-size: 14px; color: #7f8c8d;\">GP14/GP15</div>\n"
        "                </div>\n"
#endif
#if DEVICE_CHANNEL_3_ENABLED
        "                \n"
        "                <div class=\"uart-row\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"ch3_enabled\" name=\"ch3_enabled\" value=\"1\" %s>\n"
        "                        <label for=\"ch3_enabled\">UART3 Enabled</label>\n"
        "                    </div>\n"
        "                    <div class=\"form-group\" style=\"margin-bottom: 0;\">\n"
        "                        <label for=\"ch3_port\">TCP Port:</label>\n"
        "                        <input type=\"number\" id=\"ch3_port\" name=\"ch3_port\" value=\"%d\" min=\"1024\" max=\"65535\">\n"
        "                    </div>\n"
        "                    <div style=\"flex: 0.5; font-size: 14px; color: #7f8c8d;\">GP22/GP23</div>\n"
        "                </div>\n"
#endif
        "            </div>\n"
        "            \n"
        "            <div class=\"section\">\n"
        "                <h3>Save Configuration</h3>\n"
        "                <p><strong>Important:</strong> Configuration changes are saved to flash memory and will persist across firmware updates. Network changes may require a device restart to take full effect.</p>\n"
        "                \n"
        "                <button type=\"submit\" class=\"button\">Save Configuration</button>\n"
        "                <a href=\"/\" class=\"button button-secondary\" style=\"text-decoration: none; display: inline-block; margin-left: 10px;\">Cancel</a>\n"
        "            </div>\n"
        "            \n"
        "        </form>\n"
        "    </div>\n"
        "</body>\n"
        "</html>\r\n",
        current_ip_str, mac_str, layout->config.network.use_dhcp ? "Enabled" : "Disabled",
        layout->config.network.use_dhcp ? "checked" : "",
        static_ip_str,
        mac_str,
        layout->config.channels[CHANNEL_1].enabled ? "checked" : "",
        layout->config.channels[CHANNEL_1].tcp_port
#if DEVICE_CHANNEL_2_ENABLED
        ,layout->config.channels[CHANNEL_2].enabled ? "checked" : "",
        layout->config.channels[CHANNEL_2].tcp_port
#endif
#if DEVICE_CHANNEL_3_ENABLED
        ,layout->config.channels[CHANNEL_3].enabled ? "checked" : "",
        layout->config.channels[CHANNEL_3].tcp_port
#endif
    );
    
    // DEBUG: Check if HTML generation was successful
    printf("HTTP: Generated config page HTML length: %d bytes (max: %d)\n", html_len, (int)buffer_size);
    if (html_len >= buffer_size) {
        printf("HTTP: ERROR - Config HTML truncated! Need at least %d bytes\n", html_len);
    } else {
        printf("HTTP: Config HTML generation successful - %d bytes remaining\n", 
               (int)buffer_size - html_len);
    }
}
