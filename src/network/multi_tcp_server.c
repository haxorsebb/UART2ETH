/**
 * @file multi_tcp_server.c
 * @brief Multi-Port TCP Server Manager Implementation (TRUE MULTI-INSTANCE)
 * 
 * Manages multiple TCP socket servers simultaneously, one per UART channel.
 * Each server listens on its own port (4001-4004) and maps to UART channels 0-3.
 * Now supports true concurrent operation with the multi-instance TCP server.
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
    /* printf("Multi-TCP Server: TRUE MULTI-INSTANCE system initialized\n"); */
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
            /* printf("Multi-TCP Server: Failed to initialize system\n"); */
            return false;
        }
    }
    
    // Check if channel already initialized
    if (tcp_servers[channel].initialized) {
        /* printf("Multi-TCP Server: Channel %u already initialized on port %u\n", 
               channel, tcp_servers[channel].port); */
        return true;
    }
    
    /* printf("Multi-TCP Server: Initializing channel %u on port %u\n", channel, port); */
    
    // Initialize the TCP server for this channel (no deinitialization needed!)
    bool result = tcp_socket_server_init(port, channel);
    
    if (result) {
        tcp_servers[channel].initialized = true;
        tcp_servers[channel].port = port;
        tcp_servers[channel].channel = channel;
        tcp_servers[channel].active = true;
        
        /* printf("Multi-TCP Server: Channel %u initialized successfully on port %u\n", channel, port); */
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_AVAILABLE, port);
        
        return true;
    } else {
        /* printf("Multi-TCP Server: Failed to initialize channel %u on port %u\n", channel, port); */
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
    
    /* printf("Multi-TCP Server: Deinitializing channel %u (port %u)\n", 
           channel, tcp_servers[channel].port); */
    
    // Deinitialize the specific server instance
    tcp_socket_server_deinit_port(tcp_servers[channel].port);
    
    tcp_servers[channel].initialized = false;
    tcp_servers[channel].port = 0;
    tcp_servers[channel].channel = CHANNEL_MAX;
    tcp_servers[channel].active = false;
    
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
        /* printf("Multi-TCP Server: Channel %u not initialized or active\n", channel); */
        return false;
    }
    
    // Use the multi-instance server's send function
    return tcp_socket_server_send_to_channel(channel, data, length);
}

/**
 * Get statistics for all active servers
 */
void multi_tcp_server_get_all_stats(void) {
    if (!multi_tcp_server_initialized) {
        /* printf("Multi-TCP Server: System not initialized\n"); */
        return;
    }
    
    /* printf("=== Multi-TCP Server Status (TRUE MULTI-INSTANCE) ===\n"); */
    
    int active_count = 0;
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (tcp_servers[i].initialized) {
            /* printf("Channel %d: Port %u, %s\n", 
                   i, tcp_servers[i].port, 
                   tcp_servers[i].active ? "ACTIVE" : "INACTIVE"); */
            
            if (tcp_servers[i].active) {
                active_count++;
                // Note: Getting individual server stats would require 
                // extending the tcp_socket_server API to get stats by channel
                /* printf("  - Status: LISTENING\n"); */
            }
        } else {
            /* printf("Channel %d: Not initialized\n", i); */
        }
    }
    
    /* printf("Total active servers: %d/%d\n", active_count, MAX_TCP_SERVERS);
    printf("=====================================================\n"); */
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
            return true;
        }
    }
    
    return false;
}

/**
 * Get number of active channels
 */
int multi_tcp_server_get_active_count(void) {
    if (!multi_tcp_server_initialized) {
        return 0;
    }
    
    int count = 0;
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (tcp_servers[i].initialized && tcp_servers[i].active) {
            count++;
        }
    }
    
    return count;
}

/**
 * Check if specific channel is active
 */
bool multi_tcp_server_is_channel_active(channel_id_t channel) {
    if (channel >= MAX_TCP_SERVERS || !multi_tcp_server_initialized) {
        return false;
    }
    
    return tcp_servers[channel].initialized && tcp_servers[channel].active;
}

/**
 * Get port for specific channel
 */
uint16_t multi_tcp_server_get_channel_port(channel_id_t channel) {
    if (channel >= MAX_TCP_SERVERS || !multi_tcp_server_initialized) {
        return 0;
    }
    
    if (tcp_servers[channel].initialized) {
        return tcp_servers[channel].port;
    }
    
    return 0;
}

/**
 * Process all active TCP servers
 */
void multi_tcp_server_process(void) {
    if (!multi_tcp_server_initialized) {
        return;
    }
    
    // Process all active servers - the multi-instance TCP server handles this internally
    tcp_socket_server_process();
}

/**
 * Deinitialize all TCP servers
 */
void multi_tcp_server_deinit_all(void) {
    if (!multi_tcp_server_initialized) {
        return;
    }
    
    /* printf("Multi-TCP Server: Deinitializing all servers\n"); */
    
    // Deinitialize all servers
    tcp_socket_server_deinit();
    
    // Reset all server states
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        tcp_servers[i].initialized = false;
        tcp_servers[i].port = 0;
        tcp_servers[i].channel = CHANNEL_MAX;
        tcp_servers[i].active = false;
    }
    
    multi_tcp_server_initialized = false;
    
    /* printf("Multi-TCP Server: All servers deinitialized\n"); */
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 0);
}

/**
 * LEGACY FUNCTIONS - No longer needed with true multi-instance support
 */

/**
 * Switch active server (LEGACY - no longer needed)
 */
bool multi_tcp_server_switch_active_channel(channel_id_t new_channel) {
    // With true multi-instance support, all channels can be active simultaneously
    // This function now just checks if the requested channel is active
    
    if (new_channel >= MAX_TCP_SERVERS || !multi_tcp_server_initialized) {
        return false;
    }
    
    if (tcp_servers[new_channel].initialized && tcp_servers[new_channel].active) {
        /* printf("Multi-TCP Server: Channel %u is already active (no switching needed)\n", new_channel); */
        return true;
    } else {
        /* printf("Multi-TCP Server: Channel %u is not active\n", new_channel); */
        return false;
    }
}

/**
 * Get active channel (LEGACY - returns first active channel)
 */
channel_id_t multi_tcp_server_get_active_channel(void) {
    if (!multi_tcp_server_initialized) {
        return CHANNEL_MAX;
    }
    
    // Return the first active channel (for compatibility)
    for (int i = 0; i < MAX_TCP_SERVERS; i++) {
        if (tcp_servers[i].initialized && tcp_servers[i].active) {
            return i;
        }
    }
    
    return CHANNEL_MAX;
}