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
        "                <p><strong>Select Firmware File:</strong></p>\n"
        "                <input type=\"file\" id=\"fw_file\" accept=\".uf2\" style=\"margin: 15px 0;\">\n"
        "                <br>\n"
        "                <button id=\"fw_btn\" class=\"button\" onclick=\"doUpload()\">Upload &amp; Install</button>\n"
        "                <p id=\"fw_status\" style=\"margin-top:10px;\"></p>\n"
        "            </div>\n"
        "            <p><em>Note: Device will automatically reboot to apply new firmware after successful upload.</em></p>\n"
        "            <script>\n"
        "            function doUpload(){\n"
        "              var f=document.getElementById('fw_file').files[0];\n"
        "              if(!f){alert('Please select a .uf2 file first.');return;}\n"
        "              var s=document.getElementById('fw_status');\n"
        "              var b=document.getElementById('fw_btn');\n"
        "              b.disabled=true;b.textContent='Uploading...';\n"
        "              s.textContent='Uploading '+f.name+' ('+Math.round(f.size/1024)+' KB)...';\n"
        "              var fd=new FormData();fd.append('firmware',f);\n"
        "              document.getElementById('fw_file').value='';\n"
        "              fetch('/update',{method:'POST',body:fd})\n"
        "              .then(function(){uploadDone();})\n"
        "              .catch(function(){uploadDone();})\n"
        "            }\n"
        "            function uploadDone(){\n"
        "              var s=document.getElementById('fw_status');\n"
        "              var b=document.getElementById('fw_btn');\n"
        "              b.textContent='Rebooting...';\n"
        "              s.innerHTML='Upload sent. Device is rebooting...<br>'\n"
        "                +'Page will reload automatically.';\n"
        "              setTimeout(function tryReload(){\n"
        "                fetch('/update',{method:'GET'}).then(function(r){\n"
        "                  if(r.ok) window.location.replace('/update');\n"
        "                  else setTimeout(tryReload,2000);\n"
        "                }).catch(function(){setTimeout(tryReload,2000);});\n"
        "              },5000);\n"
        "            }\n"
        "            </script>\n"
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

void http_generate_reboot_page(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    // The refresh delay covers the deferred reboot grace period plus the
    // boot to a working web server with a static IP. Under DHCP the boot
    // can take longer; the user then refreshes manually once more.
    int html_len = snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>UART2ETH Reboot</title>\n"
        "    <meta http-equiv=\"refresh\" content=\"10;url=/\">\n"
        "    <link rel=\"stylesheet\" href=\"/styles.css\">\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <div class=\"header\">\n"
        "            <h1>Rebooting</h1>\n"
        "        </div>\n"
        "        <div class=\"message message-info\">\n"
        "            Reboot initiated. The device restarts now and this page\n"
        "            returns to the status page in a few seconds.\n"
        "        </div>\n"
        "    </div>\n"
        "</body>\n"
        "</html>\r\n");

    printf("HTTP: Generated reboot page (%d bytes)\n", html_len);
}
