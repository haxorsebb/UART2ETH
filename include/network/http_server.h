/**
 * @file http_server.h
 * @brief HTTP Server for UART2ETH Device Information Web Interface
 * 
 * Provides a simple HTTP server that serves device information including
 * IP address, port numbers, and basic device status when accessed via
 * web browser on port 80.
 * 
 * Features:
 * - Simple HTML page with device information
 * - Shows current IP address and TCP port assignments
 * - Device status and firmware information
 * - Built on lwIP HTTP server using raw API
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Optional WebUI Module
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct http_connection http_connection_t;

/**
 * @brief HTTP server status enumeration
 */
typedef enum {
    HTTP_SERVER_STATUS_UNINITIALIZED,   // Not initialized
    HTTP_SERVER_STATUS_INITIALIZING,    // Initialization in progress
    HTTP_SERVER_STATUS_READY,           // Server running and ready
    HTTP_SERVER_STATUS_ERROR            // Error state
} http_server_status_t;

/**
 * @brief HTTP server statistics structure
 */
typedef struct {
    uint32_t requests_served;           // Total HTTP requests served
    uint32_t bytes_sent;               // Total bytes sent in responses
    uint32_t current_connections;      // Current active connections
    uint32_t max_connections;          // Maximum concurrent connections
    uint32_t uptime_seconds;           // Server uptime in seconds
} http_server_stats_t;

/**
 * @brief HTTP server initialization and control
 */

/**
 * Initialize HTTP server on port 80
 * @return true if initialization successful, false otherwise
 */
bool http_server_init(void);

/**
 * Deinitialize HTTP server
 */
void http_server_deinit(void);

/**
 * Process HTTP server tasks (call periodically from main loop)
 */
void http_server_process(void);

/**
 * Check if HTTP server is running
 * @return true if server is active and listening, false otherwise
 */
bool http_server_is_running(void);

/**
 * Get HTTP server status
 * @return Current server status
 */
http_server_status_t http_server_get_status(void);

/**
 * Get HTTP server statistics
 * @param stats Output structure for server statistics
 */
void http_server_get_stats(http_server_stats_t* stats);

/**
 * Reset HTTP server statistics counters
 */
void http_server_reset_stats(void);

/**
 * @brief HTTP Response Sending (exposed for auth module)
 * 
 * Send HTTP response to client connection. Used by authentication
 * and other modules that need to send responses.
 * 
 * @param conn HTTP connection to send response on
 * @param response Response string to send
 * @param response_len Length of response in bytes
 * 
 * Reference: ADR-018 HTTP Server Modularization
 */
void http_send_response(http_connection_t* conn, const char* response, size_t response_len);

/**
 * @brief HTTP Basic Authentication Functions
 * 
 * Reference: ADR-016 HTTP Basic Authentication
 * Note: These functions are now in http_auth module (ADR-018)
 */

/**
 * Decode base64 string to plain text
 * @param input Base64 encoded string
 * @param output Buffer to store decoded output
 * @param max_len Maximum length of output buffer
 * @return Number of bytes decoded, or <=0 on error
 */
int http_base64_decode(const char* input, char* output, size_t max_len);

/**
 * Check HTTP Basic Authentication credentials
 * @param request Full HTTP request string
 * @param expected_password Password to validate against
 * @return true if authenticated (username=admin, password matches), false otherwise
 */
bool http_check_authentication(const char* request, const char* expected_password);

/**
 * @brief Password Change Validation
 * 
 * Reference: ADR-016 HTTP Basic Authentication - Password Management
 */

/**
 * Password change validation result codes
 */
typedef enum {
    PWD_CHANGE_OK = 0,              // Password change validation successful
    PWD_CHANGE_CURRENT_WRONG = 1,   // Current password doesn't match
    PWD_CHANGE_TOO_SHORT = 2,       // New password too short (<8 chars)
    PWD_CHANGE_TOO_LONG = 3,        // New password too long (>31 chars)
    PWD_CHANGE_NO_MATCH = 4,        // New password != confirmation
    PWD_CHANGE_EMPTY_FIELD = 5      // One or more fields empty
} password_change_result_t;

/**
 * Validate password change request
 * 
 * Validates password change according to rules:
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
 */
password_change_result_t http_validate_password_change(
    const char* current_pwd,
    const char* new_pwd,
    const char* confirm_pwd,
    const char* stored_pwd
);

/**
 * @brief Firmware Upload Functions
 * 
 * Reference: ADR-017 Update Module, ADR-016 Firmware Update Web Interface
 */

/**
 * Start a firmware upload session
 * @param expected_size Total expected size of firmware file (without multipart overhead)
 * @return true if upload session started successfully, false otherwise
 */
bool http_upload_session_start(uint32_t expected_size);

/**
 * Feed a chunk of firmware data to the upload session
 * @param bytes_received Number of bytes in this chunk
 */
void http_upload_receive_chunk(uint32_t bytes_received);

/**
 * Reset/abort the current upload session
 */
void http_upload_session_reset(void);

/**
 * Get upload session progress
 * @param bytes_received Output: bytes received so far
 * @param total_bytes Output: total expected bytes
 */
void http_upload_get_progress(uint32_t* bytes_received, uint32_t* total_bytes);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_H
