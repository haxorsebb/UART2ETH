/**
 * @file multi_tcp_server.c
 * @brief Multi-Port TCP Server Manager Implementation
 * 
 * Manages multiple TCP socket servers simultaneously, one per UART channel.
 * Each server listens on its own port (4001-4004) and maps to UART channels 0-3.
 * 
 * Documentation Reference:
 * - Issue #82: Multi-channel TCP server support
 * - arc42 Chapter 5 - Network Building Block
 */

#include "network/multi_tcp_server.h"
#include "network/tcp_socket_server.h"
#include "network/network_manager.h"
#include "log_manager.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

// Multi-server state
typedef struct {
    bool initialized;
    uint16_t port;
    channel_id_t channel;
    bool active;
} tcp_server_instance_t;

static tcp_server_instance_t tcp_servers[MAX_TCP_SERVERS];
static bool multi_tcp_server_initialized = false;

// Forward declarations
static void multi_tcp_server_deinit_all_internal(void);

/**
 * Initialize multi-port TCP server system
 */
static bool multi_tcp_server_init_system(void) {
    if (multi_tcp_server_initialized) {
        return true;
    }
    
    // Initialize all server instances
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        tcp_servers[i].initialized = false;
        tcp_servers[i].port = 0;
        tcp_servers[i].channel = CHANNEL_MAX;
        tcp_servers[i].active = false;
    }
    
    multi_tcp_server_initialized = true;
    printf("Multi-TCP Server: System initialized\n");
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 0);
    
    return true;
}

/**
 * Initialize multi-port TCP server for enabled channels
 */
bool multi_tcp_server_init_channel(channel_id_t channel, uint16_t port) {
    if (channel >= MAX_TCP_SERVERS) {
        printf("Multi-TCP Server: Invalid channel %u (max %d)\n", channel, MAX_TCP_SERVERS - 1);
        return false;
    }
    
    if (!multi_tcp_server_initialized) {
        if (!multi_tcp_server_init_system()) {
            printf("Multi-TCP Server: Failed to initialize system\n");
            return false;
        }
    }
    
    // Check if channel already initialized
    if (tcp_servers[channel].initialized) {
        printf("Multi-TCP Server: Channel %u already initialized on port %u\n", 
               channel, tcp_servers[channel].port);
        return true;
    }
    
    printf("Multi-TCP Server: Initializing channel %u on port %u\n", channel, port);
    
    // Since we're using the singleton tcp_socket_server, we need to deinitialize
    // any existing server first, then initialize the new one
    tcp_socket_server_deinit();
    
    // Initialize the TCP server for this channel
    bool result = tcp_socket_server_init(port, channel);
    
    if (result) {
        tcp_servers[channel].initialized = true;
        tcp_servers[channel].port = port;
        tcp_servers[channel].channel = channel;
        tcp_servers[channel].active = true;
        
        printf("Multi-TCP Server: Channel %u initialized successfully on port %u\n", channel, port);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, port);
        
        return true;
    } else {
        printf("Multi-TCP Server: Failed to initialize channel %u on port %u\n", channel, port);
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, port);
        
        return false;
    }
}

/**
 * Deinitialize specific TCP server channel
 */
void multi_tcp_server_deinit_channel(channel_id_t channel) {
    if (channel >= MAX_TCP_SERVERS || !multi_tcp_server_initialized) {
        return;
    }
    
    if (!tcp_servers[channel].initialized) {
        return;  // Channel not initialized
    }
    
    printf("Multi-TCP Server: Deinitializing channel %u (port %u)\n", 
           channel, tcp_servers[channel].port);
    
    // Since we're using singleton, deinit the current server if it matches this channel
    if (tcp_servers[channel].active) {
        tcp_socket_server_deinit_port(tcp_servers[channel].port);
        tcp_servers[channel].active = false;
    }
    
    tcp_servers[channel].initialized = false;
    tcp_servers[channel].port = 0;
    tcp_servers[channel].channel = CHANNEL_MAX;
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, channel);
}

/**
 * Send message to specific channel's TCP connection
 */
bool multi_tcp_server_send_to_channel(channel_id_t channel, const uint8_t* data, size_t length) {
    if (channel >= MAX_TCP_SERVERS || !multi_tcp_server_initialized) {
        return false;
    }
    
    if (!tcp_servers[channel].initialized || !tcp_servers[channel].active) {
        printf("Multi-TCP Server: Channel %u not initialized or active\n", channel);
        return false;
    }
    
    // Use the singleton server's send function
    return tcp_socket_server_send_to_channel(channel, data, length);
}

/**
 * Get statistics for all active servers
 */
void multi_tcp_server_get_all_stats(void) {
    if (!multi_tcp_server_initialized) {
        printf("Multi-TCP Server: System not initialized\n");
        return;
    }
    
    printf("=== Multi-TCP Server Status ===\n");
    
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (tcp_servers[i].initialized) {
            printf("Channel %d: Port %u, %s\n", 
                   i, tcp_servers[i].port, 
                   tcp_servers[i].active ? "ACTIVE" : "INACTIVE");
            
            if (tcp_servers[i].active) {
                tcp_server_stats_t stats;
                tcp_socket_server_get_stats(&stats);
                printf("  - Active connections: %u\n", stats.active_connections);
                printf("  - Total connections: %u\n", stats.total_connections);
                printf("  - Bytes sent: %llu\n", stats.bytes_sent);
                printf("  - Bytes received: %llu\n", stats.bytes_received);
            }
        } else {
            printf("Channel %d: Not initialized\n", i);
        }
    }
    printf("===============================\n");
}

/**
 * Check if any server is listening
 */
bool multi_tcp_server_is_any_listening(void) {
    if (!multi_tcp_server_initialized) {
        return false;
    }
    
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (tcp_servers[i].initialized && tcp_servers[i].active) {
            if (tcp_socket_server_is_listening()) {
                return true;
            }
        }
    }
    
    return false;
}

/**
 * Switch active server to a different channel
 * This is a workaround for the singleton limitation
 */
bool multi_tcp_server_switch_active_channel(channel_id_t new_channel) {
    if (new_channel >= MAX_TCP_SERVERS || !multi_tcp_server_initialized) {
        return false;
    }
    
    if (!tcp_servers[new_channel].initialized) {
        printf("Multi-TCP Server: Cannot switch to uninitialized channel %u\n", new_channel);
        return false;
    }
    
    // Find current active channel
    channel_id_t current_active = CHANNEL_MAX;
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (tcp_servers[i].active) {
            current_active = i;
            break;
        }
    }
    
    if (current_active == new_channel) {
        printf("Multi-TCP Server: Channel %u already active\n", new_channel);
        return true;  // Already active
    }
    
    printf("Multi-TCP Server: Switching from channel %u to channel %u\n", 
           current_active, new_channel);
    
    // Deactivate current server
    if (current_active != CHANNEL_MAX) {
        tcp_socket_server_deinit();
        tcp_servers[current_active].active = false;
    }
    
    // Activate new server
    bool result = tcp_socket_server_init(tcp_servers[new_channel].port, new_channel);
    
    if (result) {
        tcp_servers[new_channel].active = true;
        printf("Multi-TCP Server: Successfully switched to channel %u (port %u)\n", 
               new_channel, tcp_servers[new_channel].port);
        return true;
    } else {
        printf("Multi-TCP Server: Failed to switch to channel %u\n", new_channel);
        return false;
    }
}

/**
 * Get currently active channel
 */
channel_id_t multi_tcp_server_get_active_channel(void) {
    if (!multi_tcp_server_initialized) {
        return CHANNEL_MAX;
    }
    
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (tcp_servers[i].active) {
            return i;
        }
    }
    
    return CHANNEL_MAX;
}

/**
 * Process all active TCP servers
 */
void multi_tcp_server_process(void) {
    if (!multi_tcp_server_initialized) {
        return;
    }
    
    // Since we're using singleton, just process the active server
    tcp_socket_server_process();
}

/**
 * Internal function to deinitialize all servers
 */
static void multi_tcp_server_deinit_all_internal(void) {
    if (!multi_tcp_server_initialized) {
        return;
    }
    
    printf("Multi-TCP Server: Deinitializing all servers\n");
    
    // Deinitialize the singleton server
    tcp_socket_server_deinit();
    
    // Reset all server states
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        tcp_servers[i].initialized = false;
        tcp_servers[i].port = 0;
        tcp_servers[i].channel = CHANNEL_MAX;
        tcp_servers[i].active = false;
    }
    
    multi_tcp_server_initialized = false;
    
    printf("Multi-TCP Server: All servers deinitialized\n");
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 0);
}