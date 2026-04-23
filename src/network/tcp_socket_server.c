/**
 * @file tcp_socket_server.c
 * @brief TCP Socket Server Implementation using lwIP Raw API (MULTI-INSTANCE VERSION)
 * 
 * Implements TCP server with line-based protocol for UART bridging.
 * Uses lwIP Raw TCP API callbacks for event-driven networking.
 * Supports multiple concurrent server instances.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - TCP Socket Server Building Block
 * - Issue #82: Multi-channel TCP server support
 */

#include "network/tcp_socket_server.h"
#include "network/network_manager.h"
#include "log_manager.h"
#include "ringbuffer.h"
#include "debug.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "state_machine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Connection Management Policy:
// Single Connection Mode - Only one active connection allowed per server
// When a new connection arrives, all existing connections on that server are closed
#define TCP_SERVER_MAX_CONNECTIONS 4  // Pool size per server
#define TCP_SERVER_LINE_BUFFER_SIZE 1024
#define MINIMUM_MESSAGE_LENGTH 2  // minimum: one character + '\n'
#define MAX_TCP_SERVER_INSTANCES 5  // Maximum number of concurrent servers (one per channel)

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
    channel_id_t channel;                          // Associated UART channel (0-3)
    uint32_t bytes_sent;                          // Bytes sent on this connection
    uint32_t bytes_received;                      // Bytes received on this connection
    uint32_t last_activity_ms;                    // For timeout detection
    struct tcp_server_instance* server_instance;  // Back pointer to server
} tcp_connection_t;

/**
 * @brief TCP server instance structure
 */
typedef struct tcp_server_instance {
    struct tcp_pcb* server_pcb;                   // lwIP TCP PCB for listening
    tcp_connection_t connections[TCP_SERVER_MAX_CONNECTIONS]; // Connection pool
    tcp_server_stats_t stats;                     // Server statistics
    uint16_t listen_port;                         // Listening port
    channel_id_t server_channel;                  // Associated UART channel
    bool initialized;                             // Initialization flag
    bool active;                                  // Active flag
} tcp_server_instance_t;

// Multi-instance server pool
static tcp_server_instance_t g_server_instances[MAX_TCP_SERVER_INSTANCES];
static bool g_multi_server_initialized = false;

// Forward declarations
static tcp_server_instance_t* find_free_server_instance(void);
static tcp_server_instance_t* find_server_by_port(uint16_t port);
static tcp_server_instance_t* find_server_by_channel(channel_id_t channel);
static err_t tcp_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
static err_t tcp_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
static void tcp_connection_error_callback(void* arg, err_t err);
static err_t tcp_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len);
static void close_connection(tcp_connection_t* conn);
static int process_received_data(tcp_connection_t* conn, const char* data, size_t len);
static void init_multi_server_system(void);

/**
 * @brief Initialize multi-server system
 */
static void init_multi_server_system(void) {
    if (g_multi_server_initialized) {
        return;
    }
    
    // Initialize all server instances
    memset(g_server_instances, 0, sizeof(g_server_instances));
    
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        g_server_instances[i].server_pcb = NULL;
        g_server_instances[i].listen_port = 0;
        g_server_instances[i].server_channel = CHANNEL_MAX;
        g_server_instances[i].initialized = false;
        g_server_instances[i].active = false;
        
        // Initialize connections and link back to server
        for (int j = 0; j < TCP_SERVER_MAX_CONNECTIONS; j++) {
            g_server_instances[i].connections[j].server_instance = &g_server_instances[i];
        }
    }
    
    g_multi_server_initialized = true;
    printf("Multi-TCP Server: System initialized for %d concurrent servers\n", MAX_TCP_SERVER_INSTANCES);
}

/**
 * @brief Find a free server instance
 */
static tcp_server_instance_t* find_free_server_instance(void) {
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        if (!g_server_instances[i].initialized) {
            return &g_server_instances[i];
        }
    }
    return NULL;
}

/**
 * @brief Find server by port
 */
static tcp_server_instance_t* find_server_by_port(uint16_t port) {
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        if (g_server_instances[i].initialized && g_server_instances[i].listen_port == port) {
            return &g_server_instances[i];
        }
    }
    return NULL;
}

/**
 * @brief Find server by channel
 */
static tcp_server_instance_t* find_server_by_channel(channel_id_t channel) {
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        if (g_server_instances[i].initialized && g_server_instances[i].server_channel == channel) {
            return &g_server_instances[i];
        }
    }
    return NULL;
}

/**
 * @brief Initialize TCP socket server (MULTI-INSTANCE VERSION)
 * @param port TCP port to listen on (e.g., 4002)
 * @param channel UART channel this server maps to (0-3)
 * @return true if initialization successful, false otherwise
 */
bool tcp_socket_server_init(uint16_t port, channel_id_t channel) {
    init_multi_server_system();
    
    // Check if server already exists for this port
    tcp_server_instance_t* existing = find_server_by_port(port);
    if (existing) {
        printf("TCP Server: Already initialized on port %u for channel %u\n", port, existing->server_channel);
        return true;
    }
    
    // Check network is initialized
    network_status_t status = network_manager_get_status();
    if (status == NETWORK_STATUS_UNINITIALIZED || status == NETWORK_STATUS_ERROR) {
        printf("TCP Server: Network not initialized (status=%d)\n", status);
        return false;
    }
    
    // Find free server instance
    tcp_server_instance_t* server = find_free_server_instance();
    if (!server) {
        printf("TCP Server: No free server instances available (max %d)\n", MAX_TCP_SERVER_INSTANCES);
        return false;
    }
    
    printf("TCP Server: Initializing on port %u for channel %u\n", port, channel);
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, port);
    
    // Create new TCP PCB
    server->server_pcb = tcp_new();
    if (!server->server_pcb) {
        printf("TCP Server: Failed to create PCB for port %u\n", port);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_PCB_CREATION_FAILED);
        return false;
    }
    
    // Bind to port
    err_t err = tcp_bind(server->server_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        printf("TCP Server: Failed to bind to port %u (error %d)\n", port, err);
        tcp_close(server->server_pcb);
        server->server_pcb = NULL;
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_BIND_FAILED);
        return false;
    }
    
    // Start listening
    server->server_pcb = tcp_listen(server->server_pcb);
    if (!server->server_pcb) {
        printf("TCP Server: Failed to listen on port %u\n", port);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_LISTEN_FAILED);
        return false;
    }
    
    // Set accept callback with server instance as context
    tcp_arg(server->server_pcb, server);
    tcp_accept(server->server_pcb, tcp_server_accept_callback);
    
    // Initialize connection pool
    memset(server->connections, 0, sizeof(server->connections));
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        server->connections[i].server_instance = server;
    }
    
    // Initialize statistics
    memset(&server->stats, 0, sizeof(tcp_server_stats_t));
    server->stats.listen_port = port;
    server->stats.max_connections = TCP_SERVER_MAX_CONNECTIONS;
    
    server->listen_port = port;
    server->server_channel = channel;
    server->initialized = true;
    server->active = true;
    
    printf("TCP Server: Listening on port %u for channel %u\n", port, channel);
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, port);
    
    return true;
}

/**
 * @brief Deinitialize all TCP socket servers
 */
void tcp_socket_server_deinit(void) {
    if (!g_multi_server_initialized) {
        return;
    }
    
    printf("TCP Server: Deinitializing all servers\n");
    
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        tcp_server_instance_t* server = &g_server_instances[i];
        if (server->initialized) {
            tcp_socket_server_deinit_port(server->listen_port);
        }
    }
    
    g_multi_server_initialized = false;
    printf("TCP Server: All servers deinitialized\n");
}

/**
 * @brief Deinitialize specific TCP socket server by port
 */
void tcp_socket_server_deinit_port(uint16_t port) {
    tcp_server_instance_t* server = find_server_by_port(port);
    if (!server) {
        return;
    }
    
    printf("TCP Server: Deinitializing server on port %u\n", port);
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, port);
    
    // Close all active connections
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (server->connections[i].active) {
            close_connection(&server->connections[i]);
        }
    }
    
    // Close server PCB
    if (server->server_pcb) {
        tcp_close(server->server_pcb);
        server->server_pcb = NULL;
    }
    
    // Reset server state
    memset(server->connections, 0, sizeof(server->connections));
    memset(&server->stats, 0, sizeof(tcp_server_stats_t));
    server->initialized = false;
    server->active = false;
    server->listen_port = 0;
    server->server_channel = CHANNEL_MAX;
    
    printf("TCP Server: Server on port %u deinitialized\n", port);
}

/**
 * @brief Process TCP server tasks
 */
void tcp_socket_server_process(void) {
    // TCP server processing is primarily handled by lwIP callbacks
    // This function is kept for future TCP server management tasks
}

/**
 * @brief Check if any server is listening
 */
bool tcp_socket_server_is_listening(void) {
    if (!g_multi_server_initialized) {
        return false;
    }
    
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        if (g_server_instances[i].initialized && 
            g_server_instances[i].active && 
            g_server_instances[i].server_pcb != NULL) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Get server statistics (returns first active server's stats)
 */
void tcp_socket_server_get_stats(tcp_server_stats_t* stats) {
    if (!stats || !g_multi_server_initialized) {
        if (stats) {
            memset(stats, 0, sizeof(tcp_server_stats_t));
        }
        return;
    }
    
    // Find first active server and return its stats
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        tcp_server_instance_t* server = &g_server_instances[i];
        if (server->initialized && server->active) {
            // Update active connections count
            server->stats.active_connections = 0;
            for (int j = 0; j < TCP_SERVER_MAX_CONNECTIONS; j++) {
                if (server->connections[j].active) {
                    server->stats.active_connections++;
                }
            }
            
            memcpy(stats, &server->stats, sizeof(tcp_server_stats_t));
            return;
        }
    }
    
    // No active servers
    memset(stats, 0, sizeof(tcp_server_stats_t));
}

/**
 * @brief Reset server statistics
 */
void tcp_socket_server_reset_stats(void) {
    if (!g_multi_server_initialized) {
        return;
    }
    
    for (int i = 0; i < MAX_TCP_SERVER_INSTANCES; i++) {
        tcp_server_instance_t* server = &g_server_instances[i];
        if (server->initialized) {
            uint16_t port = server->stats.listen_port;
            uint32_t max_conn = server->stats.max_connections;
            
            memset(&server->stats, 0, sizeof(tcp_server_stats_t));
            server->stats.listen_port = port;
            server->stats.max_connections = max_conn;
        }
    }
    
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
    
    // Any packet ending with '\n' is valid
    return buffer[length-1] == '\n';
}

/**
 * @brief Send message to TCP connection for specific channel
 */
bool tcp_socket_server_send_to_channel(channel_id_t channel, const uint8_t* data, size_t length) {
    if (!data || length == 0 || !g_multi_server_initialized) {
        return false;
    }
    
    // Find server for this channel
    tcp_server_instance_t* server = find_server_by_channel(channel);
    if (!server || !server->active) {
        return false;
    }
    
    // Find active connection on this server
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        tcp_connection_t* conn = &server->connections[i];
        if (conn->active && conn->pcb) {
            err_t err = tcp_write(conn->pcb, data, length, TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                tcp_output(conn->pcb);
                conn->bytes_sent += length;
                server->stats.bytes_sent += length;
                return true;
            } else {
                printf("TCP Server: tcp_write error %d on channel %u - closing connection\n",
                    err, channel);
                // ERR_CLSD (-14) or other fatal errors mean the connection is gone.
                // Clean up so new connections can be accepted on this channel.
                if (err == ERR_CLSD || err == ERR_RST || err == ERR_ABRT) {
                    close_connection(conn);
                }                
                return false;
            }
        }
    }
    return false;
}

// Private function implementations

/**
 * @brief TCP accept callback
 */
static err_t tcp_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err) {
    tcp_server_instance_t* server = (tcp_server_instance_t*)arg;
    
    if (err != ERR_OK || !newpcb || !server) {
        return ERR_VAL;
    }
    
    printf("TCP Server: New connection on port %u (channel %u)\n", 
           server->listen_port, server->server_channel);
    
    // Single Connection Policy per server: Close all existing connections
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (server->connections[i].active) {
            close_connection(&server->connections[i]);
        }
    }
    
    // Find free connection slot
    tcp_connection_t* conn = NULL;
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (!server->connections[i].active) {
            conn = &server->connections[i];
            break;
        }
    }
    
    if (!conn) {
        printf("TCP Server: No free connection slots on port %u\n", server->listen_port);
        server->stats.connection_errors++;
        tcp_close(newpcb);
        return ERR_MEM;
    }
    
    // Initialize connection
    conn->pcb = newpcb;
    conn->line_pos = 0;
    conn->active = true;
    conn->bytes_sent = 0;
    conn->bytes_received = 0;
    conn->channel = server->server_channel;  // Use server's channel
    
    // Clear buffer
    memset(conn->line_buffer, 0, sizeof(conn->line_buffer));
    
    // Set callbacks
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, tcp_connection_recv_callback);
    tcp_err(newpcb, tcp_connection_error_callback);
    tcp_sent(newpcb, tcp_connection_sent_callback);
    
    // Update statistics
    server->stats.total_connections++;
    
    printf("TCP Server: Connection accepted on port %u -> channel %u\n", 
           server->listen_port, server->server_channel);
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
        close_connection(conn);
        return ERR_OK;
    }
    
    // Process received data
    struct pbuf *q = p;
    uint16_t total_len = p->tot_len;
    int processed = 0;

    while (q != NULL) {
        processed += process_received_data(conn, (const char*)q->payload, q->len);
        if (processed > 0) {
            conn->bytes_received += processed;
            conn->server_instance->stats.bytes_received += processed;
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
    
    printf("TCP Server: Connection error %d on channel %u\n", 
           err, conn ? conn->channel : 999);
    
    if (conn) {
        conn->pcb = NULL; // PCB already deallocated by lwIP
        conn->active = false;
        if (conn->server_instance) {
            conn->server_instance->stats.connection_errors++;
        }
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
        if (conn->server_instance) {
            conn->server_instance->stats.bytes_sent += len;
        }
    }
    
    return ERR_OK;
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
        
        // Ring Buffer Integration: Enqueue TCP messages for Core0 processing
        ring_entry_t* entry = ringbuffer_get_free_entry(RX_TCP_TO_UART, conn->channel);
        
        if (entry) {
            entry->fill_index = conn->line_pos;
            
            // Copy message data to ring buffer
            if (entry->fill_index > RINGBUFFER_PAYLOAD_MAX_SIZE) {
                entry->fill_index = RINGBUFFER_PAYLOAD_MAX_SIZE;
            }
            memcpy(entry->payload, conn->line_buffer, entry->fill_index);
            
            // Enqueue for Core0 processing
            if (ringbuffer_enqueue_entry(entry)) {
                if (conn->server_instance) {
                    conn->server_instance->stats.lines_processed++;
                }
            }
        }
    }
    
    if(message_complete || (conn->line_pos >= TCP_SERVER_LINE_BUFFER_SIZE)) {
        // Reset line buffer for next message
        conn->line_pos = 0;
    }

    return processed;
}