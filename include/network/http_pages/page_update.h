/**
 * @file page_update.h
 * @brief Firmware update page generation for HTTP server
 * 
 * Generates the firmware update page with upload form, status display,
 * and reboot controls.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 * - ADR-017: Update Module
 */

#ifndef PAGE_UPDATE_H
#define PAGE_UPDATE_H

#include <stddef.h>

/**
 * @brief Generate firmware update page
 * 
 * Creates complete HTTP response with HTML page showing:
 * - Firmware update status and statistics
 * - File upload form for .uf2 firmware files
 * - Reboot button for applying updates
 * - Optional message display
 * 
 * Requires external access to:
 * - update_manager for state and statistics
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * @param message Optional message to display (NULL for none)
 * 
 * Documentation Reference: ADR-018, ADR-017
 */
void http_generate_update_page(char* buffer, size_t buffer_size, const char* message);

#endif // PAGE_UPDATE_H
