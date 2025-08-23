/**
 * @file tcp_socket_server.h
 * @brief TCP Socket Server for UART2ETH Bridge using lwIP Raw API
 * 
 * Provides TCP server functionality with line-based protocol for UART bridging.
 * Uses lwIP Raw TCP API with callback-driven architecture.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - TCP Socket Server Building Block
 * - Issue #61: Add sockets to network implementation
 */

#ifndef TCP_SOCKET_SERVER_H
#define TCP_SOCKET_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "shared_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP server statistics structure
 */
typedef struct {
    uint16_t listen_port;           // Server listening port
    uint32_t active_connections;    // Current active connections
    uint32_t total_connections;     // Total connections since start
    uint32_t max_connections;       // Maximum concurrent connections
    uint64_t bytes_sent;            // Total bytes sent
    uint64_t bytes_received;        // Total bytes received
    uint32_t lines_processed;       // Total lines processed
    uint32_t connection_errors;     // Connection error count
} tcp_server_stats_t;

/**
 * @brief TCP Socket Server API
 */

/**
 * Initialize TCP socket server
 * @param port TCP port to listen on (e.g., 4001)
 * @return true if initialization successful, false otherwise
 */
bool tcp_socket_server_init(uint16_t port);

/**
 * Deinitialize TCP socket server
 */
void tcp_socket_server_deinit(void);

/**
 * Process TCP server tasks (call from Core1 main loop)
 */
void tcp_socket_server_process(void);

/**
 * Check if server is listening for connections
 * @return true if server is listening, false otherwise
 */
bool tcp_socket_server_is_listening(void);

/**
 * Get current server statistics
 * @param stats Output structure for server statistics
 */
void tcp_socket_server_get_stats(tcp_server_stats_t* stats);

/**
 * Reset server statistics counters
 */
void tcp_socket_server_reset_stats(void);

/**
 * Process line-based protocol (internal function for testing)
 * @param input Input line data
 * @param input_len Length of input data
 * @param output Output buffer for echo response
 * @param output_size Size of output buffer
 * @return Number of bytes written to output, -1 on error
 */
int tcp_socket_server_process_line(const char* input, size_t input_len, 
                                   char* output, size_t output_size);

/**
 * Check if buffer ends with exactly '!\r\n'
 * @param buffer Input buffer to check
 * @param length Length of buffer
 * @return true if buffer ends with '!\r\n', false otherwise
 */
bool check_message_end(const char* buffer, size_t length);

/**
 * Send message to TCP connection associated with specific UART channel
 * @param channel UART channel (0-3) to send message to
 * @param data Message data to send
 * @param length Length of message data
 * @return true if message sent successfully, false if no connection or send failed
 */
bool tcp_socket_server_send_to_channel(channel_id_t channel, const uint8_t* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // TCP_SOCKET_SERVER_H