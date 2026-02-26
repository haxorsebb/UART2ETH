/**
 * @file page_update.c
 * @brief Firmware update page generation implementation
 * 
 * Generates the firmware update page with upload controls, status
 * display, and reboot functionality.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 * - ADR-017: Update Module
 */

#include "network/http_pages/page_update.h"
#include "update/update_manager.h"
#include "config/version.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Generate firmware update page
 * 
 * Creates complete HTTP response with firmware update interface including:
 * - Current update state and statistics
 * - File upload form for .uf2 firmware
 * - Device reboot controls
 * - Optional status/error message display
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * @param message Optional message to display (NULL for none)
 * 
 * Documentation Reference: ADR-018, ADR-017
 */
void http_generate_update_page(char* buffer, size_t buffer_size, const char* message) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Get current update state and statistics
    update_state_t state = update_get_state();
    update_stats_t stats;
    update_get_stats(&stats);
    
    const char* state_str = "Unknown";
    const char* state_class = "";
    
    switch (state) {
        case UPDATE_STATE_IDLE:
            state_str = "Idle - Ready for update";
            state_class = "status-ok";
            break;
        case UPDATE_STATE_RECEIVING:
            state_str = "Receiving firmware...";
            state_class = "status-warning";
            break;
        case UPDATE_STATE_COMPLETE:
            state_str = "Upload complete";
            state_class = "status-ok";
            break;
        case UPDATE_STATE_ERROR:
            state_str = "Error occurred";
            state_class = "status-error";
            break;
    }
    
    // Generate HTML response
    int html_len = snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>UART2ETH Firmware Update</title>\n"
        "    <link rel=\"stylesheet\" href=\"/styles.css\">\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <div class=\"header\">\n"
        "            <h1>Firmware Update"
#ifdef FACTORY_INTERNAL_VERSION
        " <span class=\"factory-badge\">FACTORY INTERNAL</span>"
#endif
        "</h1>\n"
        "            <p>Manage device firmware and system updates | Firmware " FIRMWARE_VERSION_STRING "</p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"nav-links\">\n"
        "            <a href=\"/\">Status</a>\n"
        "            <a href=\"/config\">Configuration</a>\n"
        "            <a href=\"/update\" class=\"active\">Update</a>\n"
#ifdef FACTORY_INTERNAL_VERSION
        "            <a href=\"/factory\">FACTORY DEFAULTS</a>\n"
#endif
        "        </div>\n"
        "        \n"
        "%s%s%s"  /* Message placeholder */
        "        \n"
        "        <div class=\"section\">\n"
        "            <h3>Firmware Status</h3>\n"
        "            <p><span class=\"label\">Update State:</span> <span class=\"%s\">%s</span></p>\n"
        "            <p><span class=\"label\">Bytes Received:</span> <span class=\"value\">%u</span></p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <h3>Upload Firmware</h3>\n"
        "            <p>Upload a .uf2 firmware file for OTA update. Maximum size: 1024 KB</p>\n"
        "            <div style=\"border: 2px dashed #3498db; padding: 30px; text-align: center; border-radius: 8px; margin: 20px 0;\">\n"
        "                <form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">\n"
        "                    <p><strong>Select Firmware File:</strong></p>\n"
        "                    <input type=\"file\" name=\"firmware\" accept=\".uf2\" required style=\"margin: 15px 0;\">\n"
        "                    <br>\n"
        "                    <button type=\"submit\" class=\"button\">Upload & Install</button>\n"
        "                </form>\n"
        "            </div>\n"
        "            <p><em>Note: Device will automatically reboot to apply new firmware after successful upload.</em></p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"section\">\n"
        "            <h3>Device Reboot</h3>\n"
        "            <p>Reboot the device to apply pending changes or recover from errors.</p>\n"
        "            <p><strong>Warning:</strong> All active connections will be terminated.</p>\n"
        "            <form method=\"POST\" action=\"/reboot\">\n"
        "                <button type=\"submit\" class=\"button button-danger\">Reboot Device</button>\n"
        "            </form>\n"
        "        </div>\n"
        "        \n"
        "    </div>\n"
        "</body>\n"
        "</html>\r\n",
        /* Message */
        message ? "<div class=\"message message-info\">" : "",
        message ? message : "",
        message ? "</div>" : "",
        /* Status */
        state_class, state_str,
        stats.bytes_received
    );
    
    printf("HTTP: Generated update page (%d bytes)\n", html_len);
}
