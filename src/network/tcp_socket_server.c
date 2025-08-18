/**
 * @file tcp_socket_server.c
 * @brief TCP Socket Server Implementation using lwIP Raw API
 * 
 * Implements TCP server with line-based protocol for UART bridging.
 * Uses lwIP Raw TCP API callbacks for event-driven networking.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - TCP Socket Server Building Block
 * - Issue #61: Add sockets to network implementation
 */

#include "network/tcp_socket_server.h"
#include "network/network_manager.h"
#include "log_manager.h"
#include "debug.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include <string.h>
#include <stdio.h>

// Maximum number of concurrent connections
#define TCP_SERVER_MAX_CONNECTIONS 4
#define TCP_SERVER_LINE_BUFFER_SIZE 1024
#define MINIMUM_MESSAGE_LENGTH 8  // '#0000!\r\n' minimum

// TCP server error codes for logging
#define TCP_ERROR_PCB_CREATION_FAILED 1
#define TCP_ERROR_BIND_FAILED 2
#define TCP_ERROR_LISTEN_FAILED 3

/**
 * @brief TCP connection state structure
 */
typedef struct tcp_connection {
    struct tcp_pcb* pcb;                           // lwIP TCP PCB
    char line_buffer[TCP_SERVER_LINE_BUFFER_SIZE+1]; // Line assembly buffer
    size_t line_pos;                               // Current position in buffer
    bool active;                                   // Connection active flag
    uint32_t bytes_sent;                          // Bytes sent on this connection
    uint32_t bytes_received;                      // Bytes received on this connection
    uint32_t last_activity_ms;                    // For timeout detection
} tcp_connection_t;

// Server state
static struct tcp_pcb* g_server_pcb = NULL;
static tcp_connection_t g_connections[TCP_SERVER_MAX_CONNECTIONS];
static tcp_server_stats_t g_server_stats;
static bool g_server_initialized = false;
static uint16_t g_listen_port = 0;

// Forward declarations
static err_t tcp_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
static err_t tcp_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
static void tcp_connection_error_callback(void* arg, err_t err);
static err_t tcp_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len);
static tcp_connection_t* find_free_connection(void);
static tcp_connection_t* find_connection_by_pcb(struct tcp_pcb* pcb);
static void close_connection(tcp_connection_t* conn);
static int process_received_data(tcp_connection_t* conn, const char* data, size_t len);

/**
 * @brief Initialize TCP socket server on specified port
 * @param port TCP port to listen on (e.g., 4001)
 * @return true if initialization successful, false otherwise
 * @note Requires network manager to be initialized (not ERROR/UNINITIALIZED)
 */
bool tcp_socket_server_init(uint16_t port) {
    if (g_server_initialized) {
        DEBUG_ONLY({
            printf("TCP Server: Already initialized\n");
        });
        return true;
    }
    
    
    // Check network is initialized (allow non-READY states for unit testing)
    network_status_t status = network_manager_get_status();
    if (status == NETWORK_STATUS_UNINITIALIZED || status == NETWORK_STATUS_ERROR) {
        DEBUG_ONLY({
            printf("TCP Server: Network not initialized (status=%d)\n", status);
        });
        return false;
    }
    
    DEBUG_ONLY({
        printf("TCP Server: Initializing on port %u\n", port);
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, port);
    
    // Create new TCP PCB
    g_server_pcb = tcp_new();
    if (!g_server_pcb) {
        DEBUG_ONLY({
            printf("TCP Server: Failed to create PCB\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_PCB_CREATION_FAILED);
        return false;
    }
    
    // Bind to port
    err_t err = tcp_bind(g_server_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        DEBUG_ONLY({
            printf("TCP Server: Failed to bind to port %u (error %d)\n", port, err);
        });
        tcp_close(g_server_pcb);
        g_server_pcb = NULL;
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_BIND_FAILED);
        return false;
    }
    
    // Start listening
    g_server_pcb = tcp_listen(g_server_pcb);
    if (!g_server_pcb) {
        DEBUG_ONLY({
            printf("TCP Server: Failed to listen\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_LISTEN_FAILED);
        return false;
    }
    
    // Set accept callback
    tcp_accept(g_server_pcb, tcp_server_accept_callback);
    
    // Initialize connection pool
    memset(g_connections, 0, sizeof(g_connections));
    
    // Initialize statistics
    memset(&g_server_stats, 0, sizeof(tcp_server_stats_t));
    g_server_stats.listen_port = port;
    g_server_stats.max_connections = TCP_SERVER_MAX_CONNECTIONS;
    
    g_listen_port = port;
    g_server_initialized = true;
    
    DEBUG_ONLY({
        printf("TCP Server: Listening on port %u\n", port);
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, port);
    
    return true;
}

/**
 * @brief Deinitialize TCP socket server and cleanup all resources
 * @note Closes all active connections and resets server state
 */
void tcp_socket_server_deinit(void) {
    if (!g_server_initialized) {
        return;
    }
    
    DEBUG_ONLY({
        printf("TCP Server: Deinitializing\n");
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, g_listen_port);
    
    // Close all active connections
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (g_connections[i].active) {
            close_connection(&g_connections[i]);
        }
    }
    
    // Close server PCB
    if (g_server_pcb) {
        tcp_close(g_server_pcb);
        g_server_pcb = NULL;
    }
    
    // Reset all state
    memset(g_connections, 0, sizeof(g_connections));
    memset(&g_server_stats, 0, sizeof(tcp_server_stats_t));
    g_server_initialized = false;
    g_listen_port = 0;
    
    DEBUG_ONLY({
        printf("TCP Server: Deinitialized\n");
    });
}

/**
 * @brief Process TCP server tasks
 */
void tcp_socket_server_process(void) {
    // No explicit processing needed - all handled by lwIP callbacks
    // This function exists for integration with Core1 main loop
}

/**
 * @brief Check if server is listening
 */
bool tcp_socket_server_is_listening(void) {
    return g_server_initialized && (g_server_pcb != NULL);
}

/**
 * @brief Get server statistics
 */
void tcp_socket_server_get_stats(tcp_server_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    // Update active connections count
    g_server_stats.active_connections = 0;
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (g_connections[i].active) {
            g_server_stats.active_connections++;
        }
    }
    
    memcpy(stats, &g_server_stats, sizeof(tcp_server_stats_t));
}

/**
 * @brief Reset server statistics
 */
void tcp_socket_server_reset_stats(void) {
    uint16_t port = g_server_stats.listen_port;
    uint32_t max_conn = g_server_stats.max_connections;
    
    memset(&g_server_stats, 0, sizeof(tcp_server_stats_t));
    g_server_stats.listen_port = port;
    g_server_stats.max_connections = max_conn;
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_STATUS, 0);
}

/**
 * @brief Process line-based protocol
 */
int tcp_socket_server_process_line(const char* input, size_t input_len, 
                                   char* output, size_t output_size) {
    if (!input || !output || input_len == 0 || output_size == 0) {
        return -1;
    }
    
    // Simple echo - copy input to output
    size_t copy_len = (input_len < output_size) ? input_len : output_size - 1;
    memcpy(output, input, copy_len);
    output[copy_len] = '\0';
    
    return (int)copy_len;
}

/**
 * @brief Check if buffer ends with exactly '!\r\n'
 */
bool check_message_end(const char* buffer, size_t length) {
    if (!buffer || length < MINIMUM_MESSAGE_LENGTH) {
        return false;
    }
    
    return buffer[length-3] == '!' && 
           buffer[length-2] == '\r' && 
           buffer[length-1] == '\n';
}

// Private function implementations

/**
 * @brief TCP accept callback
 */
static err_t tcp_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err) {
    LWIP_UNUSED_ARG(arg);
    
    if (err != ERR_OK || !newpcb) {
        return ERR_VAL;
    }
    
    DEBUG_ONLY({
        printf("TCP Server: New connection attempt\n");
    });
    
    // Find free connection slot
    tcp_connection_t* conn = find_free_connection();
    if (!conn) {
        DEBUG_ONLY({
            printf("TCP Server: No free connection slots\n");
        });
        g_server_stats.connection_errors++;
        tcp_close(newpcb);
        return ERR_MEM;
    }
    
    // Initialize connection
    conn->pcb = newpcb;
    conn->line_pos = 0;
    conn->active = true;
    conn->bytes_sent = 0;
    conn->bytes_received = 0;
    // Clear buffer with known pattern for debugging
    memset(conn->line_buffer, 0xAA, sizeof(conn->line_buffer));
    conn->line_buffer[sizeof(conn->line_buffer)-1] = '\0';
    
    // Set callbacks
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, tcp_connection_recv_callback);
    tcp_err(newpcb, tcp_connection_error_callback);
    tcp_sent(newpcb, tcp_connection_sent_callback);
    
    // Update statistics
    g_server_stats.total_connections++;
    
    DEBUG_ONLY({
        printf("TCP Server: Connection accepted\n");
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, 1);
    
    return ERR_OK;
}

/**
 * @brief TCP receive callback
 */
static err_t tcp_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
    tcp_connection_t* conn = (tcp_connection_t*)arg;
    
    if (err != ERR_OK || !conn) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_ARG;
    }
    
    // Connection closed by remote
    if (!p) {
        DEBUG_ONLY({
            printf("TCP Server: Connection closed by client\n");
        });
        close_connection(conn);
        return ERR_OK;
    }
    
    // Process received data
    // Process entire pbuf chain!
    struct pbuf *q = p;
    uint16_t total_len = p->tot_len;
    int processed = 0;

    while (q != NULL) {
        // Extract data from q->payload, length q->len
        processed += process_received_data(conn, (const char*)q->payload, q->len);

        if (processed> 0) {
            conn->bytes_received += processed;
            g_server_stats.bytes_received += processed;
        }
        q = q->next;
    }

    tcp_recved(tpcb, total_len);
    pbuf_free(p);
    
    return ERR_OK;
}

/**
 * @brief TCP error callback
 */
static void tcp_connection_error_callback(void* arg, err_t err) {
    tcp_connection_t* conn = (tcp_connection_t*)arg;
    
    LWIP_UNUSED_ARG(err);
    
    DEBUG_ONLY({
        printf("TCP Server: Connection error %d\n", err);
    });
    
    if (conn) {
        conn->pcb = NULL; // PCB already deallocated by lwIP
        conn->active = false;
        g_server_stats.connection_errors++;
    }
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_ERROR, err);
}

/**
 * @brief TCP sent callback
 */
static err_t tcp_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len) {
    tcp_connection_t* conn = (tcp_connection_t*)arg;
    
    LWIP_UNUSED_ARG(tpcb);
    
    if (conn) {
        conn->bytes_sent += len;
        g_server_stats.bytes_sent += len;
    }
    
    return ERR_OK;
}

/**
 * @brief Find free connection slot
 */
static tcp_connection_t* find_free_connection(void) {
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (!g_connections[i].active) {
            return &g_connections[i];
        }
    }
    return NULL;
}

/**
 * @brief Find connection by PCB
 */
static tcp_connection_t* find_connection_by_pcb(struct tcp_pcb* pcb) {
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (g_connections[i].active && g_connections[i].pcb == pcb) {
            return &g_connections[i];
        }
    }
    return NULL;
}

/**
 * @brief Close connection
 */
static void close_connection(tcp_connection_t* conn) {
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
    conn->line_pos = 0;
    memset(conn->line_buffer, 0, sizeof(conn->line_buffer));
    
    DEBUG_ONLY({
        printf("TCP Server: Connection closed\n");
    });
}

/**
 * @brief Process received data for line-based protocol
 */
static int process_received_data(tcp_connection_t* conn, const char* data, size_t len) {
    if (!conn || !data || len == 0) {
        return 0;
    }
    
    // Update activity timestamp
    conn->last_activity_ms = to_ms_since_boot(get_absolute_time());
    
    // DEBUG: Print incoming raw data byte-by-byte in hex
    DEBUG_ONLY({
        printf("TCP recv %d bytes: ", len);

        for (size_t i = 0; i < len; i++) {
            printf("%02X ", (unsigned char)data[i]);
        }
        printf("\n");
    });
    
    int processed = 0;
    
    //copy to buffer
    //prevent overflow
    if( (conn->line_pos + len) > TCP_SERVER_LINE_BUFFER_SIZE ) {    
        len = TCP_SERVER_LINE_BUFFER_SIZE - conn->line_pos;
    }
    memcpy( &(conn->line_buffer[conn->line_pos]), data, len);
    conn->line_pos+=len;
    processed = len;

    bool message_complete = check_message_end(conn->line_buffer, conn->line_pos);
    if (message_complete) {
        processed = len;
        
        DEBUG_ONLY({
            printf("Message end detected! Buffer length: %zu\n", len);
        });
        err_t err = tcp_write(conn->pcb, conn->line_buffer, conn->line_pos, TCP_WRITE_FLAG_COPY);
        
        if (err == ERR_OK) {
            tcp_output(conn->pcb);
            g_server_stats.lines_processed++;
            DEBUG_ONLY({
                printf("TCP Server: Echoed message (%zu bytes) - SUCCESS\n", conn->line_pos);
            });
        } else {
            // Handle write error - close connection
            DEBUG_ONLY({
                printf("TCP Server: Write failed (err=%d) for %zu bytes, closing connection\n", err, conn->line_pos);
            });
            close_connection(conn);
        }
        
    }
    else {
        //message end not yes detected, this is normal, there is a tcp window, we will probably not receive messages > 538 bytes at once    
    }
    
    if(message_complete || (conn->line_pos >= TCP_SERVER_LINE_BUFFER_SIZE)) {
        DEBUG_ONLY({
            printf("TCP recv buffer RESET\n");
    });
        // Reset line buffer for next message
        conn->line_pos = 0;
        memset(conn->line_buffer, 0, sizeof(conn->line_buffer));
    }

    
    DEBUG_ONLY({
        printf("returning processed: %d\n", processed);
    });
    return processed;
}