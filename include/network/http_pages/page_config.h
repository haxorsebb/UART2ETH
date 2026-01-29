/**
 * @file page_config.h
 * @brief Configuration page generation for HTTP server
 * 
 * Generates the configuration page with forms for network settings,
 * UART channel configuration, and security settings.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 */

#ifndef PAGE_CONFIG_H
#define PAGE_CONFIG_H

#include <stddef.h>

/**
 * @brief Generate configuration page
 * 
 * Creates complete HTTP response with HTML page showing:
 * - Network configuration form (DHCP, static IP, MAC)
 * - UART channel configuration (enable/disable, TCP ports)
 * - Security settings (password change form)
 * 
 * Requires external access to:
 * - network_manager for current IP/MAC
 * - shared_memory for device configuration
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * 
 * Documentation Reference: ADR-018
 */
void http_generate_config_page(char* buffer, size_t buffer_size);

#endif // PAGE_CONFIG_H
