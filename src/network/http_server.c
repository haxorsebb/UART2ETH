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

#ifdef FACTORY_INTERNAL_VERSION
#include "factory_defaults.h"
#endif

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
static void http_generate_stylesheet(char* buffer, size_t buffer_size);
static http_request_type_t http_parse_request_type(const char* request_data);
static bool http_parse_post_data(const char* post_data, size_t data_len);
static bool http_handle_password_change(const char* post_data, size_t data_len);
static void http_send_redirect(http_connection_t* conn, const char* location);

// HTTP Basic Authentication functions
int http_base64_decode(const char* input, char* output, size_t max_len);
bool http_check_authentication(const char* request, const char* expected_password);
static void http_send_auth_required(http_connection_t* conn);

#ifdef FACTORY_INTERNAL_VERSION
static void http_generate_factory_page(char* buffer, size_t buffer_size, const char* error_msg, size_t error_msg_size,  const char* success_msg, size_t success_msg_size);
static bool http_parse_factory_post_data(const char* post_data, size_t data_len, char* error_msg, size_t error_msg_size, char* success_msg, size_t success_msg_size);
#endif

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
 * @brief Decode base64 string
 * 
 * Decodes a base64 encoded string to plain text.
 * Used for decoding HTTP Basic Authentication credentials.
 * 
 * @param input Base64 encoded string
 * @param output Buffer to store decoded output
 * @param max_len Maximum length of output buffer
 * @return Number of bytes decoded, or <=0 on error
 * 
 * Reference: ADR-016 HTTP Basic Authentication
 */
int http_base64_decode(const char* input, char* output, size_t max_len) {
    if (!input || !output || max_len == 0) {
        return -1;
    }
    
    // Base64 decode table
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t input_len = strlen(input);
    if (input_len == 0) {
        output[0] = '\0';
        return 0;
    }
    
    size_t output_idx = 0;
    uint32_t buf = 0;
    int buf_len = 0;
    
    for (size_t i = 0; i < input_len && output_idx < max_len - 1; i++) {
        char c = input[i];
        
        // Skip padding and whitespace
        if (c == '=' || c == ' ' || c == '\r' || c == '\n') {
            continue;
        }
        
        // Find character in base64 table
        const char* pos = strchr(base64_chars, c);
        if (!pos) {
            // Invalid character
            return -1;
        }
        
        int val = pos - base64_chars;
        buf = (buf << 6) | val;
        buf_len += 6;
        
        if (buf_len >= 8) {
            buf_len -= 8;
            output[output_idx++] = (buf >> buf_len) & 0xFF;
        }
    }
    
    output[output_idx] = '\0';
    return output_idx;
}

/**
 * @brief Send 401 Unauthorized response
 * 
 * Sends HTTP 401 response with WWW-Authenticate header to
 * trigger browser authentication dialog.
 * 
 * @param conn HTTP connection to send response on
 * 
 * Reference: ADR-016 HTTP Basic Authentication
 */
static void http_send_auth_required(http_connection_t* conn) {
    static char auth_response[512];
    int len = snprintf(auth_response, sizeof(auth_response),
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic realm=\"UART2ETH Device\"\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>401 Unauthorized</title></head>\n"
        "<body>\n"
        "    <h1>401 Unauthorized</h1>\n"
        "    <p>Access denied. Please provide valid credentials.</p>\n"
        "    <p>Username: <strong>admin</strong></p>\n"
        "</body>\n"
        "</html>\r\n");
    
    // Calculate content length (HTML body only)
    const char* content_start = strstr(auth_response, "\r\n\r\n");
    if (content_start) {
        content_start += 4; // Skip the \r\n\r\n
        int content_len = strlen(content_start);
        
        // Rebuild with correct Content-Length
        len = snprintf(auth_response, sizeof(auth_response),
            "HTTP/1.1 401 Unauthorized\r\n"
            "WWW-Authenticate: Basic realm=\"UART2ETH Device\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head><title>401 Unauthorized</title></head>\n"
            "<body>\n"
            "    <h1>401 Unauthorized</h1>\n"
            "    <p>Access denied. Please provide valid credentials.</p>\n"
            "    <p>Username: <strong>admin</strong></p>\n"
            "</body>\n"
            "</html>\r\n", content_len);
    }

    http_send_response(conn, auth_response, len);
    // Don't close connection - let browser close it after receiving 401
    // or reuse it for authenticated request
}

/**
 * @brief Check HTTP Basic Authentication
 * 
 * Validates Authorization header against expected credentials.
 * Username must be "admin" and password must match expected_password.
 * 
 * @param request Full HTTP request string
 * @param expected_password Password to validate against
 * @return true if authenticated, false otherwise
 * 
 * Reference: ADR-016 HTTP Basic Authentication
 */
bool http_check_authentication(const char* request, const char* expected_password) {
    if (!request || !expected_password) {
        return false;
    }
    
    // Find Authorization header
    const char* auth_header = strstr(request, "Authorization: Basic ");
    if (!auth_header) {
        printf("HTTP Auth: No Authorization header found\n");
        return false;
    }
    
    // Extract base64 credentials (skip "Authorization: Basic ")
    auth_header += 21;  // strlen("Authorization: Basic ")
    
    // Find end of base64 string (CR or LF)
    char base64_creds[128] = {0};
    const char* end = auth_header;
    while (*end && *end != '\r' && *end != '\n' && (end - auth_header) < 127) {
        end++;
    }
    size_t cred_len = end - auth_header;
    strncpy(base64_creds, auth_header, cred_len);
    base64_creds[cred_len] = '\0';
    
    // Decode base64 to "username:password"
    char decoded[128] = {0};
    int decoded_len = http_base64_decode(base64_creds, decoded, sizeof(decoded));
    if (decoded_len <= 0) {
        printf("HTTP Auth: Base64 decode failed\n");
        return false;
    }
    
    // Split on ':' to extract username and password
    char* colon = strchr(decoded, ':');
    if (!colon) {
        printf("HTTP Auth: No colon separator in credentials\n");
        return false;
    }
    
    *colon = '\0';  // Split string
    char* username = decoded;
    char* password = colon + 1;
    
    // Validate username (must be "admin")
    if (strcmp(username, "admin") != 0) {
        printf("HTTP Auth: Invalid username '%s'\n", username);
        return false;
    }
    
    // Validate password
    if (strcmp(password, expected_password) != 0) {
        printf("HTTP Auth: Invalid password\n");
        return false;
    }
    
    printf("HTTP Auth: Authentication successful for user 'admin'\n");
    return true;
}

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
        "            <a href=\"/factory\">FACTORY DEFAULTS</a>\n"
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
        "                    <input type=\"text\" id=\"mac_addr\" name=\"mac_addr\" value=\"%s\" placeholder=\"02:00:00:00:00:01\" readonly>\n"
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
#if DEVICE_CHANNEL_4_ENABLED
        "                \n"
        "                <div class=\"uart-row\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"ch4_enabled\" name=\"ch4_enabled\" value=\"1\" %s>\n"
        "                        <label for=\"ch4_enabled\">UART4 Enabled</label>\n"
        "                    </div>\n"
        "                    <div class=\"form-group\" style=\"margin-bottom: 0;\">\n"
        "                        <label for=\"ch4_port\">TCP Port:</label>\n"
        "                        <input type=\"number\" id=\"ch4_port\" name=\"ch4_port\" value=\"%d\" min=\"1024\" max=\"65535\">\n"
        "                    </div>\n"
        "                    <div style=\"flex: 0.5; font-size: 14px; color: #7f8c8d;\">GP24/GP25</div>\n"
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
        "        \n"
        "        <!-- Password Change Form (separate from network/UART config) -->\n"
        "        <form method=\"POST\" action=\"/change_password\">\n"
        "            <div class=\"section\">\n"
        "                <h3>Security Configuration</h3>\n"
        "                <p>Change the administrator password. The default username is <strong>admin</strong> and cannot be changed.</p>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"current_password\">Current Password:</label>\n"
        "                    <input type=\"password\" id=\"current_password\" name=\"current_password\" required autocomplete=\"current-password\">\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"new_password\">New Password (8-31 characters):</label>\n"
        "                    <input type=\"password\" id=\"new_password\" name=\"new_password\" minlength=\"8\" maxlength=\"31\" required autocomplete=\"new-password\">\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"confirm_password\">Confirm New Password:</label>\n"
        "                    <input type=\"password\" id=\"confirm_password\" name=\"confirm_password\" minlength=\"8\" maxlength=\"31\" required autocomplete=\"new-password\">\n"
        "                </div>\n"
        "                \n"
        "                <button type=\"submit\" class=\"button\">Change Password</button>\n"
        "            </div>\n"
        "        </form>\n"
        "        \n"
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
#if DEVICE_CHANNEL_4_ENABLED
        ,layout->config.channels[CHANNEL_4].enabled ? "checked" : "",
        layout->config.channels[CHANNEL_4].tcp_port
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

/**
 * @brief Generate CSS stylesheet for all pages
 */
static void http_generate_stylesheet(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Common CSS styles for all pages - minified to save space
    snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/css\r\n"
        "Connection: close\r\n"
        "\r\n"
        "body{font-family:Arial,sans-serif;margin:40px;background-color:#f5f5f5}"
        ".container{background-color:white;padding:30px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:900px}"
        ".header{color:#2c3e50;border-bottom:2px solid #3498db;padding-bottom:10px;margin-bottom:30px}"
        ".header.warning{border-bottom-color:#e67e22}"
        ".section{margin-bottom:25px;padding:20px;border:1px solid #ddd;border-radius:4px}"
        ".section h3{margin-top:0;color:#2c3e50}"
        ".label{font-weight:bold;color:#34495e}"
        ".value{color:#2980b9;font-family:monospace}"
        ".status-ok{color:#27ae60;font-weight:bold}"
        ".port-table{border-collapse:collapse;width:100%%}"
        ".port-table th,.port-table td{border:1px solid #ddd;padding:8px;text-align:left}"
        ".port-table th{background-color:#3498db;color:white}"
        ".nav-links{margin:20px 0;text-align:center}"
        ".nav-links a{display:inline-block;margin:0 10px;padding:10px 20px;background-color:#95a5a6;color:white;text-decoration:none;border-radius:4px}"
        ".nav-links a:hover{background-color:#7f8c8d}"
        ".nav-links a.active{background-color:#3498db}"
        ".mode-badge{display:inline-block;padding:4px 12px;background-color:#9b59b6;color:white;border-radius:12px;font-size:12px;margin-left:10px}"
        ".warning-badge{display:inline-block;padding:6px 15px;background-color:#e67e22;color:white;border-radius:4px;font-size:14px;font-weight:bold;margin-left:10px}"
        ".form-group{margin-bottom:15px}"
        ".form-group label{display:block;margin-bottom:5px;font-weight:bold;color:#34495e}"
        ".form-group input,.form-group select{width:100%%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
        ".form-group small{display:block;margin-top:3px;color:#7f8c8d;font-size:12px}"
        ".form-row{display:flex;gap:15px}"
        ".form-row .form-group{flex:1}"
        ".checkbox-group{display:flex;align-items:center}"
        ".checkbox-group input[type=checkbox]{width:auto;margin-right:10px}"
        ".button{background-color:#3498db;color:white;padding:12px 24px;border:none;border-radius:4px;cursor:pointer;font-size:16px;font-weight:bold}"
        ".button:hover{background-color:#2980b9}"
        ".button-success{background-color:#27ae60}"
        ".button-success:hover{background-color:#229954}"
        ".button-danger{background-color:#e74c3c}"
        ".button-danger:hover{background-color:#c0392b}"
        ".button-secondary{background-color:#95a5a6}"
        ".button-secondary:hover{background-color:#7f8c8d}"
        ".current-status{background-color:#ecf0f1;padding:10px;border-radius:4px;margin-bottom:15px}"
        ".current-factory{padding:15px;border-radius:4px;margin-bottom:20px;border-left:4px solid}"
        ".current-factory.valid{background-color:#d5f4e6;border-color:#27ae60}"
        ".current-factory.invalid{background-color:#fadbd8;border-color:#e74c3c}"
        ".current-factory h4{margin-top:0;color:#2c3e50}"
        ".uart-row{display:flex;align-items:center;gap:15px;margin-bottom:15px}"
        ".uart-row>*{flex:1}"
        ".preview-box{background-color:#ecf0f1;padding:10px;border-radius:4px;margin-top:5px;font-family:monospace;font-size:14px;font-weight:bold}"
        ".error{color:#e74c3c}"
        ".success{color:#27ae60}\r\n"
    );
    
    printf("HTTP: Generated CSS stylesheet (%zu bytes)\n", strlen(buffer));
}

#ifdef FACTORY_INTERNAL_VERSION
/**
 * @brief Generate factory defaults configuration HTML page (manufacturing only)
 * 
 * Documentation Reference:
 * - ADR-015: Factory Defaults Web Interface
 */
/**
 * @brief Generate factory defaults configuration page (minified, server-side validation only)
 * 
 * Documentation Reference:
 * - ADR-015: Factory Defaults Web Interface
 */
static void http_generate_factory_page(char* buffer, size_t buffer_size, const char* error_msg, size_t error_msg_size,  const char* success_msg, size_t success_msg_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Get current factory defaults (if any)
    const factory_defaults_t* current_factory = factory_defaults_get();
    bool factory_valid = factory_defaults_is_valid();
    
    printf("FACTORY DEFAULTS GET %d 0x%08X %02d/%02d\n", factory_valid, current_factory, current_factory->production_week,current_factory->production_year);

    // Prepare current values for display
    char current_serial[32] = "Not Programmed";
    char current_mac[18] = "00:00:00:00:00:00";
    char current_ip[16] = "0.0.0.0";
    char current_netmask[16] = "0.0.0.0";
    const char* current_dhcp = "No";
    const char* current_board_type = "Unknown";
    char current_password[32] = "Not Set";
    
    if (factory_valid && current_factory) {
        // Format serial number as YYWW-NNNNNNNNNNNN
        uint64_t serial_decimal = 0;
        for (int i = 0; i < 6; i++) {
            serial_decimal = (serial_decimal << 8) | current_factory->serial_number[i];
        }
        snprintf(current_serial, sizeof(current_serial), "%02u%02u-%012llu",
                 current_factory->production_year,
                 current_factory->production_week,
                 serial_decimal);
        
        // Format MAC address
        snprintf(current_mac, sizeof(current_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 current_factory->mac_address[0], current_factory->mac_address[1],
                 current_factory->mac_address[2], current_factory->mac_address[3],
                 current_factory->mac_address[4], current_factory->mac_address[5]);
        
        // Format IP addresses
        uint32_t ip = current_factory->default_ip;
        snprintf(current_ip, sizeof(current_ip), "%d.%d.%d.%d",
                 (int)((ip >> 0) & 0xFF), (int)((ip >> 8) & 0xFF),
                 (int)((ip >> 16) & 0xFF), (int)((ip >> 24) & 0xFF));
        
        uint32_t netmask = current_factory->default_netmask;
        snprintf(current_netmask, sizeof(current_netmask), "%d.%d.%d.%d",
                 (int)((netmask >> 0) & 0xFF), (int)((netmask >> 8) & 0xFF),
                 (int)((netmask >> 16) & 0xFF), (int)((netmask >> 24) & 0xFF));
        
        current_dhcp = current_factory->default_dhcp_enable ? "Yes" : "No";
        
        // Get board type name
        switch (current_factory->board_type) {
            case BOARD_TYPE_SHARK: current_board_type = "SHARK"; break;
            case BOARD_TYPE_PRIMARY: current_board_type = "PRIMARY"; break;
            case BOARD_TYPE_SECONDARY: current_board_type = "SECONDARY"; break;
            default: current_board_type = "Unknown"; break;
        }
        
        // Copy password (show actual password for factory verification)
        snprintf(current_password, sizeof(current_password), "%s", current_factory->default_password);
    }
    
    // Generate minified HTML response (no JavaScript, server-side validation only)
    int html_len = snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><title>Factory Defaults</title>"
        "<link rel=\"stylesheet\" href=\"/styles.css\"></head><body>"
        "<div class=\"container\">"
        "<div class=\"header warning\">"
        "<h1>Factory Defaults<span class=\"warning-badge\">⚠️ FACTORY INTERNAL</span></h1>"
        "<p>Manufacturing Tool - Program Device Factory Configuration</p>"
        "</div>"
        "<div class=\"nav-links\"><a href=\"/\">Status</a><a href=\"/config\">Configuration</a></div>"
        "%s%s%s"  // Error message placeholder
        "%s%s%s"  // Success message placeholder
        "<div class=\"current-factory %s\">"
        "<h4>Currently Programmed</h4>"
        "<p><strong>Serial:</strong> %s</p>"
        "<p><strong>MAC:</strong> %s</p>"
        "<p><strong>Board:</strong> %s</p>"
        "<p><strong>IP:</strong> %s | <strong>Mask:</strong> %s | <strong>DHCP:</strong> %s</p>"
        "<p><strong>Access:</strong>User:Admin | Password %s</p>"
        "</div>"
        "<form method=\"POST\" action=\"/factory\">"
        "<div class=\"section\"><h3>Serial Number</h3>"
        "<div class=\"form-row\">"
        "<div class=\"form-group\"><label for=\"prod_year\">Production Year (YY):</label>"
        "<input type=\"number\" id=\"prod_year\" name=\"prod_year\" min=\"0\" max=\"99\" value=\"26\" required>"
        "<small>YY for 20YY (e.g., 26=2026)</small></div>"
        "<div class=\"form-group\"><label for=\"prod_week\">Production Week:</label>"
        "<input type=\"number\" id=\"prod_week\" name=\"prod_week\" min=\"1\" max=\"52\" value=\"1\" required>"
        "<small>Week 1-52</small></div>"
        "</div>"
        "<div class=\"form-group\"><label for=\"serial_number\">Serial Number (Decimal):</label>"
        "<input type=\"text\" id=\"serial_number\" name=\"serial_number\" value=\"1\" required>"
        "<small>Unique serial (0-281474976710655)</small></div>"
        "</div>"
        "<div class=\"section\"><h3>Network Identity</h3>"
        "<div class=\"form-group\"><label for=\"mac_address\">MAC Address:</label>"
        "<input type=\"text\" id=\"mac_address\" name=\"mac_address\" value=\"02:00:00:00:00:01\" required>"
        "<small>Format: XX:XX:XX:XX:XX:XX</small></div>"
        "</div>"
        "<div class=\"section\"><h3>Board Type</h3>"
        "<div class=\"form-group\"><label for=\"board_type\">Hardware Variant:</label>"
        "<select id=\"board_type\" name=\"board_type\" required>"
        "<option value=\"0\" selected>SHARK</option>"
        "<option value=\"1\">PRIMARY</option>"
        "<option value=\"2\">SECONDARY</option>"
        "</select></div>"
        "</div>"
        "<div class=\"section\"><h3>Default Network</h3>"
        "<div class=\"form-row\">"
        "<div class=\"form-group\"><label for=\"default_ip\">Default IP:</label>"
        "<input type=\"text\" id=\"default_ip\" name=\"default_ip\" value=\"192.168.1.100\" required></div>"
        "<div class=\"form-group\"><label for=\"default_netmask\">Default Netmask:</label>"
        "<input type=\"text\" id=\"default_netmask\" name=\"default_netmask\" value=\"255.255.255.0\" required></div>"
        "</div>"
        "<div class=\"form-group\"><div class=\"checkbox-group\">"
        "<input type=\"checkbox\" id=\"default_dhcp\" name=\"default_dhcp\" value=\"1\">"
        "<label for=\"default_dhcp\">Enable DHCP by default</label>"
        "</div></div>"
        "</div>"
        "<div class=\"section\"><h3>Security</h3>"
        "<div class=\"form-group\"><label for=\"default_password\">Factory Password:</label>"
        "<input type=\"text\" id=\"default_password\" name=\"default_password\" value=\"admin\" maxlength=\"31\" required>"
        "<small>Max 31 characters</small></div>"
        "</div>"
        "<div class=\"section\"><h3>Actions</h3>"
        "<p><strong>Warning:</strong> This permanently programs flash memory.</p>"
        "<button type=\"submit\" class=\"button button-success\">✓ Write Factory Defaults</button> "
        "<a href=\"/\" class=\"button button-secondary\">Cancel</a>"
        "</div>"
        "</form>"
        "</div></body></html>\r\n",
        error_msg_size > 0 ? "<div class=\"section\" style=\"background-color: #fadbd8;border-left:4px solid #e74c3c\">" : "",
        error_msg_size > 0 ? error_msg : "",
        error_msg_size > 0 ? "</div>" : "",
        success_msg_size > 0 ? "<div class=\"section\" style=\"background-color: #fadbd8;border-left:4px solid #3ce74aff\">" : "",
        success_msg_size > 0 ? success_msg : "",
        success_msg_size > 0 ? "</div>" : "",
        factory_valid ? "valid" : "invalid",
        current_serial, current_mac, current_board_type,
        current_ip, current_netmask, current_dhcp, current_password
    );
    
    printf("HTTP: Generated factory page (%d bytes, %s)\n", html_len, error_msg ? "with error" : "OK");
    if (html_len >= buffer_size) {
        printf("HTTP: ERROR - Factory page truncated! Need %d bytes\n", html_len);
    }
}

/**
 * @brief Parse factory defaults POST data and write to flash (manufacturing only)
 * 
 * Documentation Reference:
 * - ADR-015: Factory Defaults Web Interface
 */
static bool http_parse_factory_post_data(const char* post_data, size_t data_len, char* error_msg, size_t error_msg_size, char* success_msg, size_t success_msg_size) {
    // Find start of form data (after double CRLF)
    const char* form_start = strstr(post_data, "\r\n\r\n");
    if (!form_start) {
        snprintf(error_msg, error_msg_size, "Invalid POST data format");
        return false;
    }
    form_start += 4; // Skip past \r\n\r\n
    
    printf("HTTP: Parsing factory defaults form data: %s\n", form_start);
    
    // Prepare factory defaults structure
    factory_defaults_t factory_data = {0};
    
    // Parse form fields
    char* form_copy = malloc(strlen(form_start) + 1);
    if (!form_copy) {
        snprintf(error_msg, error_msg_size, "Memory allocation failed");
        return false;
    }
    strcpy(form_copy, form_start);
    
    // Initialize defaults
    uint8_t prod_year = 0;
    uint8_t prod_week = 0;
    uint64_t serial_number = 0;
    bool has_year = false, has_week = false, has_serial = false;
    bool has_mac = false, has_board_type = false;
    bool has_ip = false, has_netmask = false;
    bool has_password = false;
    
    char* token = strtok(form_copy, "&");
    while (token) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char* key = token;
            char* value = equals + 1;
            
            // Parse serial number fields with validation
            if (strcmp(key, "prod_year") == 0) {
                int year = atoi(value);
                if (year < 0 || year > 99) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Production year must be 0-99");
                    return false;
                }
                prod_year = (uint8_t)year;
                has_year = true;
            } else if (strcmp(key, "prod_week") == 0) {
                int week = atoi(value);
                if (week < 1 || week > 52) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Production week must be 1-52");
                    return false;
                }
                prod_week = (uint8_t)week;
                has_week = true;
            } else if (strcmp(key, "serial_number") == 0) {
                // Parse decimal serial number
                char* endptr;
                unsigned long long sn = strtoull(value, &endptr, 10);
                if (*endptr != '\0' || sn > 281474976710655ULL) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Serial number must be 0-281474976710655");
                    return false;
                }
                serial_number = sn;
                has_serial = true;
            }
            // Parse MAC address
            else if (strcmp(key, "mac_address") == 0) {
                int m[6];
                if (sscanf(value, "%02x%%3A%02x%%3A%02x%%3A%02x%%3A%02x%%3A%02x", 
                          &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6 ||
                    sscanf(value, "%02x-%02x-%02x-%02x-%02x-%02x", 
                          &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
                    for (int i = 0; i < 6; i++) {
                        factory_data.mac_address[i] = (uint8_t)m[i];
                    }
                    has_mac = true;
                }
            }
            // Parse board type
            else if (strcmp(key, "board_type") == 0) {
                int board_type = atoi(value);
                if (board_type >= 0 && board_type <= 2) {
                    factory_data.board_type = (uint8_t)board_type;
                    has_board_type = true;
                }
            }
            // Parse default IP
            else if (strcmp(key, "default_ip") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                    factory_data.default_ip = (uint32_t)a | ((uint32_t)b << 8) | 
                                             ((uint32_t)c << 16) | ((uint32_t)d << 24);
                    has_ip = true;
                }
            }
            // Parse default netmask
            else if (strcmp(key, "default_netmask") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                    factory_data.default_netmask = (uint32_t)a | ((uint32_t)b << 8) | 
                                                  ((uint32_t)c << 16) | ((uint32_t)d << 24);
                    has_netmask = true;
                }
            }
            // Parse default DHCP
            else if (strcmp(key, "default_dhcp") == 0 && strcmp(value, "1") == 0) {
                factory_data.default_dhcp_enable = 1;
            }
            // Parse default password with validation
            else if (strcmp(key, "default_password") == 0) {
                // URL decode password (replace + with space, decode %)
                char decoded_password[32] = {0};
                size_t decoded_len = 0;
                for (size_t i = 0; value[i] && decoded_len < 31; i++) {
                    if (value[i] == '+') {
                        decoded_password[decoded_len++] = ' ';
                    } else if (value[i] == '%' && value[i+1] && value[i+2]) {
                        int hex_val;
                        sscanf(&value[i+1], "%02x", &hex_val);
                        decoded_password[decoded_len++] = (char)hex_val;
                        i += 2;
                    } else {
                        decoded_password[decoded_len++] = value[i];
                    }
                }
                if (decoded_len > 31) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Password must be max 31 characters");
                    return false;
                }
                strncpy(factory_data.default_password, decoded_password, 31);
                factory_data.default_password[31] = '\0';
                has_password = true;
            }
        }
        token = strtok(NULL, "&");
    }
    
    free(form_copy);
    
    // Validate all required fields present
    if (!has_year || !has_week || !has_serial) {
        snprintf(error_msg, error_msg_size, "Missing serial number fields");
        return false;
    }
    if (!has_mac) {
        snprintf(error_msg, error_msg_size, "Missing MAC address");
        return false;
    }
    if (!has_board_type) {
        snprintf(error_msg, error_msg_size, "Missing board type");
        return false;
    }
    if (!has_ip || !has_netmask) {
        snprintf(error_msg, error_msg_size, "Missing IP configuration");
        return false;
    }
    if (!has_password) {
        snprintf(error_msg, error_msg_size, "Missing default password");
        return false;
    }
    
    // Convert decimal serial number to 6-byte array (big-endian)
    factory_data.production_year = prod_year;
    factory_data.production_week = prod_week;
    for (int i = 5; i >= 0; i--) {
        factory_data.serial_number[i] = (uint8_t)(serial_number & 0xFF);
        serial_number >>= 8;
    }
    
    // Log what we're about to write
    printf("HTTP: Factory defaults to write:\n");
    printf("  Serial: %02u%02u-%02X%02X%02X%02X%02X%02X\n",
           factory_data.production_year, factory_data.production_week,
           factory_data.serial_number[0], factory_data.serial_number[1],
           factory_data.serial_number[2], factory_data.serial_number[3],
           factory_data.serial_number[4], factory_data.serial_number[5]);
    printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           factory_data.mac_address[0], factory_data.mac_address[1],
           factory_data.mac_address[2], factory_data.mac_address[3],
           factory_data.mac_address[4], factory_data.mac_address[5]);
    printf("  Board Type: %u\n", factory_data.board_type);
    printf("  Default IP: %u.%u.%u.%u\n",
           (unsigned int)(factory_data.default_ip & 0xFF),
           (unsigned int)((factory_data.default_ip >> 8) & 0xFF),
           (unsigned int)((factory_data.default_ip >> 16) & 0xFF),
           (unsigned int)((factory_data.default_ip >> 24) & 0xFF));
    printf("  Default DHCP: %s\n", factory_data.default_dhcp_enable ? "Yes" : "No");
    
    // Write factory defaults to flash
    if (!factory_defaults_write(&factory_data)) {
        snprintf(error_msg, error_msg_size, "Flash write operation failed");
        return false;
    }
    
    printf("HTTP: Factory defaults written successfully!\n");
    
    // Verify by reloading
    if (!factory_defaults_init()) {
        snprintf(error_msg, error_msg_size, "Write succeeded but verification failed");
        return false;
    }
    
    printf("HTTP: Factory defaults verified successfully!\n");
    snprintf(success_msg, success_msg_size, "Factory defaults updated successfully!");
    return true;
}
#endif // FACTORY_INTERNAL_VERSION
