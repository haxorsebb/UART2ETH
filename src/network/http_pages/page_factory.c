/**
 * @file page_factory.c
 * @brief Factory defaults page implementation (manufacturing only)
 * 
 * Generates factory configuration page and handles factory defaults
 * programming for manufacturing use. Only compiled when FACTORY_INTERNAL_VERSION
 * is defined.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 * - ADR-015: Factory Defaults Web Interface
 */

#include "network/http_pages/page_factory.h"

#ifdef FACTORY_INTERNAL_VERSION

#include "config/factory_defaults.h"
#include "config/shared_memory.h"
#include "config/flash_persistence.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void http_generate_factory_page(char* buffer, size_t buffer_size, const char* error_msg, size_t error_msg_size,  const char* success_msg, size_t success_msg_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Get current factory defaults (if any)
    const factory_defaults_t* current_factory = factory_defaults_get();
    bool factory_valid = factory_defaults_is_valid();
    
    printf("FACTORY DEFAULTS GET %d 0x%08X %02d/%02d\n", factory_valid, current_factory, current_factory->production_week,current_factory->production_year);

    // Prepare current values for display
    char current_serial[32] = "Not Programmed";
    char current_mac[18] = "00:00:00:00:00:00";
    char current_mac_suffix[9] = "0:00:00";
    char current_ip[16] = "0.0.0.0";
    char current_netmask[16] = "0.0.0.0";
    char current_gateway[16] = "0.0.0.0";
    const char* current_dhcp = "No";
    const char* current_board_type = "Unknown";
    char current_password[32] = "Not Set";
    
    // Form field defaults (populated from factory defaults when available)
    int form_prod_year = 26;
    int form_prod_week = 1;
    char form_serial[21] = "1";  // max uint48 is 281474976710655 (15 digits)
    int form_board_type = 0;
    char form_ip[16] = "192.168.1.201";
    char form_netmask[16] = "255.255.255.0";
    char form_gateway[16] = "192.168.1.1";
    const char* form_dhcp_checked = "";
    char form_password[32] = "admin";
    char form_mac_decimal[21] = "58102136176640";  // default: 34:D7:F5:30:00:00
    
    if (factory_valid && current_factory) {
        // Format serial number as YYWW-NNNNNNNNNNNN
        uint64_t serial_decimal = 0;
        for (int i = 0; i < 6; i++) {
            serial_decimal = (serial_decimal << 8) | current_factory->serial_number[i];
        }
        
        // Populate form fields from saved factory defaults
        form_prod_year = current_factory->production_year;
        form_prod_week = current_factory->production_week;
        snprintf(form_serial, sizeof(form_serial), "%llu", serial_decimal);
        form_board_type = current_factory->board_type;
        
        uint32_t fip = current_factory->default_ip;
        snprintf(form_ip, sizeof(form_ip), "%d.%d.%d.%d",
                 (int)(fip & 0xFF), (int)((fip >> 8) & 0xFF),
                 (int)((fip >> 16) & 0xFF), (int)((fip >> 24) & 0xFF));
        
        uint32_t fnm = current_factory->default_netmask;
        snprintf(form_netmask, sizeof(form_netmask), "%d.%d.%d.%d",
                 (int)(fnm & 0xFF), (int)((fnm >> 8) & 0xFF),
                 (int)((fnm >> 16) & 0xFF), (int)((fnm >> 24) & 0xFF));
        
        uint32_t fgw = current_factory->default_gateway;
        snprintf(form_gateway, sizeof(form_gateway), "%d.%d.%d.%d",
                 (int)(fgw & 0xFF), (int)((fgw >> 8) & 0xFF),
                 (int)((fgw >> 16) & 0xFF), (int)((fgw >> 24) & 0xFF));
        
        form_dhcp_checked = current_factory->default_dhcp_enable ? "checked" : "";
        snprintf(form_password, sizeof(form_password), "%s", current_factory->default_password);
        
        // Compute decimal MAC from 6-byte address for the form field
        uint64_t mac_val = 0;
        for (int i = 0; i < 6; i++) {
            mac_val = (mac_val << 8) | current_factory->mac_address[i];
        }
        snprintf(form_mac_decimal, sizeof(form_mac_decimal), "%llu", mac_val);
        snprintf(current_serial, sizeof(current_serial), "%02u%02u-%012llu",
                 current_factory->production_year,
                 current_factory->production_week,
                 serial_decimal);
        
        // Format MAC address
        snprintf(current_mac, sizeof(current_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 current_factory->mac_address[0], current_factory->mac_address[1],
                 current_factory->mac_address[2], current_factory->mac_address[3],
                 current_factory->mac_address[4], current_factory->mac_address[5]);
        
        // Format MAC suffix (last 5 hex chars as X:XX:XX)
        snprintf(current_mac_suffix, sizeof(current_mac_suffix), "%X:%02X:%02X",
                 current_factory->mac_address[3] & 0x0F,
                 current_factory->mac_address[4],
                 current_factory->mac_address[5]);
        
        // Format IP addresses
        uint32_t ip = current_factory->default_ip;
        snprintf(current_ip, sizeof(current_ip), "%d.%d.%d.%d",
                 (int)((ip >> 0) & 0xFF), (int)((ip >> 8) & 0xFF),
                 (int)((ip >> 16) & 0xFF), (int)((ip >> 24) & 0xFF));
        
        uint32_t netmask = current_factory->default_netmask;
        snprintf(current_netmask, sizeof(current_netmask), "%d.%d.%d.%d",
                 (int)((netmask >> 0) & 0xFF), (int)((netmask >> 8) & 0xFF),
                 (int)((netmask >> 16) & 0xFF), (int)((netmask >> 24) & 0xFF));
        
        uint32_t gw = current_factory->default_gateway;
        snprintf(current_gateway, sizeof(current_gateway), "%d.%d.%d.%d",
                 (int)((gw >> 0) & 0xFF), (int)((gw >> 8) & 0xFF),
                 (int)((gw >> 16) & 0xFF), (int)((gw >> 24) & 0xFF));
        
        current_dhcp = current_factory->default_dhcp_enable ? "Yes" : "No";
        
        // Get board type name
        switch (current_factory->board_type) {
            case BOARD_TYPE_SHARK: current_board_type = "SHARK"; break;
            case BOARD_TYPE_PRIMARY: current_board_type = "PRIMARY"; break;
            case BOARD_TYPE_SECONDARY: current_board_type = "SECONDARY"; break;
            default: current_board_type = "Unknown"; break;
        }
        
        // Copy password (show actual password for factory verification)
        snprintf(current_password, sizeof(current_password), "%s", current_factory->default_password);
    }
    
    // Generate minified HTML response (no JavaScript, server-side validation only)
    int html_len = snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><title>Factory Defaults</title>"
        "<link rel=\"stylesheet\" href=\"/styles.css\"></head><body>"
        "<div class=\"container\">"
        "<div class=\"header warning\">"
        "<h1>Factory Defaults<span class=\"warning-badge\">FACTORY INTERNAL</span></h1>"
        "<p>Manufacturing Tool - Program Device Factory Configuration</p>"
        "</div>"
        "<div class=\"nav-links\"><a href=\"/\">Status</a><a href=\"/config\">Configuration</a><a href=\"/update\">Update</a></div>"
        "%s%s%s"  // Error message placeholder
        "%s%s%s"  // Success message placeholder
        "<div class=\"current-factory %s\">"
        "<h4>Currently Programmed</h4>"
        "<p><strong>Serial:</strong> %s</p>"
        "<p><strong>MAC:</strong> %s</p>"
        "<p><strong>Board:</strong> %s</p>"
        "<p><strong>IP:</strong> %s | <strong>Mask:</strong> %s | <strong>GW:</strong> %s | <strong>DHCP:</strong> %s</p>"
        "<p><strong>Access:</strong> User: admin | Password %s</p>"
        "</div>"
        "<form method=\"POST\" action=\"/factory\">"
        "<div class=\"section\"><h3>Serial Number</h3>"
        "<div class=\"form-row\">"
        "<div class=\"form-group\"><label for=\"prod_year\">Production Year (YY):</label>"
        "<input type=\"number\" id=\"prod_year\" name=\"prod_year\" min=\"0\" max=\"99\" value=\"%d\" required>"
        "<small>YY for 20YY (e.g., 26=2026)</small></div>"
        "<div class=\"form-group\"><label for=\"prod_week\">Production Week:</label>"
        "<input type=\"number\" id=\"prod_week\" name=\"prod_week\" min=\"1\" max=\"53\" value=\"%d\" required>"
        "<small>Week 1-53 (ISO 8601)</small></div>"
        "</div>"
        "<div class=\"form-group\"><label for=\"serial_number\">Serial Number (Decimal):</label>"
        "<input type=\"text\" id=\"serial_number\" name=\"serial_number\" value=\"%s\" required>"
        "<small>Unique serial (0-281474976710655)</small></div>"
        "</div>"
        "<div class=\"section\"><h3>Network Identity</h3>"
        "<div class=\"form-group\"><label for=\"mac_decimal\">MAC Address (Decimal):</label>"
        "<input type=\"text\" id=\"mac_decimal\" name=\"mac_decimal\" value=\"%s\" "
        "style=\"width:200px;font-family:monospace;\" required>"
        "<small>Valid range: 58102136176640 - 58102137225215 "
        "(34:D7:F5:30:00:00 - 34:D7:F5:3F:FF:FF)</small>"
        "<p id=\"mac_hex\" style=\"font-family:monospace;margin:4px 0 0 0;\"></p>"
        "<script>function updMac(){var v=document.getElementById('mac_decimal').value;"
        "var n=parseFloat(v);if(n>=58102136176640&&n<=58102137225215){"
        "var h=[],t=n;for(var i=0;i<6;i++){h.unshift(('0'+(t%%256).toString(16)).slice(-2).toUpperCase());"
        "t=Math.floor(t/256);}document.getElementById('mac_hex').textContent='= '+h.join(':');"
        "}else{document.getElementById('mac_hex').textContent='(out of range)';}}"
        "document.getElementById('mac_decimal').addEventListener('input',updMac);"
        "updMac();</script>"
        "</div>"
        "</div>"
        "<div class=\"section\"><h3>Board Type</h3>"
        "<div class=\"form-group\"><label for=\"board_type\">Hardware Variant:</label>"
        "<select id=\"board_type\" name=\"board_type\" required>"
        "<option value=\"0\"%s>SHARK</option>"
        "<option value=\"1\"%s>PRIMARY</option>"
        "<option value=\"2\"%s>SECONDARY</option>"
        "</select></div>"
        "</div>"
        "<div class=\"section\"><h3>Default Network</h3>"
        "<div class=\"form-row\">"
        "<div class=\"form-group\"><label for=\"default_ip\">Default IP:</label>"
        "<input type=\"text\" id=\"default_ip\" name=\"default_ip\" value=\"%s\" required></div>"
        "<div class=\"form-group\"><label for=\"default_netmask\">Default Netmask:</label>"
        "<input type=\"text\" id=\"default_netmask\" name=\"default_netmask\" value=\"%s\" required></div>"
        "</div>"
        "<div class=\"form-group\"><label for=\"default_gateway\">Default Gateway:</label>"
        "<input type=\"text\" id=\"default_gateway\" name=\"default_gateway\" value=\"%s\" required></div>"
        "<div class=\"form-group\"><div class=\"checkbox-group\">"
        "<input type=\"checkbox\" id=\"default_dhcp\" name=\"default_dhcp\" value=\"1\" %s>"
        "<label for=\"default_dhcp\">Enable DHCP by default</label>"
        "</div></div>"
        "</div>"
        "<div class=\"section\"><h3>Security</h3>"
        "<div class=\"form-group\"><label for=\"default_password\">Factory Password:</label>"
        "<input type=\"text\" id=\"default_password\" name=\"default_password\" value=\"%s\" maxlength=\"31\" required>"
        "<small>Max 31 characters</small></div>"
        "</div>"
        "<div class=\"section\"><h3>Actions</h3>"
        "<p><strong>Warning:</strong> This permanently programs flash memory.</p>"
        "<button type=\"submit\" class=\"button button-success\">✓ Write Factory Defaults</button> "
        "<a href=\"/\" class=\"button button-secondary\">Cancel</a>"
        "</div>"
        "</form>"
        "</div></body></html>\r\n",
        error_msg_size > 0 ? "<div class=\"section\" style=\"background-color: #fadbd8;border-left:4px solid #e74c3c\">" : "",
        error_msg_size > 0 ? error_msg : "",
        error_msg_size > 0 ? "</div>" : "",
        success_msg_size > 0 ? "<div class=\"section\" style=\"background-color: #fadbd8;border-left:4px solid #3ce74aff\">" : "",
        success_msg_size > 0 ? success_msg : "",
        success_msg_size > 0 ? "</div>" : "",
        factory_valid ? "valid" : "invalid",
        current_serial, current_mac, current_board_type,
        current_ip, current_netmask, current_gateway, current_dhcp, current_password,
        /* Form field values */
        form_prod_year, form_prod_week, form_serial,
        form_mac_decimal,
        (form_board_type == 0) ? " selected" : "",
        (form_board_type == 1) ? " selected" : "",
        (form_board_type == 2) ? " selected" : "",
        form_ip, form_netmask, form_gateway, form_dhcp_checked, form_password
    );
    
    printf("HTTP: Generated factory page (%d bytes, %s)\n", html_len, error_msg ? "with error" : "OK");
    if (html_len >= (int)buffer_size) {
        printf("HTTP: ERROR - Factory page truncated! Need %d bytes\n", html_len);
    }
}

bool http_parse_factory_post_data(const char* post_data, size_t data_len, char* error_msg, size_t error_msg_size, char* success_msg, size_t success_msg_size) {
    // Find start of form data (after double CRLF)
    const char* form_start = strstr(post_data, "\r\n\r\n");
    if (!form_start) {
        snprintf(error_msg, error_msg_size, "Invalid POST data format");
        return false;
    }
    form_start += 4; // Skip past \r\n\r\n
    
    printf("HTTP: Parsing factory defaults form data: %s\n", form_start);
    
    // Prepare factory defaults structure
    factory_defaults_t factory_data = {0};
    
    // Parse form fields
    char* form_copy = malloc(strlen(form_start) + 1);
    if (!form_copy) {
        snprintf(error_msg, error_msg_size, "Memory allocation failed");
        return false;
    }
    strcpy(form_copy, form_start);
    
    // Initialize defaults
    uint8_t prod_year = 0;
    uint8_t prod_week = 0;
    uint64_t serial_number = 0;
    bool has_year = false, has_week = false, has_serial = false;
    bool has_mac = false, has_board_type = false;
    bool has_ip = false, has_netmask = false, has_gateway = false;
    bool has_password = false;
    
    char* token = strtok(form_copy, "&");
    while (token) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char* key = token;
            char* value = equals + 1;
            
            // Parse serial number fields with validation
            if (strcmp(key, "prod_year") == 0) {
                int year = atoi(value);
                if (year < 0 || year > 99) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Production year must be 0-99");
                    return false;
                }
                prod_year = (uint8_t)year;
                has_year = true;
            } else if (strcmp(key, "prod_week") == 0) {
                int week = atoi(value);
                if (week < 1 || week > 53) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Production week must be 1-53");
                    return false;
                }
                prod_week = (uint8_t)week;
                has_week = true;
            } else if (strcmp(key, "serial_number") == 0) {
                // Parse decimal serial number
                char* endptr;
                unsigned long long sn = strtoull(value, &endptr, 10);
                if (*endptr != '\0' || sn > 281474976710655ULL) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Serial number must be 0-281474976710655");
                    return false;
                }
                serial_number = sn;
                has_serial = true;
            }
            // Parse MAC address as decimal number
            // Valid range: 58102136176640 (34:D7:F5:30:00:00) to
            //              58102137225215 (34:D7:F5:3F:FF:FF)
            else if (strcmp(key, "mac_decimal") == 0) {
                char* endptr;
                unsigned long long mac_val = strtoull(value, &endptr, 10);
                if (*endptr != '\0' ||
                    mac_val < 58102136176640ULL ||
                    mac_val > 58102137225215ULL) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size,
                             "MAC must be 58102136176640 - 58102137225215");
                    return false;
                }
                // Convert decimal to 6-byte MAC (big-endian)
                for (int i = 5; i >= 0; i--) {
                    factory_data.mac_address[i] = (uint8_t)(mac_val & 0xFF);
                    mac_val >>= 8;
                }
                has_mac = true;
            }
            // Parse board type
            else if (strcmp(key, "board_type") == 0) {
                int board_type = atoi(value);
                if (board_type >= 0 && board_type <= 2) {
                    factory_data.board_type = (uint8_t)board_type;
                    has_board_type = true;
                }
            }
            // Parse default IP
            else if (strcmp(key, "default_ip") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 &&
                    a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
                    c >= 0 && c <= 255 && d >= 0 && d <= 255) {
                    factory_data.default_ip = (uint32_t)a | ((uint32_t)b << 8) | 
                                             ((uint32_t)c << 16) | ((uint32_t)d << 24);
                    has_ip = true;
                } else {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Invalid IP address (each octet must be 0-255)");
                    return false;
                }
            }
            // Parse default netmask
            else if (strcmp(key, "default_netmask") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 &&
                    a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
                    c >= 0 && c <= 255 && d >= 0 && d <= 255) {
                    factory_data.default_netmask = (uint32_t)a | ((uint32_t)b << 8) | 
                                                  ((uint32_t)c << 16) | ((uint32_t)d << 24);
                    has_netmask = true;
                } else {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Invalid netmask (each octet must be 0-255)");
                    return false;
                }
            }
            // Parse default gateway
            else if (strcmp(key, "default_gateway") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 &&
                    a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
                    c >= 0 && c <= 255 && d >= 0 && d <= 255) {
                    factory_data.default_gateway = (uint32_t)a | ((uint32_t)b << 8) | 
                                                  ((uint32_t)c << 16) | ((uint32_t)d << 24);
                    has_gateway = true;
                } else {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Invalid gateway (each octet must be 0-255)");
                    return false;
                }
            }
            // Parse default DHCP
            else if (strcmp(key, "default_dhcp") == 0 && strcmp(value, "1") == 0) {
                factory_data.default_dhcp_enable = 1;
            }
            // Parse default password with validation
            else if (strcmp(key, "default_password") == 0) {
                // URL decode password (replace + with space, decode %)
                char decoded_password[32] = {0};
                size_t decoded_len = 0;
                for (size_t i = 0; value[i] && decoded_len < 31; i++) {
                    if (value[i] == '+') {
                        decoded_password[decoded_len++] = ' ';
                    } else if (value[i] == '%' && value[i+1] && value[i+2]) {
                        int hex_val;
                        sscanf(&value[i+1], "%02x", &hex_val);
                        decoded_password[decoded_len++] = (char)hex_val;
                        i += 2;
                    } else {
                        decoded_password[decoded_len++] = value[i];
                    }
                }
                if (decoded_len > 31) {
                    free(form_copy);
                    snprintf(error_msg, error_msg_size, "Password must be max 31 characters");
                    return false;
                }
                strncpy(factory_data.default_password, decoded_password, 31);
                factory_data.default_password[31] = '\0';
                has_password = true;
            }
        }
        token = strtok(NULL, "&");
    }
    
    free(form_copy);
    
    // Validate all required fields present
    if (!has_year || !has_week || !has_serial) {
        snprintf(error_msg, error_msg_size, "Missing serial number fields");
        return false;
    }
    if (!has_mac) {
        snprintf(error_msg, error_msg_size, "Missing MAC address");
        return false;
    }
    if (!has_board_type) {
        snprintf(error_msg, error_msg_size, "Missing board type");
        return false;
    }
    if (!has_ip || !has_netmask || !has_gateway) {
        snprintf(error_msg, error_msg_size, "Missing IP/netmask/gateway configuration");
        return false;
    }
    if (!has_password) {
        snprintf(error_msg, error_msg_size, "Missing default password");
        return false;
    }
    
    // Convert decimal serial number to 6-byte array (big-endian)
    factory_data.production_year = prod_year;
    factory_data.production_week = prod_week;
    for (int i = 5; i >= 0; i--) {
        factory_data.serial_number[i] = (uint8_t)(serial_number & 0xFF);
        serial_number >>= 8;
    }
    
    // Log what we're about to write
    printf("HTTP: Factory defaults to write:\n");
    printf("  Serial: %02u%02u-%02X%02X%02X%02X%02X%02X\n",
           factory_data.production_year, factory_data.production_week,
           factory_data.serial_number[0], factory_data.serial_number[1],
           factory_data.serial_number[2], factory_data.serial_number[3],
           factory_data.serial_number[4], factory_data.serial_number[5]);
    printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           factory_data.mac_address[0], factory_data.mac_address[1],
           factory_data.mac_address[2], factory_data.mac_address[3],
           factory_data.mac_address[4], factory_data.mac_address[5]);
    printf("  Board Type: %u\n", factory_data.board_type);
    printf("  Default IP: %u.%u.%u.%u\n",
           (unsigned int)(factory_data.default_ip & 0xFF),
           (unsigned int)((factory_data.default_ip >> 8) & 0xFF),
           (unsigned int)((factory_data.default_ip >> 16) & 0xFF),
           (unsigned int)((factory_data.default_ip >> 24) & 0xFF));
    printf("  Default GW: %u.%u.%u.%u\n",
           (unsigned int)(factory_data.default_gateway & 0xFF),
           (unsigned int)((factory_data.default_gateway >> 8) & 0xFF),
           (unsigned int)((factory_data.default_gateway >> 16) & 0xFF),
           (unsigned int)((factory_data.default_gateway >> 24) & 0xFF));
    printf("  Default GW: %u.%u.%u.%u\n",
           (unsigned int)(factory_data.default_gateway & 0xFF),
           (unsigned int)((factory_data.default_gateway >> 8) & 0xFF),
           (unsigned int)((factory_data.default_gateway >> 16) & 0xFF),
           (unsigned int)((factory_data.default_gateway >> 24) & 0xFF));
    printf("  Default DHCP: %s\n", factory_data.default_dhcp_enable ? "Yes" : "No");
    
    // Write factory defaults to flash
    if (!factory_defaults_write(&factory_data)) {
        snprintf(error_msg, error_msg_size, "Flash write operation failed");
        return false;
    }
    
    printf("HTTP: Factory defaults written successfully!\n");
    
    // Verify by reloading
    if (!factory_defaults_init()) {
        snprintf(error_msg, error_msg_size, "Write succeeded but verification failed");
        return false;
    }
    
    printf("HTTP: Factory defaults verified successfully!\n");
    
    // Apply new factory defaults to the running configuration so that
    // all pages (status, config, factory) show consistent values and
    // the ENC28J60 MAC is updated without requiring a reboot.
    factory_defaults_apply_to_config();
    
    // Signal core1 to reconfigure the network with the updated MAC/IP
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (layout) {
        layout->config_change_pending = true;
    }
    
    // Persist the updated configuration to flash
    flash_persistence_force_save_configuration();
    
    printf("HTTP: Factory defaults applied to running config and persisted\n");
    snprintf(success_msg, success_msg_size, "Factory defaults updated successfully!");
    return true;
}

#endif // FACTORY_INTERNAL_VERSION
