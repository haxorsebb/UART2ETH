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

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_H
