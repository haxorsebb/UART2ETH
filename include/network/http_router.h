/**
 * @file http_router.h
 * @brief HTTP Request Router for UART2ETH Web Interface
 * 
 * Provides a route registration and dispatch system for HTTP requests.
 * Replaces the large if-else routing chain with a clean table-based
 * lookup system.
 * 
 * Features:
 * - Route registration with path and method matching
 * - Handler function dispatch
 * - Simple exact-match routing (no wildcards)
 * 
 * Documentation Reference:
 * - ADR-018 HTTP Server Modularization - Phase 4: Request Routing
 */

#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct http_connection http_connection_t;

/**
 * @brief HTTP request method types
 */
typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_UNKNOWN
} http_method_t;

/**
 * @brief Route handler function type
 * 
 * Handler functions receive the connection and request buffer.
 * They are responsible for:
 * - Parsing request data
 * - Calling appropriate business logic
 * - Sending response via http_send_response()
 * 
 * @param conn HTTP connection
 * @param request_buffer Full HTTP request string
 * @param buffer_len Length of request buffer
 */
typedef void (*http_route_handler_t)(http_connection_t* conn, 
                                      const char* request_buffer, 
                                      size_t buffer_len);

/**
 * @brief Initialize the HTTP router
 * 
 * Sets up the route table. Must be called before http_router_register_route().
 */
void http_router_init(void);

/**
 * @brief Register a route with its handler
 * 
 * @param path URL path (e.g., "/", "/config", "/update")
 * @param method HTTP method (GET or POST)
 * @param handler Function to call when route matches
 * @return true if registered successfully, false if table full
 */
bool http_router_register_route(const char* path, 
                                 http_method_t method, 
                                 http_route_handler_t handler);

/**
 * @brief Find handler for a request
 * 
 * Matches the request path and method against registered routes.
 * Parses the HTTP request to extract method and path, then looks
 * up the appropriate handler in the route table.
 * 
 * @param request_buffer Full HTTP request string
 * @return Handler function if route found, NULL otherwise
 */
http_route_handler_t http_router_find_handler(const char* request_buffer);

#ifdef __cplusplus
}
#endif

#endif // HTTP_ROUTER_H
