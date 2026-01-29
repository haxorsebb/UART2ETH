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
#include "network/http_forms.h"
#include "network/http_router.h"
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

// Forward declarations
static err_t http_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
static err_t http_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
static void http_connection_error_callback(void* arg, err_t err);
static err_t http_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len);
static void http_close_connection(http_connection_t* conn);
// Page generation functions now in http_pages/ modules (ADR-018)
// Request routing functions now in http_router module (ADR-018 Phase 4)
static bool http_handle_reboot_request(const char* post_data, size_t data_len);
static void http_send_redirect(http_connection_t* conn, const char* location);
// Route handler forward declarations (ADR-018 Phase 4)
static void http_handle_root_get(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_config_get(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_update_get(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_styles_get(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
#ifdef FACTORY_INTERNAL_VERSION
static void http_handle_factory_get(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_factory_post(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
#endif
static void http_handle_config_post(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_password_post(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_reboot_post(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_update_post(http_connection_t* conn, const char* request_buffer, size_t buffer_len);
static void http_handle_404(http_connection_t* conn);

// HTTP Basic Authentication functions now in http_auth module (ADR-018 Phase 2)
// HTTP Form handling functions now in http_forms module (ADR-018 Phase 3)
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
    
    // Initialize HTTP router and register routes (ADR-018 Phase 4)
    http_router_init();
    
    // Register GET routes
    http_router_register_route("/", HTTP_METHOD_GET, http_handle_root_get);
    http_router_register_route("/config", HTTP_METHOD_GET, http_handle_config_get);
    http_router_register_route("/update", HTTP_METHOD_GET, http_handle_update_get);
    http_router_register_route("/styles.css", HTTP_METHOD_GET, http_handle_styles_get);
    
    #ifdef FACTORY_INTERNAL_VERSION
    http_router_register_route("/factory", HTTP_METHOD_GET, http_handle_factory_get);
    http_router_register_route("/factory", HTTP_METHOD_POST, http_handle_factory_post);
    #endif
    
    // Register POST routes
    http_router_register_route("/", HTTP_METHOD_POST, http_handle_config_post);
    http_router_register_route("/change_password", HTTP_METHOD_POST, http_handle_password_post);
    http_router_register_route("/reboot", HTTP_METHOD_POST, http_handle_reboot_post);
    http_router_register_route("/update", HTTP_METHOD_POST, http_handle_update_post);
    
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

// ============================================================================
// HTTP Route Handlers (ADR-018 Phase 4)
// ============================================================================

/**
 * @brief Handler for GET /
 * Serves the device status page
 */
static void http_handle_root_get(http_connection_t* conn, 
                                  const char* request_buffer, 
                                  size_t buffer_len) {
    (void)request_buffer;  // Unused
    (void)buffer_len;      // Unused
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    http_generate_device_page(response_buffer, sizeof(response_buffer));
    http_send_response(conn, response_buffer, strlen(response_buffer));
}

/**
 * @brief Handler for GET /config
 * Serves the configuration page
 */
static void http_handle_config_get(http_connection_t* conn, 
                                    const char* request_buffer, 
                                    size_t buffer_len) {
    (void)request_buffer;  // Unused
    (void)buffer_len;      // Unused
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    http_generate_config_page(response_buffer, sizeof(response_buffer));
    http_send_response(conn, response_buffer, strlen(response_buffer));
}

/**
 * @brief Handler for GET /update
 * Serves the firmware update page
 */
static void http_handle_update_get(http_connection_t* conn, 
                                    const char* request_buffer, 
                                    size_t buffer_len) {
    (void)request_buffer;  // Unused
    (void)buffer_len;      // Unused
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    http_generate_update_page(response_buffer, sizeof(response_buffer), NULL);
    http_send_response(conn, response_buffer, strlen(response_buffer));
}

/**
 * @brief Handler for GET /styles.css
 * Serves the CSS stylesheet
 */
static void http_handle_styles_get(http_connection_t* conn, 
                                    const char* request_buffer, 
                                    size_t buffer_len) {
    (void)request_buffer;  // Unused
    (void)buffer_len;      // Unused
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    http_generate_stylesheet(response_buffer, sizeof(response_buffer));
    http_send_response(conn, response_buffer, strlen(response_buffer));
}

#ifdef FACTORY_INTERNAL_VERSION
/**
 * @brief Handler for GET /factory
 * Serves the factory defaults configuration page
 */
static void http_handle_factory_get(http_connection_t* conn, 
                                     const char* request_buffer, 
                                     size_t buffer_len) {
    (void)request_buffer;  // Unused
    (void)buffer_len;      // Unused
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    http_generate_factory_page(response_buffer, sizeof(response_buffer), NULL, 0, NULL, 0);
    http_send_response(conn, response_buffer, strlen(response_buffer));
}

/**
 * @brief Handler for POST /factory
 * Processes factory defaults write request
 */
static void http_handle_factory_post(http_connection_t* conn, 
                                      const char* request_buffer, 
                                      size_t buffer_len) {
    printf("HTTP Server: Processing factory defaults write\n");
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    char error_msg[128] = {0};
    char success_msg[128] = {0};
    
    http_parse_factory_post_data(request_buffer, buffer_len, 
                                  error_msg, sizeof(error_msg),
                                  success_msg, sizeof(success_msg));
    
    // Regardless of error or not, send back same page with message
    http_generate_factory_page(response_buffer, sizeof(response_buffer), 
                                error_msg, strlen(error_msg), 
                                success_msg, strlen(success_msg));
    http_send_response(conn, response_buffer, strlen(response_buffer));
}
#endif

/**
 * @brief Handler for POST /
 * Processes configuration update request
 */
static void http_handle_config_post(http_connection_t* conn, 
                                     const char* request_buffer, 
                                     size_t buffer_len) {
    printf("HTTP Server: Processing configuration update\n");
    
    if (http_parse_post_data(request_buffer, buffer_len)) {
        // Configuration updated successfully - redirect to main page
        http_send_redirect(conn, "/");
    } else {
        // Error updating configuration - show error page
        static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
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

/**
 * @brief Handler for POST /change_password
 * Processes password change request
 */
static void http_handle_password_post(http_connection_t* conn, 
                                       const char* request_buffer, 
                                       size_t buffer_len) {
    printf("HTTP Server: Processing password change\n");
    
    if (http_handle_password_change(request_buffer, buffer_len)) {
        // Password changed successfully - redirect to config page
        http_send_redirect(conn, "/config");
    } else {
        // Error changing password - show error page
        static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
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

/**
 * @brief Handler for POST /reboot
 * Processes reboot request
 */
static void http_handle_reboot_post(http_connection_t* conn, 
                                     const char* request_buffer, 
                                     size_t buffer_len) {
    printf("HTTP Server: Processing reboot request\n");
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    if (http_handle_reboot_request(request_buffer, buffer_len)) {
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

/**
 * @brief Handler for POST /update
 * Processes firmware upload request
 */
static void http_handle_update_post(http_connection_t* conn, 
                                     const char* request_buffer, 
                                     size_t buffer_len) {
    printf("HTTP Server: Processing firmware upload\n");
    
    static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    
    // Initialize multipart context from request
    if (!multipart_init_context(&conn->multipart_ctx, request_buffer, buffer_len)) {
        printf("HTTP Upload: Failed to initialize multipart context\n");
        http_generate_update_page(response_buffer, sizeof(response_buffer),
            "Error: Failed to process upload. Invalid multipart data.");
        http_send_response(conn, response_buffer, strlen(response_buffer));
        return;
    }
    
    // Start upload session in update_manager
    if (!http_upload_session_start(conn->multipart_ctx.file_size)) {
        printf("HTTP Upload: Failed to start upload session\n");
        multipart_reset_context(&conn->multipart_ctx);
        http_generate_update_page(response_buffer, sizeof(response_buffer),
            "Error: Failed to start firmware upload. Update manager not ready.");
        http_send_response(conn, response_buffer, strlen(response_buffer));
        return;
    }
    
    // Process the initial chunk (HTTP headers + some data might be in this pbuf)
    if (!multipart_process_chunk(&conn->multipart_ctx, (const uint8_t*)request_buffer, buffer_len,
                                http_firmware_upload_callback, NULL)) {
        printf("HTTP Upload: Failed to process initial chunk\n");
        http_upload_session_reset();
        multipart_reset_context(&conn->multipart_ctx);
        http_generate_update_page(response_buffer, sizeof(response_buffer),
            "Error: Failed to process upload data.");
        http_send_response(conn, response_buffer, strlen(response_buffer));
        return;
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

/**
 * @brief Handler for 404 Not Found
 * Serves a humorous 404 error page
 */
static void http_handle_404(http_connection_t* conn) {
    static char response_buffer[1024];
    int len = snprintf(response_buffer, sizeof(response_buffer),
        "HTTP/1.0 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>404 - Page Not Found</title></head>\n"
        "<body style='font-family: monospace; text-align: center; padding-top: 50px;'>\n"
        "<h1>404 - Page Not Found</h1>\n"
        "<pre>\n"
        "    _____ \n"
        "   /     \\\n"
        "  | () () |\n"
        "   \\  ^  /\n"
        "    |||||\n"
        "    |||||\n"
        "</pre>\n"
        "<p><b>Oops! This page got lost in the serial buffer.</b></p>\n"
        "<p>The page you're looking for wandered off through UART%d<br>\n"
        "and hasn't been seen since. It's probably stuck in a TCP timeout somewhere.</p>\n"
        "<p><i>Error Code: 0x%X (ENOPAGEFOUND)</i></p>\n"
        "<p><a href='/'>← Return to Home</a> | <a href='/config'>Configuration</a> | <a href='/update'>Firmware Update</a></p>\n"
        "</body>\n"
        "</html>\n",
        (int)(to_us_since_boot(get_absolute_time()) % 4),  // Random UART 0-3
        0xDEADBEEF);  // Classic hex code
    
    http_send_response(conn, response_buffer, len);
    http_close_connection(conn);
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
    
    // Route request to appropriate handler (ADR-018 Phase 4)
    http_route_handler_t handler = http_router_find_handler(request_buffer);
    if (handler) {
        handler(conn, request_buffer, copy_len);
    } else {
        // 404 Not Found
        http_handle_404(conn);
    }
    
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    
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

// Request parsing functions moved to http_router module (ADR-018 Phase 4)

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
// Form handling functions moved to http_forms module (ADR-018 Phase 3)

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
