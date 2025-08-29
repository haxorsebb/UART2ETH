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
#include "ringbuffer.h"
#include "debug.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "state_machine.h"
#include <string.h>
#include <stdio.h>

// Connection Management Policy:
// Single Connection Mode - Only one active connection allowed at a time
// When a new connection arrives, all existing connections are closed
// This ensures one server handles all connections sequentially
#define TCP_SERVER_MAX_CONNECTIONS 4  // Pool size (only 1 active due to single connection policy)
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
    channel_id_t channel;                          // Associated UART channel (0-3)
    uint32_t bytes_sent;                          // Bytes sent on this connection
    uint32_t bytes_received;                      // Bytes received on this connection
    uint32_t last_activity_ms;                    // For timeout detection
} tcp_connection_t;

#define MAX_TCP_SERVERS 4  // Support up to 4 UART channels

/**
 * @brief TCP server instance structure
 */
typedef struct {
    struct tcp_pcb* server_pcb;
    tcp_connection_t connections[TCP_SERVER_MAX_CONNECTIONS];
    tcp_server_stats_t stats;
    uint16_t listen_port;
    channel_id_t channel;
    bool initialized;
} tcp_server_instance_t;

// Multi-server state
static tcp_server_instance_t g_servers[MAX_TCP_SERVERS];
static bool g_multi_server_initialized = false;

// Forward declarations
static err_t tcp_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
static err_t tcp_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
static void tcp_connection_error_callback(void* arg, err_t err);
static err_t tcp_connection_sent_callback(void* arg, struct tcp_pcb* tpcb, u16_t len);
static void close_connection(tcp_connection_t* conn);
static int process_received_data(tcp_connection_t* conn, const char* data, size_t len);

/**
 * @brief Initialize TCP socket server on specified port
 * @param port TCP port to listen on (e.g., 4001)
 * @return true if initialization successful, false otherwise
 * @note Requires network manager to be initialized (not ERROR/UNINITIALIZED)
 */
bool tcp_socket_server_init(uint16_t port, channel_id_t channel) {
    // Initialize multi-server system on first call
    if (!g_multi_server_initialized) {
        memset(g_servers, 0, sizeof(g_servers));
        g_multi_server_initialized = true;
        DEBUG_ONLY({
            printf("TCP Multi-Server: System initialized\n");
        });
    }
    
    // Find free server slot
    tcp_server_instance_t* server = NULL;
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (!g_servers[i].initialized) {
            server = &g_servers[i];
            break;
        }
    }
    
    if (!server) {
        DEBUG_ONLY({
            printf("TCP Server: No free server slots available\n");
        });
        return false;
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
        printf("TCP Server: Initializing on port %u for channel %u\n", port, channel);
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, port);
    
    // Create new TCP PCB
    server->server_pcb = tcp_new();
    if (!server->server_pcb) {
        DEBUG_ONLY({
            printf("TCP Server: Failed to create PCB for port %u\n", port);
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_PCB_CREATION_FAILED);
        return false;
    }
    
    // Bind to port
    err_t err = tcp_bind(server->server_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        DEBUG_ONLY({
            printf("TCP Server: Failed to bind to port %u (error %d)\n", port, err);
        });
        tcp_close(server->server_pcb);
        server->server_pcb = NULL;
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_BIND_FAILED);
        return false;
    }
    
    // Start listening
    server->server_pcb = tcp_listen(server->server_pcb);
    if (!server->server_pcb) {
        DEBUG_ONLY({
            printf("TCP Server: Failed to listen on port %u\n", port);
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, TCP_ERROR_LISTEN_FAILED);
        return false;
    }
    
    // Set accept callback with server instance as argument
    tcp_arg(server->server_pcb, server);
    tcp_accept(server->server_pcb, tcp_server_accept_callback);
    
    // Initialize connection pool
    memset(server->connections, 0, sizeof(server->connections));
    
    // Initialize statistics
    memset(&server->stats, 0, sizeof(tcp_server_stats_t));
    server->stats.listen_port = port;
    server->stats.max_connections = TCP_SERVER_MAX_CONNECTIONS;
    
    server->listen_port = port;
    server->channel = channel;
    server->initialized = true;
    
    DEBUG_ONLY({
        printf("TCP Server: Listening on port %u for channel %u\n", port, channel);
    });
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, port);
    
    return true;
}

/**
 * @brief Deinitialize all TCP socket servers and cleanup all resources
 * @note Closes all active connections and resets all server state
 */
void tcp_socket_server_deinit(void) {
    if (!g_multi_server_initialized) {
        return;
    }
    
    DEBUG_ONLY({
        printf("TCP Multi-Server: Deinitializing all servers\n");
    });
    
    // Close all servers
    for (int srv = 0; srv < MAX_TCP_SERVERS; srv++) {
        tcp_server_instance_t* server = &g_servers[srv];
        if (!server->initialized) {
            continue;
        }
        
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, server->listen_port);
        
        // Close all active connections for this server
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
        server->listen_port = 0;
        server->channel = CHANNEL_MAX;
    }
    
    g_multi_server_initialized = false;
    
    DEBUG_ONLY({
        printf("TCP Multi-Server: All servers deinitialized\n");
    });
}

/**
 * @brief Deinitialize specific TCP socket server by port
 * @param port TCP port to deinitialize
 */
void tcp_socket_server_deinit_port(uint16_t port) {
    if (!g_multi_server_initialized) {
        return;
    }
    
    // Find server by port
    tcp_server_instance_t* server = NULL;
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (g_servers[i].initialized && g_servers[i].listen_port == port) {
            server = &g_servers[i];
            break;
        }
    }
    
    if (!server) {
        return;
    }
    
    DEBUG_ONLY({
        printf("TCP Server: Deinitializing port %d\n", port);
    });
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, port);
    
    // Close all active connections for this server
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
    server->listen_port = 0;
    server->channel = CHANNEL_MAX;
    
    DEBUG_ONLY({
        printf("TCP Server: Port %d deinitialized\n", port);
    });
}

/**
 * @brief Process TCP server tasks
 * 
 * TCP Server Processing:
 * - Handle lwIP TCP stack callbacks and connection management
 * - TCP packet processing is handled by lwIP callbacks
 * - Ringbuffer processing is handled by Core1 main loop
 * 
 * Note: Ringbuffer message sending is now handled by Core1 main loop
 * via core1_process_ringbuffer() -> tcp_socket_server_send_to_channel()
 */
void tcp_socket_server_process(void) {
    // TCP server processing is primarily handled by lwIP callbacks
    // This function is kept for future TCP server management tasks
    // such as connection timeout handling, statistics updates, etc.
    
    // For now, this function is mainly a placeholder for future TCP server tasks
    // The main TCP processing happens in the lwIP callbacks:
    // - tcp_server_accept_callback() - handles new connections
    // - tcp_connection_recv_callback() - handles incoming data
    // - tcp_connection_error_callback() - handles connection errors
    // - tcp_connection_sent_callback() - handles sent data confirmation
}

/**
 * @brief Check if any server is listening
 */
bool tcp_socket_server_is_listening(void) {
    if (!g_multi_server_initialized) {
        return false;
    }
    
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (g_servers[i].initialized && g_servers[i].server_pcb != NULL) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Get aggregated server statistics from all servers
 */
void tcp_socket_server_get_stats(tcp_server_stats_t* stats) {
    if (!stats || !g_multi_server_initialized) {
        return;
    }
    
    memset(stats, 0, sizeof(tcp_server_stats_t));
    
    // Aggregate stats from all servers
    for (int srv = 0; srv < MAX_TCP_SERVERS; srv++) {
        tcp_server_instance_t* server = &g_servers[srv];
        if (!server->initialized) {
            continue;
        }
        
        // Update active connections count for this server
        server->stats.active_connections = 0;
        for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
            if (server->connections[i].active) {
                server->stats.active_connections++;
            }
        }
        
        // Aggregate stats
        stats->active_connections += server->stats.active_connections;
        stats->total_connections += server->stats.total_connections;
        if (server->stats.max_connections > stats->max_connections) {
            stats->max_connections = server->stats.max_connections;
        }
        stats->bytes_sent += server->stats.bytes_sent;
        stats->bytes_received += server->stats.bytes_received;
        stats->lines_processed += server->stats.lines_processed;
        stats->connection_errors += server->stats.connection_errors;
        
        // Use port from first initialized server
        if (stats->listen_port == 0) {
            stats->listen_port = server->listen_port;
        }
    }
}

/**
 * @brief Reset all server statistics
 */
void tcp_socket_server_reset_stats(void) {
    if (!g_multi_server_initialized) {
        return;
    }
    
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (g_servers[i].initialized) {
            uint16_t port = g_servers[i].stats.listen_port;
            uint32_t max_conn = g_servers[i].stats.max_connections;
            
            memset(&g_servers[i].stats, 0, sizeof(tcp_server_stats_t));
            g_servers[i].stats.listen_port = port;
            g_servers[i].stats.max_connections = max_conn;
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
    
    return buffer[length-3] == '!' && 
           buffer[length-2] == '\r' && 
           buffer[length-1] == '\n';
}

/**
 * @brief Send message to TCP connection associated with specific UART channel
 * 
 * Since we now use single connection policy, this function sends to the
 * currently active connection regardless of UART channel mapping.
 */
bool tcp_socket_server_send_to_channel(channel_id_t channel, const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return false;
    }
    
    // Find server instance for this channel
    tcp_server_instance_t* server = NULL;
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (g_servers[i].initialized && g_servers[i].channel == channel) {
            server = &g_servers[i];
            break;
        }
    }
    
    if (!server) {
        DEBUG_ONLY({
            printf("TCP Server: No server found for channel %u\n", channel);
        });
        return false;
    }
    
    // Find active connection for this server
    for (int i = 0; i < TCP_SERVER_MAX_CONNECTIONS; i++) {
        if (server->connections[i].active && server->connections[i].pcb) {
            DEBUG_ONLY({
                printf("TCP Server: Sending %zu bytes to channel %u (port %u)\n", length, channel, server->listen_port);
            });
            
            err_t err = tcp_write(server->connections[i].pcb, data, length, TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                tcp_output(server->connections[i].pcb);
                server->connections[i].bytes_sent += length;
                server->stats.bytes_sent += length;
                return true;
            } else {
                DEBUG_ONLY({
                    printf("TCP Server: Failed to send data (error %d)\n", err);
                });
                return false;
            }
        }
    }
    
    DEBUG_ONLY({
        printf("TCP Server: No active connection for channel %u\n", channel);
    });
    return false;
}

// Private function implementations

/**
 * @brief TCP accept callback
 * 
 * Single Connection Policy:
 * - Only one active connection allowed at a time
 * - Close all existing connections when a new one arrives
 * - This ensures one server handles all connections sequentially
 */
static err_t tcp_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err) {
    tcp_server_instance_t* server = (tcp_server_instance_t*)arg;
    
    if (err != ERR_OK || !newpcb || !server) {
        return ERR_VAL;
    }
    
    DEBUG_ONLY({
        printf("TCP Server: New connection on port %u (channel %u)\n", server->listen_port, server->channel);
    });
    
    // Single Connection Policy: Close all existing connections for this server
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
        DEBUG_ONLY({
            printf("TCP Server: No free connection slots for port %u\n", server->listen_port);
        });
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
    conn->channel = server->channel;  // Use server's channel mapping
    
    // Clear buffer with known pattern for debugging
    memset(conn->line_buffer, 0xAA, sizeof(conn->line_buffer));
    conn->line_buffer[sizeof(conn->line_buffer)-1] = '\0';
    
    // Set callbacks
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, tcp_connection_recv_callback);
    tcp_err(newpcb, tcp_connection_error_callback);
    tcp_sent(newpcb, tcp_connection_sent_callback);
    
    // Update statistics
    server->stats.total_connections++;
    
    DEBUG_ONLY({
        printf("TCP Server: Connection accepted on port %u -> channel %u\n", server->listen_port, server->channel);
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

        if (processed > 0) {
            conn->bytes_received += processed;
            // Update server stats - need to find server by channel
            for (int srv = 0; srv < MAX_TCP_SERVERS; srv++) {
                if (g_servers[srv].initialized && g_servers[srv].channel == conn->channel) {
                    g_servers[srv].stats.bytes_received += processed;
                    break;
                }
            }
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
        // Update server stats - need to find server by channel
        for (int srv = 0; srv < MAX_TCP_SERVERS; srv++) {
            if (g_servers[srv].initialized && g_servers[srv].channel == conn->channel) {
                g_servers[srv].stats.connection_errors++;
                break;
            }
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
        // Update server stats - need to find server by channel
        for (int srv = 0; srv < MAX_TCP_SERVERS; srv++) {
            if (g_servers[srv].initialized && g_servers[srv].channel == conn->channel) {
                g_servers[srv].stats.bytes_sent += len;
                break;
            }
        }
    }
    
    return ERR_OK;
}

// NOTE: The helper functions find_free_connection, find_connection_by_pcb, 
// and close_all_connections have been integrated into the server-specific 
// logic since each server now manages its own connections array.

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
            printf("Message end detected! Buffer length: %zu\n", conn->line_pos);
        });
        
        // Ring Buffer Integration (Issue #68): Enqueue TCP messages for Core0 processing
        // Instead of direct echo, enqueue message with direction RX_TCP_TO_UART
        ring_entry_t* entry = ringbuffer_get_free_entry(RX_TCP_TO_UART,conn->channel);
        // Setup ring buffer entry
        entry->fill_index = conn->line_pos;
        
        // Copy message data to ring buffer
        if (entry->fill_index > RINGBUFFER_PAYLOAD_MAX_SIZE) {
            entry->fill_index = RINGBUFFER_PAYLOAD_MAX_SIZE;
        }
        memcpy(entry->payload, conn->line_buffer, entry->fill_index);
        
        // Enqueue for Core0 processing
        bool enqueue_result = ringbuffer_enqueue_entry(entry);
        if (enqueue_result) {
            // Update server stats - need to find server by channel
            for (int srv = 0; srv < MAX_TCP_SERVERS; srv++) {
                if (g_servers[srv].initialized && g_servers[srv].channel == conn->channel) {
                    g_servers[srv].stats.lines_processed++;
                    break;
                }
            }
            DEBUG_ONLY({
                printf("TCP Server: Message enqueued for Core0 (%zu bytes) - SUCCESS\n", conn->line_pos);
            });
        } else {
            DEBUG_ONLY({
                printf("TCP Server: Failed to enqueue message for Core0\n");
            });
        }
    }
    else {
        //message end not yet detected, this is normal, there is a tcp window, we will probably not receive messages > 538 bytes at once    
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