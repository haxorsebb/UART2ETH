/**
 * @file page_config.c
 * @brief Configuration page generation implementation
 * 
 * Generates the configuration page with network settings, UART channel
 * configuration, and security settings forms.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 */

#include "network/http_pages/page_config.h"
#include "debug.h"
#include "network/network_manager.h"
#include "shared_memory.h"
#include "device_mode.h"
#include "config/version.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Generate configuration page
 * 
 * Creates complete HTTP response with configuration forms for network
 * settings, UART channels, and security (password change).
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * 
 * Documentation Reference: ADR-018
 */
void http_generate_config_page(char* buffer, size_t buffer_size,
                               const char* error_msg, size_t error_msg_size,
                               const char* success_msg, size_t success_msg_size,
                               bool offer_reboot) {
    // "Reboot now" control rendered next to the success message after a
    // stored configuration change; changes take effect at boot (ADR-019).
    static const char reboot_form_html[] =
        "<form method=\"POST\" action=\"/reboot\" style=\"margin-top:8px\">"
        "<button type=\"submit\" class=\"button button-danger\">Reboot now</button>"
        "</form>";
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Get current configuration
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    // Get current IP address for display
    simple_ip_addr_t ip_addr;
    bool has_ip = network_manager_get_ip_address(&ip_addr);
    char current_ip_str[16] = "Not Available";
    if (has_ip) {
        network_manager_ip_to_string(&ip_addr, current_ip_str);
    }
    
    // Format static IP for form
    uint32_t static_ip = layout->config.network.static_ip.addr;
    char static_ip_str[16];
    snprintf(static_ip_str, sizeof(static_ip_str), "%d.%d.%d.%d",
             (int)((static_ip >> 0) & 0xFF),
             (int)((static_ip >> 8) & 0xFF),
             (int)((static_ip >> 16) & 0xFF),
             (int)((static_ip >> 24) & 0xFF));
    
    // Format static netmask for form
    uint32_t static_nm = layout->config.network.static_netmask.addr;
    char static_nm_str[16];
    snprintf(static_nm_str, sizeof(static_nm_str), "%d.%d.%d.%d",
             (int)((static_nm >> 0) & 0xFF),
             (int)((static_nm >> 8) & 0xFF),
             (int)((static_nm >> 16) & 0xFF),
             (int)((static_nm >> 24) & 0xFF));
    
    // Format static gateway for form
    uint32_t static_gw = layout->config.network.static_gateway.addr;
    char static_gw_str[16];
    snprintf(static_gw_str, sizeof(static_gw_str), "%d.%d.%d.%d",
             (int)((static_gw >> 0) & 0xFF),
             (int)((static_gw >> 8) & 0xFF),
             (int)((static_gw >> 16) & 0xFF),
             (int)((static_gw >> 24) & 0xFF));
    
    // Format MAC address for form
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             layout->config.network.mac_address[0], layout->config.network.mac_address[1],
             layout->config.network.mac_address[2], layout->config.network.mac_address[3],
             layout->config.network.mac_address[4], layout->config.network.mac_address[5]);
    
    // Generate HTML response
    int html_len = snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>UART2ETH Configuration</title>\n"
        "    <link rel=\"stylesheet\" href=\"/styles.css\">\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <div class=\"header\">\n"
        "            <h1>UART2ETH Configuration"
#ifdef FACTORY_INTERNAL_VERSION
        " <span class=\"factory-badge\">FACTORY INTERNAL</span>"
#endif
        "</h1>\n"
        "            <p>Configure network settings and UART channels | Firmware " FIRMWARE_VERSION_STRING "</p>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"nav-links\">\n"
        "            <a href=\"/\">Status</a>\n"
        "            <a href=\"/config\" class=\"active\">Configuration</a>\n"
        "            <a href=\"/update\">Update</a>\n"
#ifdef FACTORY_INTERNAL_VERSION
        "            <a href=\"/factory\">FACTORY DEFAULTS</a>\n"
#endif
        "        </div>\n"
        "        \n"
        "%s%s%s"
        "%s%s%s%s"
        "        <div class=\"current-status\">\n"
        "            <strong>Current Status:</strong> IP Address: %s | MAC: %s | DHCP: %s\n"
        "        </div>\n"
        "        \n"
        "        <form method=\"POST\" action=\"/\">\n"
        "            \n"
        "            <div class=\"section\">\n"
        "                <h3>Network Configuration</h3>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"use_dhcp\" name=\"use_dhcp\" value=\"1\" %s>\n"
        "                        <label for=\"use_dhcp\">Use DHCP (automatic IP assignment)</label>\n"
        "                    </div>\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"static_ip\">Static IP Address:</label>\n"
        "                    <input type=\"text\" id=\"static_ip\" name=\"static_ip\" value=\"%s\" placeholder=\"192.168.1.201\">\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"static_netmask\">Subnet Mask:</label>\n"
        "                    <input type=\"text\" id=\"static_netmask\" name=\"static_netmask\" value=\"%s\" placeholder=\"255.255.255.0\">\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"static_gateway\">Gateway:</label>\n"
        "                    <input type=\"text\" id=\"static_gateway\" name=\"static_gateway\" value=\"%s\" placeholder=\"192.168.1.1\">\n"
        "                </div>\n"
        "            </div>\n"
        "            \n"
        "            <div class=\"section\">\n"
        "                <h3>UART Channel Configuration</h3>\n"
        "                \n"
#if DEVICE_CHANNEL_2_ENABLED
        "                \n"
        "                <div class=\"uart-row\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"ch2_enabled\" name=\"ch2_enabled\" value=\"1\" %s>\n"
        "                        <label for=\"ch2_enabled\">UART2 Enabled</label>\n"
        "                    </div>\n"
        "                    <div class=\"form-group\" style=\"margin-bottom: 0;\">\n"
        "                        <label for=\"ch2_port\">TCP Port:</label>\n"
        "                        <input type=\"number\" id=\"ch2_port\" name=\"ch2_port\" value=\"%d\" min=\"1024\" max=\"65535\">\n"
        "                    </div>\n"
        "                    <div style=\"flex: 0.5; font-size: 14px; color: #7f8c8d;\">GP14/GP15</div>\n"
        "                </div>\n"
#endif
#if DEVICE_CHANNEL_3_ENABLED
        "                \n"
        "                <div class=\"uart-row\">\n"
        "                    <div class=\"checkbox-group\">\n"
        "                        <input type=\"checkbox\" id=\"ch3_enabled\" name=\"ch3_enabled\" value=\"1\" %s>\n"
        "                        <label for=\"ch3_enabled\">UART3 Enabled</label>\n"
        "                    </div>\n"
        "                    <div class=\"form-group\" style=\"margin-bottom: 0;\">\n"
        "                        <label for=\"ch3_port\">TCP Port:</label>\n"
        "                        <input type=\"number\" id=\"ch3_port\" name=\"ch3_port\" value=\"%d\" min=\"1024\" max=\"65535\">\n"
        "                    </div>\n"
        "                    <div style=\"flex: 0.5; font-size: 14px; color: #7f8c8d;\">GP22/GP23</div>\n"
        "                </div>\n"
#endif
#if DEVICE_CHANNEL_4_ENABLED
        "                \n"
        "                <div class=\"uart-row\">\n"
        "                    <div style=\"flex:1;font-size:14px;color:#2c3e50;\">"
        "                        <strong>UART4</strong> (always enabled)"
        "                    </div>\n"
        "                    <div class=\"form-group\" style=\"margin-bottom: 0;\">\n"
        "                        <label for=\"ch4_port\">TCP Port:</label>\n"
        "                        <input type=\"number\" id=\"ch4_port\" name=\"ch4_port\" value=\"%d\" min=\"1024\" max=\"65535\">\n"
        "                    </div>\n"
        "                    <div style=\"flex: 0.5; font-size: 14px; color: #7f8c8d;\">GP5/GP4</div>\n"
        "                </div>\n"
#endif
        "            </div>\n"
        "            \n"
        "            <div class=\"section\">\n"
        "                <h3>Save Configuration</h3>\n"
        "                <p><strong>Important:</strong> Configuration changes are saved to flash memory and will persist across firmware updates. Network changes may require a device restart to take full effect.</p>\n"
        "                \n"
        "                <button type=\"submit\" class=\"button\">Save Configuration</button>\n"
        "                <a href=\"/\" class=\"button button-secondary\" style=\"text-decoration: none; display: inline-block; margin-left: 10px;\">Cancel</a>\n"
        "            </div>\n"
        "            \n"
        "        </form>\n"
        "        \n"
        "        <!-- Password Change Form (separate from network/UART config) -->\n"
        "        <form method=\"POST\" action=\"/change_password\">\n"
        "            <div class=\"section\">\n"
        "                <h3>Security Configuration</h3>\n"
        "                <p>Change the administrator password. The default username is <strong>admin</strong> and cannot be changed.</p>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"current_password\">Current Password:</label>\n"
        "                    <input type=\"password\" id=\"current_password\" name=\"current_password\" required autocomplete=\"current-password\">\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"new_password\">New Password (8-31 characters):</label>\n"
        "                    <input type=\"password\" id=\"new_password\" name=\"new_password\" minlength=\"8\" maxlength=\"31\" required autocomplete=\"new-password\">\n"
        "                </div>\n"
        "                \n"
        "                <div class=\"form-group\">\n"
        "                    <label for=\"confirm_password\">Confirm New Password:</label>\n"
        "                    <input type=\"password\" id=\"confirm_password\" name=\"confirm_password\" minlength=\"8\" maxlength=\"31\" required autocomplete=\"new-password\">\n"
        "                </div>\n"
        "                \n"
        "                <button type=\"submit\" class=\"button\">Change Password</button>\n"
        "            </div>\n"
        "        </form>\n"
        "        \n"
        "    </div>\n"
        "</body>\n"
        "</html>\r\n",
        error_msg_size > 0 ? "<div class=\"section\" style=\"background-color:#fadbd8;border-left:4px solid #e74c3c\">" : "",
        error_msg_size > 0 ? error_msg : "",
        error_msg_size > 0 ? "</div>" : "",
        success_msg_size > 0 ? "<div class=\"section\" style=\"background-color:#d5f5e3;border-left:4px solid #27ae60\">" : "",
        success_msg_size > 0 ? success_msg : "",
        (success_msg_size > 0 && offer_reboot) ? reboot_form_html : "",
        success_msg_size > 0 ? "</div>" : "",
        current_ip_str, mac_str, layout->config.network.use_dhcp ? "Enabled" : "Disabled",
        layout->config.network.use_dhcp ? "checked" : "",
        static_ip_str,
        static_nm_str,
        static_gw_str
#if DEVICE_CHANNEL_4_ENABLED
        ,layout->config.channels[CHANNEL_4].tcp_port
#endif
    );
    
    // DEBUG: Check if HTML generation was successful
    DEBUG_ONLY(
        printf("HTTP: Generated config page HTML length: %d bytes (max: %d)\n", html_len, (int)buffer_size);
        if (html_len >= (int)buffer_size) {
                printf("HTTP: ERROR - Config HTML truncated! Need at least %d bytes\n", html_len);
        } else {
                printf("HTTP: Config HTML generation successful - %d bytes remaining\n", 
                (int)buffer_size - html_len);
        }
    );
}
