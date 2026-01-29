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
#include "network/http_auth.h"
#include "network/http_multipart.h"
#include "network/network_manager.h"
#include "shared_memory.h"
#include "device_mode.h"
#include "log_manager.h"
#include "update/update_manager.h"
#include "state_machine/state_machine.h"
#include "debug.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// HTML page templates
#include "html/html_styles.h"
#include "html/html_page_update.h"

// HTTP page generation modules (ADR-018: HTTP Server Modularization)
#include "network/http_pages/page_device.h"
#include "network/http_pages/page_config.h"
#include "network/http_pages/page_update.h"
#include "network/http_pages/page_styles.h"

#ifdef FACTORY_INTERNAL_VERSION
#include "factory_defaults.h"
#include "network/http_pages/page_factory.h"
#endif

// HTTP server configuration
#define HTTP_SERVER_PORT 80
#define HTTP_SERVER_MAX_CONNECTIONS 2
#define HTTP_RESPONSE_BUFFER_SIZE 8192  // Increased to 8KB for configuration page

// HTTP server state
static struct tcp_pcb* g_http_server_pcb = NULL;
static http_server_status_t g_server_status = HTTP_SERVER_STATUS_UNINITIALIZED;
http_server_stats_t g_server_stats; // Made non-static for page module access (ADR-018)
static absolute_time_t g_server_start_time;

// Firmware upload session state
// Documentation Reference: ADR-016 - Firmware Update Web Interface, ADR-017 - Update Module
typedef enum {
    UPLOAD_STATE_IDLE,
    UPLOAD_STATE_RECEIVING,
    UPLOAD_STATE_COMPLETE,
    UPLOAD_STATE_ERROR
} upload_state_t;

typedef struct {
    upload_state_t state;
    uint32_t total_expected_size;
    uint32_t bytes_received;
    uint32_t last_progress_report;
} upload_session_t;

static upload_session_t g_upload_session = {
    .state = UPLOAD_STATE_IDLE,
    .total_expected_size = 0,
    .bytes_received = 0,
    .last_progress_report = 0
};

// Connection tracking with upload state
typedef struct http_connection {
    struct tcp_pcb* pcb;
    bool active;
    uint32_t start_time_ms;
    
    // Upload state for this connection (multipart/form-data handling)
    multipart_context_t multipart_ctx;   // Multipart upload context
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
// Page generation functions now in http_pages/ modules (ADR-018)
static http_request_type_t http_parse_request_type(const char* request_data);
static bool http_parse_post_data(const char* post_data, size_t data_len);
static bool http_handle_password_change(const char* post_data, size_t data_len);
static bool http_handle_reboot_request(const char* post_data, size_t data_len);
static void http_send_redirect(http_connection_t* conn, const char* location);

// HTTP Basic Authentication functions now in http_auth module (ADR-018 Phase 2)
// Factory page functions now in http_pages/page_factory.c (ADR-018)

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

// ============================================================================
// Upload Session Management (Public API)
// ============================================================================

/**
 * @brief Start a firmware upload session
 * 
 * Initializes the upload session and calls update_manager to prepare
 * for receiving firmware data.
 * 
 * @param expected_size Expected size of firmware file (bytes)
 * @return true if session started successfully, false otherwise
 * 
 * Documentation Reference: ADR-016, ADR-017
 */
bool http_upload_session_start(uint32_t expected_size) {
    if (g_upload_session.state == UPLOAD_STATE_RECEIVING) {
        printf("HTTP Upload: Session already in progress\n");
        return false;
    }
    
    printf("HTTP Upload: Starting session, expected size: %u bytes\n", expected_size);
    
    // Initialize update manager for this upload
    if (!update_start_upload(expected_size)) {
        printf("HTTP Upload: Failed to start update manager session\n");
        g_upload_session.state = UPLOAD_STATE_ERROR;
        return false;
    }
    
    // Initialize session state
    g_upload_session.state = UPLOAD_STATE_RECEIVING;
    g_upload_session.total_expected_size = expected_size;
    g_upload_session.bytes_received = 0;
    g_upload_session.last_progress_report = 0;
    
    return true;
}

/**
 * @brief Feed a chunk of firmware data to the upload session
 * 
 * @param bytes_received Number of bytes in this chunk
 */
void http_upload_receive_chunk(uint32_t bytes_received) {
    if (g_upload_session.state != UPLOAD_STATE_RECEIVING) {
        return;
    }
    
    g_upload_session.bytes_received += bytes_received;
    
    // Progress reporting every 64KB
    if ((g_upload_session.bytes_received - g_upload_session.last_progress_report) >= (64 * 1024)) {
        printf("HTTP Upload: %u / %u KB (%u%%)\n",
               g_upload_session.bytes_received / 1024,
               g_upload_session.total_expected_size / 1024,
               (g_upload_session.bytes_received * 100) / g_upload_session.total_expected_size);
        g_upload_session.last_progress_report = g_upload_session.bytes_received;
    }
}

/**
 * @brief Reset/abort the current upload session
 */
void http_upload_session_reset(void) {
    if (g_upload_session.state == UPLOAD_STATE_RECEIVING) {
        printf("HTTP Upload: Aborting session\n");
        update_abort_upload();
    }
    
    g_upload_session.state = UPLOAD_STATE_IDLE;
    g_upload_session.total_expected_size = 0;
    g_upload_session.bytes_received = 0;
    g_upload_session.last_progress_report = 0;
}

/**
 * @brief Get upload session progress
 * 
 * @param bytes_received Output: bytes received so far
 * @param total_bytes Output: total expected bytes
 */
void http_upload_get_progress(uint32_t* bytes_received, uint32_t* total_bytes) {
    if (bytes_received) {
        *bytes_received = g_upload_session.bytes_received;
    }
    if (total_bytes) {
        *total_bytes = g_upload_session.total_expected_size;
    }
}

// ============================================================================
// Firmware Upload Callback (bridges multipart → update_manager)
// ============================================================================

/**
 * @brief Callback for multipart handler to feed firmware data chunks
 * 
 * This is called by the multipart parser for each chunk of actual file data.
 * It forwards the data to update_manager for writing to flash.
 * 
 * @param data Pointer to firmware data chunk
 * @param size Size of chunk in bytes
 * @param finished True if this is the final chunk
 * @param user_data User context (unused)
 * @return true if chunk was processed successfully, false on error
 * 
 * Documentation Reference: ADR-016, ADR-017
 */
static bool http_firmware_upload_callback(const uint8_t* data, uint32_t size, bool finished, void* user_data) {
    (void)user_data;  // Unused
    
    if (!data || size == 0) {
        return false;
    }
    
    // Feed chunk to update manager (cast away const - update_manager modifies workarea, not data)
    if (!update_write_block((uint8_t*)data, size, finished)) {
        printf("HTTP Upload: Failed to write %u bytes to update manager\n", size);
        g_upload_session.state = UPLOAD_STATE_ERROR;
        return false;
    }
    
    // Update session progress
    http_upload_receive_chunk(size);
    
    // Handle completion
    if (finished) {
        printf("HTTP Upload: Firmware upload complete (%u bytes)\n", g_upload_session.bytes_received);
        g_upload_session.state = UPLOAD_STATE_COMPLETE;
    }
    
    return true;
}

// ============================================================================
// Private function implementations
// ============================================================================

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
    
    // Check if this connection is in the middle of a firmware upload
    if (conn->multipart_ctx.active && !multipart_is_complete(&conn->multipart_ctx)) {
        printf("HTTP Upload: Received data packet for ongoing upload\n");
        
        // Copy data to buffer for multipart processing
        static uint8_t upload_buffer[2048];
        size_t copy_len = (p->tot_len < sizeof(upload_buffer)) ? p->tot_len : sizeof(upload_buffer);
        pbuf_copy_partial(p, upload_buffer, copy_len, 0);
        
        // Process this chunk through multipart handler
        if (!multipart_process_chunk(&conn->multipart_ctx, upload_buffer, copy_len,
                                    http_firmware_upload_callback, NULL)) {
            printf("HTTP Upload: Failed to process data chunk\n");
            http_upload_session_reset();
            multipart_reset_context(&conn->multipart_ctx);
            
            static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
            http_generate_update_page(response_buffer, sizeof(response_buffer),
                "Error: Upload failed during data transfer.");
            http_send_response(conn, response_buffer, strlen(response_buffer));
            http_close_connection(conn);
            
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            return ERR_OK;
        }
        
        // Check if upload is now complete
        if (multipart_is_complete(&conn->multipart_ctx)) {
            printf("HTTP Upload: Upload completed successfully\n");
            multipart_reset_context(&conn->multipart_ctx);
            
            static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
            http_generate_update_page(response_buffer, sizeof(response_buffer),
                "Firmware uploaded successfully! Device will reboot to apply the update.");
            http_send_response(conn, response_buffer, strlen(response_buffer));
            http_close_connection(conn);
            
            // TODO: Schedule reboot via state machine
        }
        
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }
    
    // Normal HTTP request processing (not an upload continuation)
    // Copy request data to null-terminated string for parsing
    static char request_buffer[1024];
    size_t copy_len = (p->tot_len < sizeof(request_buffer) - 1) ? p->tot_len : sizeof(request_buffer) - 1;
    pbuf_copy_partial(p, request_buffer, copy_len, 0);
    request_buffer[copy_len] = '\0';
    
    // Parse request type
    http_request_type_t request_type = http_parse_request_type(request_buffer);
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    
    // Check HTTP Basic Authentication for ALL requests
    // Get password from shared memory
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        printf("HTTP Auth: Failed to get shared memory layout\n");
        http_send_auth_required(conn);
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }
    
    // Verify authentication
    if (!http_check_authentication(request_buffer, layout->config.admin_password)) {
        printf("HTTP Auth: Authentication failed, sending 401\n");
        http_send_auth_required(conn);
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }
    
    printf("HTTP Auth: Request authenticated successfully\n");
    
    if (request_type == HTTP_POST) {
        // Determine which form was submitted based on the action URL
        if (strstr(request_buffer, "POST /change_password") != NULL) {
            // Handle password change
            printf("HTTP Server: Processing password change\n");
            
            if (http_handle_password_change(request_buffer, copy_len)) {
                // Password changed successfully - redirect to config page
                http_send_redirect(conn, "/config");
            } else {
                // Error changing password - show error page
                snprintf(response_buffer, sizeof(response_buffer),
                    "HTTP/1.0 400 Bad Request\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "<html><body><h1>Password Change Error</h1>"
                    "<p>Failed to change password. Please check your current password and try again.</p>"
                    "<p><a href=\"/config\">Return to configuration</a></p>"
                    "</body></html>\r\n");
                http_send_response(conn, response_buffer, strlen(response_buffer));
            }
        }
        #ifdef FACTORY_INTERNAL_VERSION
        else if (strstr(request_buffer, "POST /factory") != NULL) {
            // Handle factory defaults write
            printf("HTTP Server: Processing factory defaults write\n");
            
            char error_msg[128] = {0};
            char success_msg[128] = {0};
            
            http_parse_factory_post_data(request_buffer, copy_len, error_msg, sizeof(error_msg),success_msg,sizeof(success_msg) );
            //regardless of error or not, send back same page with message
            http_generate_factory_page(response_buffer, sizeof(response_buffer), error_msg, strlen(error_msg), success_msg , strlen(success_msg));
            http_send_response(conn, response_buffer, strlen(response_buffer));
            
        }
        #endif
        else if (strstr(request_buffer, "POST /reboot") != NULL) {
            // Handle reboot request
            printf("HTTP Server: Processing reboot request\n");
            
            if (http_handle_reboot_request(request_buffer, copy_len)) {
                // Reboot initiated - show confirmation page
                http_generate_update_page(response_buffer, sizeof(response_buffer), 
                    "Reboot initiated. Device will restart in a few seconds. Please wait and then refresh this page.");
                http_send_response(conn, response_buffer, strlen(response_buffer));
            } else {
                // Reboot failed
                http_generate_update_page(response_buffer, sizeof(response_buffer),
                    "Error: Failed to initiate reboot.");
                http_send_response(conn, response_buffer, strlen(response_buffer));
            }
        }
        else if (strstr(request_buffer, "POST /update") != NULL) {
            // Handle firmware upload
            printf("HTTP Server: Processing firmware upload\n");
            
            // Initialize multipart context from request
            if (!multipart_init_context(&conn->multipart_ctx, request_buffer, copy_len)) {
                printf("HTTP Upload: Failed to initialize multipart context\n");
                http_generate_update_page(response_buffer, sizeof(response_buffer),
                    "Error: Failed to process upload. Invalid multipart data.");
                http_send_response(conn, response_buffer, strlen(response_buffer));
                return ERR_OK;
            }
            
            // Start upload session in update_manager
            if (!http_upload_session_start(conn->multipart_ctx.file_size)) {
                printf("HTTP Upload: Failed to start upload session\n");
                multipart_reset_context(&conn->multipart_ctx);
                http_generate_update_page(response_buffer, sizeof(response_buffer),
                    "Error: Failed to start firmware upload. Update manager not ready.");
                http_send_response(conn, response_buffer, strlen(response_buffer));
                return ERR_OK;
            }
            
            // Process the initial chunk (HTTP headers + some data might be in this pbuf)
            if (!multipart_process_chunk(&conn->multipart_ctx, (const uint8_t*)request_buffer, copy_len,
                                        http_firmware_upload_callback, NULL)) {
                printf("HTTP Upload: Failed to process initial chunk\n");
                http_upload_session_reset();
                multipart_reset_context(&conn->multipart_ctx);
                http_generate_update_page(response_buffer, sizeof(response_buffer),
                    "Error: Failed to process upload data.");
                http_send_response(conn, response_buffer, strlen(response_buffer));
                return ERR_OK;
            }
            
            // Check if upload is already complete (small file in single packet)
            if (multipart_is_complete(&conn->multipart_ctx)) {
                printf("HTTP Upload: Upload complete in single packet\n");
                multipart_reset_context(&conn->multipart_ctx);
                http_generate_update_page(response_buffer, sizeof(response_buffer),
                    "Firmware uploaded successfully! Device will reboot to apply the update.");
                http_send_response(conn, response_buffer, strlen(response_buffer));
                
                // Trigger reboot after short delay (allow response to be sent)
                // TODO: Schedule reboot via state machine
            } else {
                // Multi-packet upload - connection will stay open for more data
                printf("HTTP Upload: Multi-packet upload in progress, waiting for more data...\n");
                // Don't close connection or send response yet - more data coming
            }
        }
        else {
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
        }
    }
    else if (strstr(request_buffer, "GET /styles.css") != NULL) {
        // Serve CSS stylesheet
        http_generate_stylesheet(response_buffer, sizeof(response_buffer));
        http_send_response(conn, response_buffer, strlen(response_buffer));
    }
    #ifdef FACTORY_INTERNAL_VERSION
    else if (strstr(request_buffer, "GET /factory") != NULL) {
        // Show factory defaults configuration page
        http_generate_factory_page(response_buffer, sizeof(response_buffer), NULL, 0, NULL, 0);
        http_send_response(conn, response_buffer, strlen(response_buffer));
    }
    #endif
    else if (strstr(request_buffer, "GET /config") != NULL) {
        // Show configuration page
        http_generate_config_page(response_buffer, sizeof(response_buffer));
        http_send_response(conn, response_buffer, strlen(response_buffer));
    }
    else if (strstr(request_buffer, "GET /update") != NULL) {
        // Show firmware update page
        http_generate_update_page(response_buffer, sizeof(response_buffer), NULL);
        http_send_response(conn, response_buffer, strlen(response_buffer));
    }
    else {
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
 * 
 * Made non-static for use by http_auth module (ADR-018 Phase 2)
 */
void http_send_response(http_connection_t* conn, const char* response, size_t response_len) {
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

// Authentication functions moved to http_auth module (ADR-018 Phase 2)

/**
 * @brief Validate password change request
 * 
 * Validates password change according to ADR-016 rules:
 * - Current password must match stored password
 * - New password must be 8-31 characters
 * - New password must match confirmation
 * - All fields must be non-empty
 * 
 * @param current_pwd Current password from form
 * @param new_pwd New password from form
 * @param confirm_pwd Confirmation password from form
 * @param stored_pwd Stored password to validate against
 * @return Validation result code
 * 
 * Reference: ADR-016 HTTP Basic Authentication - Password Management
 */
password_change_result_t http_validate_password_change(
    const char* current_pwd,
    const char* new_pwd,
    const char* confirm_pwd,
    const char* stored_pwd
) {
    // Check for NULL pointers
    if (!current_pwd || !new_pwd || !confirm_pwd || !stored_pwd) {
        return PWD_CHANGE_EMPTY_FIELD;
    }
    
    // Check for empty fields
    if (strlen(current_pwd) == 0 || strlen(new_pwd) == 0 || strlen(confirm_pwd) == 0) {
        printf("HTTP Password Change: Empty field detected\n");
        return PWD_CHANGE_EMPTY_FIELD;
    }
    
    // Validate current password matches stored password
    if (strcmp(current_pwd, stored_pwd) != 0) {
        printf("HTTP Password Change: Current password incorrect\n");
        return PWD_CHANGE_CURRENT_WRONG;
    }
    
    // Validate new password length (minimum 8 characters)
    size_t new_pwd_len = strlen(new_pwd);
    if (new_pwd_len < 8) {
        printf("HTTP Password Change: New password too short (%zu chars, need 8)\n", new_pwd_len);
        return PWD_CHANGE_TOO_SHORT;
    }
    
    // Validate new password length (maximum 31 characters)
    if (new_pwd_len > 31) {
        printf("HTTP Password Change: New password too long (%zu chars, max 31)\n", new_pwd_len);
        return PWD_CHANGE_TOO_LONG;
    }
    
    // Validate new password matches confirmation
    if (strcmp(new_pwd, confirm_pwd) != 0) {
        printf("HTTP Password Change: Password confirmation mismatch\n");
        return PWD_CHANGE_NO_MATCH;
    }
    
    printf("HTTP Password Change: Validation successful\n");
    return PWD_CHANGE_OK;
}

/**
 * @brief Handle password change request
 * 
 * Parses password change POST data, validates the request, and updates
 * the stored password if validation passes.
 * 
 * @param post_data Raw POST request data
 * @param data_len Length of POST data
 * @return true if password was changed successfully, false otherwise
 * 
 * Reference: ADR-016 HTTP Basic Authentication - Password Management
 */
static bool http_handle_password_change(const char* post_data, size_t data_len) {
    // Find start of form data (after double CRLF)
    const char* form_start = strstr(post_data, "\r\n\r\n");
    if (!form_start) {
        printf("HTTP Password Change: No form data found\n");
        return false;
    }
    form_start += 4; // Skip past \r\n\r\n
    
    printf("HTTP: Parsing password change form data\n");
    
    // Extract password fields from form data
    char current_password[64] = {0};
    char new_password[64] = {0};
    char confirm_password[64] = {0};
    
    // Parse form fields - simple key=value&key=value parsing
    char* form_copy = malloc(strlen(form_start) + 1);
    if (!form_copy) {
        printf("HTTP Password Change: Memory allocation failed\n");
        return false;
    }
    strcpy(form_copy, form_start);
    
    char* token = strtok(form_copy, "&");
    while (token) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char* key = token;
            char* value = equals + 1;
            
            // Decode URL-encoded values (basic handling of %20, etc.)
            // For simplicity, just copy directly for now
            if (strcmp(key, "current_password") == 0) {
                strncpy(current_password, value, sizeof(current_password) - 1);
            } else if (strcmp(key, "new_password") == 0) {
                strncpy(new_password, value, sizeof(new_password) - 1);
            } else if (strcmp(key, "confirm_password") == 0) {
                strncpy(confirm_password, value, sizeof(confirm_password) - 1);
            }
        }
        token = strtok(NULL, "&");
    }
    
    free(form_copy);
    
    // Get current configuration
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    // Validate password change
    password_change_result_t result = http_validate_password_change(
        current_password,
        new_password,
        confirm_password,
        layout->config.admin_password
    );
    
    if (result != PWD_CHANGE_OK) {
        printf("HTTP Password Change: Validation failed with code %d\n", result);
        return false;
    }
    
    // Password validation successful - update stored password
    strncpy(layout->config.admin_password, new_password, sizeof(layout->config.admin_password) - 1);
    layout->config.admin_password[sizeof(layout->config.admin_password) - 1] = '\0';
    
    // Increment revision counter and save to flash
    layout->revision_counter++;
    bool save_result = flash_persistence_force_save_configuration();
    
    printf("HTTP Password Change: Password updated successfully, flash save: %s\n", 
           save_result ? "success" : "failed");
    
    return save_result;
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
    bool channel_enabled_before[5] = {
        layout->config.channels[0].enabled,
        layout->config.channels[1].enabled, 
        layout->config.channels[2].enabled,
        layout->config.channels[3].enabled,
        layout->config.channels[4].enabled
    };
    bool dhcp_enabled_before = layout->config.network.use_dhcp;
    
    // Reset all checkboxes to false first, then enable only checked ones
    layout->config.network.use_dhcp = false;  // CRITICAL FIX: Reset DHCP checkbox
    for (int ch = 1; ch <= 4; ch++) {
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
            } /*else if (strcmp(key, "mac_addr") == 0) {
                // Parse MAC address (format: 02:00:00:00:00:01)
                int m[6];
                if (sscanf(value, "%02x:%02x:%02x:%02x:%02x:%02x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
                    for (int i = 0; i < 6; i++) {
                        layout->config.network.mac_address[i] = (uint8_t)m[i];
                    }
                    config_changed = true;
                    printf("HTTP: Updated MAC address\n");
                }
            }*/
            
            // Parse UART channel settings (ch1_port, ch1_enabled, etc.)
            for (int ch = 1; ch <= 4; ch++) {
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
    for (int ch = 1; ch <= 4; ch++) {
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
 * @brief Handle reboot request
 * 
 * Triggers a system reboot via watchdog reset.
 * 
 * @param post_data POST request data
 * @param data_len Length of POST data
 * @return true if reboot initiated successfully
 */
static bool http_handle_reboot_request(const char* post_data, size_t data_len) {
    (void)post_data;  // Unused
    (void)data_len;   // Unused
    
    printf("HTTP Server: Initiating reboot via watchdog\n");
    
    // Trigger watchdog reset with 1ms timeout
    watchdog_reboot(0, 0, 1);
    
    return true;
}
