/**
 * @file page_device.h
 * @brief Device status page generation for HTTP server
 * 
 * Generates the main device information page showing network configuration,
 * UART channel status, and device information.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 */

#ifndef PAGE_DEVICE_H
#define PAGE_DEVICE_H

#include <stddef.h>

/**
 * @brief Generate device status page
 * 
 * Creates complete HTTP response with HTML page showing:
 * - Network configuration (IP, MAC, DHCP status)
 * - UART channel ports and GPIO pin assignments  
 * - Device information (firmware version, hardware, uptime)
 * 
 * Requires external access to:
 * - network_manager for IP/MAC address
 * - shared_memory for device configuration
 * - g_server_stats for uptime
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * 
 * Documentation Reference: ADR-018
 */
void http_generate_device_page(char* buffer, size_t buffer_size);

#endif // PAGE_DEVICE_H
