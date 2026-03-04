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
 * @param error_msg Error message to display (NULL or empty for none)
 * @param error_msg_size Length of error message string
 * @param success_msg Success message to display (NULL or empty for none)
 * @param success_msg_size Length of success message string
 * 
 * Documentation Reference: ADR-018
 */
void http_generate_config_page(char* buffer, size_t buffer_size,
                               const char* error_msg, size_t error_msg_size,
                               const char* success_msg, size_t success_msg_size);

#endif // PAGE_CONFIG_H
