/**
 * @file http_router.c
 * @brief HTTP Request Router Implementation
 * 
 * Implements route registration and dispatch for HTTP requests.
 * 
 * Architecture:
 * - Static route table with max 20 routes
 * - Simple exact-match routing (no wildcards)
 * - O(n) lookup time (acceptable for small route count)
 * 
 * Documentation Reference:
 * - ADR-018 HTTP Server Modularization - Phase 4
 */

#include "http_router.h"
#include <string.h>
#include <stdio.h>

// Maximum number of routes that can be registered
#define HTTP_ROUTER_MAX_ROUTES 20

/**
 * @brief Route table entry
 */
typedef struct {
    const char* path;              // URL path (e.g., "/", "/config")
    http_method_t method;          // HTTP method (GET or POST)
    http_route_handler_t handler;  // Handler function to call
} http_route_entry_t;

// Static route table
static http_route_entry_t g_route_table[HTTP_ROUTER_MAX_ROUTES];
static size_t g_route_count = 0;

/**
 * @brief Initialize the HTTP router
 */
void http_router_init(void) {
    // Clear the route table
    memset(g_route_table, 0, sizeof(g_route_table));
    g_route_count = 0;
    
    /* printf("HTTP Router: Initialized (max %d routes)\n", HTTP_ROUTER_MAX_ROUTES); */
}

/**
 * @brief Register a route with its handler
 */
bool http_router_register_route(const char* path, 
                                 http_method_t method, 
                                 http_route_handler_t handler) {
    if (!path || !handler) {
        /* printf("HTTP Router: Invalid parameters for route registration\n"); */
        return false;
    }
    
    if (g_route_count >= HTTP_ROUTER_MAX_ROUTES) {
        /* printf("HTTP Router: Route table full (max %d routes)\n", HTTP_ROUTER_MAX_ROUTES); */
        return false;
    }
    
    // Add route to table
    g_route_table[g_route_count].path = path;
    g_route_table[g_route_count].method = method;
    g_route_table[g_route_count].handler = handler;
    g_route_count++;
    
    /* printf("HTTP Router: Registered %s %s (total routes: %zu)\n", 
           method == HTTP_METHOD_GET ? "GET" : "POST", 
           path, 
           g_route_count); */
    
    return true;
}

/**
 * @brief Parse HTTP method from request
 */
static http_method_t http_router_parse_method(const char* request_buffer) {
    if (!request_buffer) {
        return HTTP_METHOD_UNKNOWN;
    }
    
    if (strncmp(request_buffer, "GET", 3) == 0) {
        return HTTP_METHOD_GET;
    } else if (strncmp(request_buffer, "POST", 4) == 0) {
        return HTTP_METHOD_POST;
    }
    
    return HTTP_METHOD_UNKNOWN;
}

/**
 * @brief Extract path from HTTP request
 * 
 * Request format: "GET /config HTTP/1.1\r\n"
 *                      ^      ^
 *                      start  end
 * 
 * @param request_buffer HTTP request string
 * @param path_out Output buffer for path
 * @param path_max Maximum length of output buffer
 * @return true if path extracted successfully
 */
static bool http_router_extract_path(const char* request_buffer, 
                                      char* path_out, 
                                      size_t path_max) {
    if (!request_buffer || !path_out || path_max == 0) {
        return false;
    }
    
    // Find first space (after method)
    const char* path_start = strchr(request_buffer, ' ');
    if (!path_start) {
        return false;
    }
    path_start++; // Skip the space
    
    // Find second space (before HTTP version)
    const char* path_end = strchr(path_start, ' ');
    if (!path_end) {
        return false;
    }
    
    // Calculate path length
    size_t path_len = path_end - path_start;
    if (path_len == 0 || path_len >= path_max) {
        return false;
    }
    
    // Copy path to output buffer
    memcpy(path_out, path_start, path_len);
    path_out[path_len] = '\0';
    
    return true;
}

/**
 * @brief Find handler for a request
 */
http_route_handler_t http_router_find_handler(const char* request_buffer) {
    if (!request_buffer) {
        return NULL;
    }
    
    // Parse method
    http_method_t method = http_router_parse_method(request_buffer);
    if (method == HTTP_METHOD_UNKNOWN) {
        /* printf("HTTP Router: Unknown HTTP method\n"); */
        return NULL;
    }
    
    // Extract path
    char path[128];
    if (!http_router_extract_path(request_buffer, path, sizeof(path))) {
        /* printf("HTTP Router: Failed to extract path from request\n"); */
        return NULL;
    }
    
    /* printf("HTTP Router: Looking up %s %s\n", 
           method == HTTP_METHOD_GET ? "GET" : "POST", 
           path); */
    
    // Search route table for matching path and method
    for (size_t i = 0; i < g_route_count; i++) {
        if (g_route_table[i].method == method && 
            strcmp(g_route_table[i].path, path) == 0) {
            /* printf("HTTP Router: Found handler for %s %s\n",
                   method == HTTP_METHOD_GET ? "GET" : "POST",
                   path); */
            return g_route_table[i].handler;
        }
    }
    
    /* printf("HTTP Router: No handler found for %s %s\n",
           method == HTTP_METHOD_GET ? "GET" : "POST",
           path); */
    return NULL;
}
