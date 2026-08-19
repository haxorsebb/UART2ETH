/**
 * @file http_forms.c
 * @brief HTTP Form Handling Module Implementation
 * 
 * Implements form data parsing, validation, and URL decoding utilities
 * for processing POST requests in the UART2ETH web interface.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization - Phase 3
 * - ADR-016: HTTP Basic Authentication
 */

#include "network/http_forms.h"
#include "debug.h"
#include "network/http_server.h"
#include "shared_memory.h"
#include "flash_persistence.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "lwip/ip4_addr.h"

/**
 * @brief Decode URL-encoded string in place
 * 
 * Decodes URL percent-encoding (%20, %2B, etc.) in the string.
 * Modifies the string in place.
 * 
 * @param str String to decode (modified in place)
 */
void http_url_decode(char* str) {
    if (!str) return;
    
    char* dst = str;
    char* src = str;
    
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            // Decode %XX hex encoding
            int value;
            if (sscanf(src + 1, "%2x", &value) == 1) {
                *dst++ = (char)value;
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else if (*src == '+') {
            // Convert + to space
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * @brief Parse URL-encoded form data
 * 
 * Parses application/x-www-form-urlencoded form data into
 * an array of key-value pairs. This is a generic form parser.
 * 
 * @param form_data Raw form data string
 * @param fields Output array for parsed fields
 * @param max_fields Maximum number of fields to parse
 * @param field_count Output: actual number of fields parsed
 * @return true if parsing successful, false on error
 */
bool http_parse_form_data(const char* form_data, http_form_field_t* fields, 
                          size_t max_fields, size_t* field_count) {
    if (!form_data || !fields || !field_count || max_fields == 0) {
        return false;
    }
    
    *field_count = 0;
    
    // Make a working copy of the form data
    char* form_copy = malloc(strlen(form_data) + 1);
    if (!form_copy) {
        return false;
    }
    strcpy(form_copy, form_data);
    
    // Parse key=value pairs separated by &
    char* token = strtok(form_copy, "&");
    while (token && *field_count < max_fields) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            fields[*field_count].key = token;
            fields[*field_count].value = equals + 1;
            
            // URL decode both key and value in place
            http_url_decode((char*)fields[*field_count].key);
            http_url_decode((char*)fields[*field_count].value);
            
            (*field_count)++;
        }
        token = strtok(NULL, "&");
    }
    
    // Note: form_copy is NOT freed here - it must remain valid
    // for the lifetime of the fields array
    return true;
}

/**
 * @brief Get form field value by key
 * 
 * Searches parsed form fields for a specific key and returns its value.
 * 
 * @param fields Array of parsed form fields
 * @param field_count Number of fields in array
 * @param key Field name to search for
 * @return Field value if found, NULL otherwise
 */
const char* http_get_form_field(const http_form_field_t* fields, size_t field_count, const char* key) {
    if (!fields || !key) {
        return NULL;
    }
    
    for (size_t i = 0; i < field_count; i++) {
        if (strcmp(fields[i].key, key) == 0) {
            return fields[i].value;
        }
    }
    
    return NULL;
}

/**
 * @brief Check if form field equals specific value
 * 
 * Convenience function to check if a field has a specific value.
 * 
 * @param fields Array of parsed form fields
 * @param field_count Number of fields in array
 * @param key Field name to check
 * @param value Expected value
 * @return true if field exists and equals value, false otherwise
 */
bool http_form_field_equals(const http_form_field_t* fields, size_t field_count, 
                            const char* key, const char* value) {
    const char* field_value = http_get_form_field(fields, field_count, key);
    if (!field_value || !value) {
        return false;
    }
    return strcmp(field_value, value) == 0;
}

/**
 * @brief Validate password change request
 * 
 * Validates password change according to ADR-016 rules:
 * - Current password must match stored password
 * - New password must be 8-31 characters
 * - New password must match confirmation
 * - All fields must be non-empty
 * 
 * @param current_pwd Current password from form
 * @param new_pwd New password from form
 * @param confirm_pwd Confirmation password from form
 * @param stored_pwd Stored password to validate against
 * @return Validation result code
 * 
 * Reference: ADR-016 HTTP Basic Authentication - Password Management
 */
password_change_result_t http_validate_password_change(
    const char* current_pwd,
    const char* new_pwd,
    const char* confirm_pwd,
    const char* stored_pwd
) {
    // Check for NULL pointers
    if (!current_pwd || !new_pwd || !confirm_pwd || !stored_pwd) {
        return PWD_CHANGE_EMPTY_FIELD;
    }
    
    // Check for empty fields
    if (strlen(current_pwd) == 0 || strlen(new_pwd) == 0 || strlen(confirm_pwd) == 0) {
        printf("HTTP Password Change: Empty field detected\n");
        return PWD_CHANGE_EMPTY_FIELD;
    }
    
    // Validate current password matches stored password
    if (strcmp(current_pwd, stored_pwd) != 0) {
        printf("HTTP Password Change: Current password incorrect\n");
        return PWD_CHANGE_CURRENT_WRONG;
    }
    
    // Validate new password length (minimum 8 characters)
    size_t new_pwd_len = strlen(new_pwd);
    if (new_pwd_len < 8) {
        printf("HTTP Password Change: New password too short (%zu chars, need 8)\n", new_pwd_len);
        return PWD_CHANGE_TOO_SHORT;
    }
    
    // Validate new password length (maximum 31 characters)
    if (new_pwd_len > 31) {
        printf("HTTP Password Change: New password too long (%zu chars, max 31)\n", new_pwd_len);
        return PWD_CHANGE_TOO_LONG;
    }
    
    // Validate new password matches confirmation
    if (strcmp(new_pwd, confirm_pwd) != 0) {
        printf("HTTP Password Change: Password confirmation mismatch\n");
        return PWD_CHANGE_NO_MATCH;
    }
    
    printf("HTTP Password Change: Validation successful\n");
    return PWD_CHANGE_OK;
}

/**
 * @brief Handle password change request
 * 
 * Parses password change POST data, validates the request, and updates
 * the stored password if validation passes.
 * 
 * @param post_data Raw POST request data
 * @param data_len Length of POST data
 * @return true if password was changed successfully, false otherwise
 * 
 * Reference: ADR-016 HTTP Basic Authentication - Password Management
 */
bool http_handle_password_change(const char* post_data, size_t data_len,
                                 char* error_msg, size_t error_msg_size,
                                 char* success_msg, size_t success_msg_size) {
    (void)data_len;
    // Find start of form data (after double CRLF)
    const char* form_start = strstr(post_data, "\r\n\r\n");
    if (!form_start) {
        printf("HTTP Password Change: No form data found\n");
        snprintf(error_msg, error_msg_size, "Invalid form data");
        return false;
    }
    form_start += 4; // Skip past \r\n\r\n
    
    printf("HTTP: Parsing password change form data\n");
    
    // Extract password fields from form data
    char current_password[64] = {0};
    char new_password[64] = {0};
    char confirm_password[64] = {0};
    
    // Parse form fields - simple key=value&key=value parsing
    char* form_copy = malloc(strlen(form_start) + 1);
    if (!form_copy) {
        printf("HTTP Password Change: Memory allocation failed\n");
        snprintf(error_msg, error_msg_size, "Memory allocation failed");
        return false;
    }
    strcpy(form_copy, form_start);
    
    char* token = strtok(form_copy, "&");
    while (token) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char* key = token;
            char* value = equals + 1;
            
            // Decode URL-encoded values (basic handling of %20, etc.)
            // For simplicity, just copy directly for now
            if (strcmp(key, "current_password") == 0) {
                strncpy(current_password, value, sizeof(current_password) - 1);
            } else if (strcmp(key, "new_password") == 0) {
                strncpy(new_password, value, sizeof(new_password) - 1);
            } else if (strcmp(key, "confirm_password") == 0) {
                strncpy(confirm_password, value, sizeof(confirm_password) - 1);
            }
        }
        token = strtok(NULL, "&");
    }
    
    free(form_copy);
    
    // Get current configuration
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    // Validate password change
    password_change_result_t result = http_validate_password_change(
        current_password,
        new_password,
        confirm_password,
        layout->config.admin_password
    );
    
    if (result != PWD_CHANGE_OK) {
        printf("HTTP Password Change: Validation failed with code %d\n", result);
        switch (result) {
            case PWD_CHANGE_CURRENT_WRONG:
                snprintf(error_msg, error_msg_size, "Current password is incorrect");
                break;
            case PWD_CHANGE_TOO_SHORT:
                snprintf(error_msg, error_msg_size, "New password must be at least 8 characters");
                break;
            case PWD_CHANGE_TOO_LONG:
                snprintf(error_msg, error_msg_size, "New password must be at most 31 characters");
                break;
            case PWD_CHANGE_NO_MATCH:
                snprintf(error_msg, error_msg_size, "New password and confirmation do not match");
                break;
            case PWD_CHANGE_EMPTY_FIELD:
                snprintf(error_msg, error_msg_size, "All password fields are required");
                break;
            default:
                snprintf(error_msg, error_msg_size, "Password validation failed");
                break;
        }
        return false;
    }
    
    // Password validation successful - update stored password
    strncpy(layout->config.admin_password, new_password, sizeof(layout->config.admin_password) - 1);
    layout->config.admin_password[sizeof(layout->config.admin_password) - 1] = '\0';
    
    // Increment revision counter and save to flash
    layout->revision_counter++;
    bool save_result = flash_persistence_force_save_configuration();
    
    printf("HTTP Password Change: Password updated successfully, flash save: %s\n", 
           save_result ? "success" : "failed");
    
    if (save_result) {
        snprintf(success_msg, success_msg_size, "Password changed successfully");
    } else {
        snprintf(error_msg, error_msg_size, "Password changed but flash save failed");
    }
    
    return save_result;
}

/**
 * @brief Parse POST form data and update configuration
 * 
 * Parses configuration POST data and updates system configuration.
 * Handles network settings, UART channel configuration, etc.
 * 
 * @param post_data Raw POST request data
 * @param data_len Length of POST data
 * @return true if configuration updated successfully, false on error
 */
bool http_parse_post_data(const char* post_data, size_t data_len,
                          char* error_msg, size_t error_msg_size,
                          char* success_msg, size_t success_msg_size) {
    (void)data_len;
    // Find start of form data (after double CRLF)
    const char* form_start = strstr(post_data, "\r\n\r\n");
    if (!form_start) {
        snprintf(error_msg, error_msg_size, "Invalid form data");
        return false;
    }
    form_start += 4; // Skip past \r\n\r\n
    
    printf("HTTP: Parsing form data: %s\n", form_start);
    
    // Get current configuration
    shared_memory_layout_t* layout = shared_memory_get_layout();
    bool config_changed = false;
    
    DEBUG_ONLY( printf("HTTP: Parsing configuration form data\n") );
    
    // CRITICAL FIX: Reset ALL checkbox flags first 
    // (unchecked checkboxes don't appear in POST data)
    
    // Save previous states for change detection
    bool channel_enabled_before[5] = {
        layout->config.channels[0].enabled,
        layout->config.channels[1].enabled, 
        layout->config.channels[2].enabled,
        layout->config.channels[3].enabled,
        layout->config.channels[4].enabled
    };
    bool dhcp_enabled_before = layout->config.network.use_dhcp;
    
    // Reset all checkboxes to false first, then enable only checked ones
    layout->config.network.use_dhcp = false;  // CRITICAL FIX: Reset DHCP checkbox
    for (int ch = 1; ch <= 4; ch++) {
#if DEVICE_CHANNEL_4_ENABLED
        // UART4 is always enabled in SHARK mode (no checkbox on config page)
        if (ch == CHANNEL_4) continue;
#endif
        layout->config.channels[ch].enabled = false;  // Reset UART channel checkboxes
    }
    
    // Parse form fields - simple key=value&key=value parsing
    char* form_copy = malloc(strlen(form_start) + 1);
    if (!form_copy) return false;
    strcpy(form_copy, form_start);
    
    char* token = strtok(form_copy, "&");
    while (token) {
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char* key = token;
            char* value = equals + 1;
            
            // Parse network settings
            if (strcmp(key, "static_ip") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 &&
                    a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
                    c >= 0 && c <= 255 && d >= 0 && d <= 255) {
                    IP4_ADDR(&layout->config.network.static_ip, a, b, c, d);
                    config_changed = true;
                    printf("HTTP: Updated static IP to %d.%d.%d.%d\n", a, b, c, d);
                } else {
                    printf("HTTP: Invalid static IP: %s\n", value);
                }
            } else if (strcmp(key, "static_netmask") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 &&
                    a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
                    c >= 0 && c <= 255 && d >= 0 && d <= 255) {
                    IP4_ADDR(&layout->config.network.static_netmask, a, b, c, d);
                    config_changed = true;
                    printf("HTTP: Updated netmask to %d.%d.%d.%d\n", a, b, c, d);
                } else {
                    printf("HTTP: Invalid netmask: %s\n", value);
                }
            } else if (strcmp(key, "static_gateway") == 0) {
                int a, b, c, d;
                if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4 &&
                    a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
                    c >= 0 && c <= 255 && d >= 0 && d <= 255) {
                    IP4_ADDR(&layout->config.network.static_gateway, a, b, c, d);
                    config_changed = true;
                    printf("HTTP: Updated gateway to %d.%d.%d.%d\n", a, b, c, d);
                } else {
                    printf("HTTP: Invalid gateway: %s\n", value);
                }
            } else if (strcmp(key, "use_dhcp") == 0 && strcmp(value, "1") == 0) {
                // Enable DHCP only if checkbox was checked (appears in POST data with value "1")
                layout->config.network.use_dhcp = true;
                printf("HTTP: DHCP ENABLED (checkbox checked)\n");
            } /*else if (strcmp(key, "mac_addr") == 0) {
                // Parse MAC address (format: 02:00:00:00:00:01)
                int m[6];
                if (sscanf(value, "%02x:%02x:%02x:%02x:%02x:%02x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
                    for (int i = 0; i < 6; i++) {
                        layout->config.network.mac_address[i] = (uint8_t)m[i];
                    }
                    config_changed = true;
                    printf("HTTP: Updated MAC address\n");
                }
            }*/
            
            // Parse UART channel settings (ch1_port, ch1_enabled, etc.)
            for (int ch = 1; ch <= 4; ch++) {
                char field_name[16];
                
                snprintf(field_name, sizeof(field_name), "ch%d_port", ch);
                if (strcmp(key, field_name) == 0) {
                    int port = atoi(value);
                    if (port >= 1024 && port <= 65535) {
                        layout->config.channels[ch].tcp_port = port;
                        config_changed = true;
                        printf("HTTP: Updated channel %d port to %d\n", ch, port);
                    }
                }
                
                // Enable channel if checkbox was checked (appears in POST data)
                snprintf(field_name, sizeof(field_name), "ch%d_enabled", ch);
                if (strcmp(key, field_name) == 0 && strcmp(value, "1") == 0) {
                    layout->config.channels[ch].enabled = true;
                    printf("HTTP: Channel %d ENABLED (checkbox checked)\n", ch);
                }
            }
        }
        token = strtok(NULL, "&");
    }
    
    // Check which settings changed state and mark config as changed
    
    // Check DHCP setting change
    if (dhcp_enabled_before != layout->config.network.use_dhcp) {
        config_changed = true;
        printf("HTTP: DHCP changed: %s -> %s\n",
               dhcp_enabled_before ? "ENABLED" : "DISABLED",
               layout->config.network.use_dhcp ? "ENABLED" : "DISABLED");
    }
    
    // Check channel changes
    for (int ch = 1; ch <= 4; ch++) {
        if (channel_enabled_before[ch] != layout->config.channels[ch].enabled) {
            config_changed = true;
            printf("HTTP: Channel %d changed: %s -> %s\n", ch,
                   channel_enabled_before[ch] ? "ENABLED" : "DISABLED",
                   layout->config.channels[ch].enabled ? "ENABLED" : "DISABLED");
        }
    }
    
    free(form_copy);
    
    // Save configuration to flash if changes were made
    if (config_changed) {
        layout->revision_counter++;
        layout->config_change_pending = true;  // Signal Core1 to apply changes
        
        bool save_result = flash_persistence_force_save_configuration();
        printf("HTTP: Configuration save result: %s\n", save_result ? "success" : "failed");
        printf("HTTP: Configuration change signaled to Core1 for runtime update\n");
        printf("HTTP: Note: Network changes (IP/DHCP/MAC) will be applied immediately\n");
        
        if (save_result) {
            snprintf(success_msg, success_msg_size, "Configuration saved successfully");
        } else {
            snprintf(error_msg, error_msg_size, "Configuration changed but flash save failed");
        }
    } else {
        snprintf(success_msg, success_msg_size, "No changes detected");
    }
    
    return config_changed;
}
