/**
 * @file multi_tcp_server.h
 * @brief Multi-Port TCP Server Manager for UART2ETH
 * 
 * Manages multiple TCP socket servers simultaneously, one per UART channel.
 * Each server listens on its own port (4001-4004) and maps to UART channels 0-3.
 * 
 * Documentation Reference:
 * - Issue #82: Multi-channel TCP server support
 * - arc42 Chapter 5 - Network Building Block
 */

#ifndef MULTI_TCP_SERVER_H
#define MULTI_TCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "shared_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TCP_SERVERS 4  // Support up to 4 UART channels

/**
 * Initialize multi-port TCP server for enabled channels
 * @param channel Channel ID to initialize
 * @param port Port number for this channel
 * @return true if initialization successful
 */
bool multi_tcp_server_init_channel(channel_id_t channel, uint16_t port);

/**
 * Deinitialize specific TCP server channel
 * @param channel Channel to deinitialize
 */
void multi_tcp_server_deinit_channel(channel_id_t channel);

/**
 * Send message to specific channel's TCP connection
 * @param channel UART channel (0-3)
 * @param data Message data
 * @param length Message length
 * @return true if sent successfully
 */
bool multi_tcp_server_send_to_channel(channel_id_t channel, const uint8_t* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // MULTI_TCP_SERVER_H