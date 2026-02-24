/**
 * @file uart_config_protocol.c
 * @brief UART Configuration Protocol implementation
 
Command Format:

Read: #CFG:GET:<parameter>!\r\n
Write: #CFG:SET:<parameter>=<value>!\r\n
Wildcard: #CFG:GET:NET.*!\r\n (returns all NET parameters)

Response Format:

Success: #CFG:OK:<parameter>=<value>!\r\n
Error: #CFG:ERR:<code>:<message>!\r\n

Supported Parameters:
Parameter                       Type        Description
CUR.DHCP                        read only   DHCP enabled
CUR.IP                          read only   Static IP address
CUR.MASK                        read only   Subnet mask
CUR.GW                          read only   Gateway
CUR.MAC                         read only   MAC addressNET.DHCP                        0/1         DHCP enabled
NET.IP                          IP          Static IP address
NET.MASK                        IP          Subnet mask
NET.GW                          IP          Gateway
NET.MAC                         MAC         MAC address
CH1.EN / CH2.EN / CH3.EN        0/1         Channel enabled
CH1.PORT / CH2.PORT / CH3.PORT  1-65535     TCP port
CH1.BAUD / CH2.BAUD / CH3.BAUD  300-921600  Baud rate
SYS.VERSION                     readonly    Firmware version
SYS.SAVE                        action      Save config and apply
SYS.REBOOT                      action      Reboot device
SYS.FACTORY                     action      Factory reset

Behavior:

Channels: Works on UART channels 1, 2, 3 only (channel 0/debug UART is excluded)
Command detection: Only #CFG:GET: and #CFG:SET: are intercepted; responses (#CFG:OK:, #CFG:ERR:) pass through to TCP
(this is done this way so that the functions can be tested by inserting commands on the Ethernet side and let them
loopback into the UART side)
No pass-through: Config commands are processed locally and NOT forwarded to Ethernet
Response: Sent back on the same UART channel the command was received on
Pending changes: SET commands modify memory; changes take effect after SYS.SAVE
Apply on save: SYS.SAVE saves to flash AND immediately applies network/TCP server changes (no reboot required)


*/

#include "uart_config_protocol.h"
#include "shared_memory.h"
#include "network/enc28j60_driver.h"
#include "network/network_manager.h"
#include "hardware/watchdog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Firmware version
#define FIRMWARE_VERSION "1.0.0"

// Module state
static bool g_initialized = false;
static bool g_pending_changes = false;

// Forward declarations
static cfg_error_t parse_command(const char* cmd, char* param, char* value, bool* is_get);
static cfg_error_t handle_get(const char* param, char* value);
static cfg_error_t handle_set(const char* param, const char* value);
static void format_response_ok(const char* param, const char* value, uint8_t* response, size_t* len);
static void format_response_error(cfg_error_t error, uint8_t* response, size_t* len);
static void format_response_multi(const char* params[], const char* values[], int count, 
                                   uint8_t* response, size_t* len);

// Parameter handlers
static cfg_error_t get_net_param(const char* param, char* value);
static cfg_error_t set_net_param(const char* param, const char* value);
static cfg_error_t get_current_param(const char* param, char* value);  // CUR.* - read-only current values
static cfg_error_t get_ch_param(int channel, const char* param, char* value);
static cfg_error_t set_ch_param(int channel, const char* param, const char* value);
static cfg_error_t handle_sys_command(const char* param, bool is_get, char* value);

// Utility functions
static bool parse_ip_address(const char* str, uint32_t* ip_out);
static void format_ip_address(uint32_t ip, char* str);
static bool parse_mac_address(const char* str, uint8_t mac[6]);
static void format_mac_address(const uint8_t mac[6], char* str);

bool uart_config_protocol_init(void) {
    g_initialized = true;
    g_pending_changes = false;
    return true;
}

bool uart_config_is_command(const uint8_t* data, size_t length) {
    // Must have at least "#CFG:GET:" or "#CFG:SET:" (9 chars minimum)
    if (!data || length < CFG_PREFIX_LEN + CFG_CMD_LEN) {
        // printf("CFG_IS_CMD: too short, len=%zu, need=%d\n", length, CFG_PREFIX_LEN + CFG_CMD_LEN);
        return false;
    }
    
    // Check for #CFG: prefix
    if (memcmp(data, CFG_PREFIX, CFG_PREFIX_LEN) != 0) {
        /*
        printf("CFG_IS_CMD: prefix mismatch, got='%.*s' (hex: %02X %02X %02X %02X %02X)\n", 
               CFG_PREFIX_LEN, data, data[0], data[1], data[2], data[3], data[4]);
        */
        return false;
    }
    
    // Must be GET: or SET: command, not a response (OK: or ERR:)
    const char* cmd_part = (const char*)(data + CFG_PREFIX_LEN);
    bool is_get = (strncmp(cmd_part, CFG_CMD_GET, CFG_CMD_LEN) == 0);
    bool is_set = (strncmp(cmd_part, CFG_CMD_SET, CFG_CMD_LEN) == 0);
    
    if (!is_get && !is_set) {
        /*
        printf("CFG_IS_CMD: not GET/SET, got='%.*s' (hex: %02X %02X %02X %02X)\n",
               CFG_CMD_LEN, cmd_part, cmd_part[0], cmd_part[1], cmd_part[2], cmd_part[3]);
        */
    }
    
    return is_get || is_set;
}

bool uart_config_process_command(channel_id_t channel, const uint8_t* data, size_t length,
                                  uint8_t* response, size_t* response_len) {
    if (!g_initialized || !data || !response || !response_len) {
        return false;
    }
    
    // Channel 0 (debug UART) is not allowed to configure
    if (channel == CHANNEL_0) {
        return false;
    }
    
    // Verify it's a config command
    if (!uart_config_is_command(data, length)) {
        return false;
    }
    
    // Convert to null-terminated string (strip trailing !\r\n)
    char cmd_buffer[256];
    size_t cmd_len = length;
    
    // Strip trailing !\r\n
    while (cmd_len > 0 && (data[cmd_len-1] == '\n' || data[cmd_len-1] == '\r' || data[cmd_len-1] == '!')) {
        cmd_len--;
    }
    
    if (cmd_len >= sizeof(cmd_buffer)) {
        format_response_error(CFG_ERR_PARSE_ERROR, response, response_len);
        return true;
    }
    
    memcpy(cmd_buffer, data, cmd_len);
    cmd_buffer[cmd_len] = '\0';
    
    // Parse command (skip #CFG: prefix)
    char param[CFG_MAX_PARAM_LEN] = {0};
    char value[CFG_MAX_VALUE_LEN] = {0};
    bool is_get = false;
    
    cfg_error_t err = parse_command(cmd_buffer + CFG_PREFIX_LEN, param, value, &is_get);
    if (err != CFG_ERR_OK) {
        format_response_error(err, response, response_len);
        return true;
    }
    
    // Handle wildcard GET (e.g., NET.*)
    if (is_get && strlen(param) >= 2 && param[strlen(param)-1] == '*') {
        // Get prefix without wildcard
        char prefix[CFG_MAX_PARAM_LEN];
        strncpy(prefix, param, strlen(param) - 1);
        prefix[strlen(param) - 1] = '\0';
        
        // Collect matching parameters
        const char* params[20];
        const char* values[20];
        static char value_storage[20][CFG_MAX_VALUE_LEN];
        int count = 0;
        
        if (strcmp(prefix, "NET.") == 0) {
            static const char* net_params[] = {"NET.DHCP", "NET.IP", "NET.MASK", "NET.GW", "NET.MAC", "NET.ETH"};
            for (int i = 0; i < 6 && count < 20; i++) {
                if (get_net_param(net_params[i] + 4, value_storage[count]) == CFG_ERR_OK) {
                    params[count] = net_params[i];
                    values[count] = value_storage[count];
                    count++;
                }
            }
        } else if (strcmp(prefix, "CUR.") == 0) {
            // Current/active network parameters
            static const char* cur_params[] = {"CUR.IP", "CUR.MASK", "CUR.GW", "CUR.MAC", "CUR.DHCP"};
            static const char* cur_short[] = {"IP", "MASK", "GW", "MAC", "DHCP"};
            for (int i = 0; i < 5 && count < 20; i++) {
                if (get_current_param(cur_short[i], value_storage[count]) == CFG_ERR_OK) {
                    params[count] = cur_params[i];
                    values[count] = value_storage[count];
                    count++;
                }
            }
        } else if (strncmp(prefix, "CH", 2) == 0 && prefix[2] >= '1' && prefix[2] <= '3') {
            int ch = prefix[2] - '0';
            static const char* ch_params[] = {"EN", "PORT", "BAUD"};
            char full_param[16];
            for (int i = 0; i < 3 && count < 20; i++) {
                snprintf(full_param, sizeof(full_param), "CH%d.%s", ch, ch_params[i]);
                if (get_ch_param(ch, ch_params[i], value_storage[count]) == CFG_ERR_OK) {
                    params[count] = full_param;
                    values[count] = value_storage[count];
                    count++;
                }
            }
        }
        
        if (count > 0) {
            format_response_multi(params, values, count, response, response_len);
            return true;
        }
    }
    
    // Handle single parameter
    if (is_get) {
        err = handle_get(param, value);
        if (err == CFG_ERR_OK) {
            format_response_ok(param, value, response, response_len);
        } else {
            format_response_error(err, response, response_len);
        }
    } else {
        err = handle_set(param, value);
        if (err == CFG_ERR_OK) {
            // For SET, read back the value
            char readback[CFG_MAX_VALUE_LEN] = {0};
            handle_get(param, readback);
            format_response_ok(param, readback[0] ? readback : value, response, response_len);
        } else {
            format_response_error(err, response, response_len);
        }
    }
    
    return true;
}

bool uart_config_has_pending_changes(void) {
    return g_pending_changes;
}

const char* uart_config_error_string(cfg_error_t error) {
    switch (error) {
        case CFG_ERR_OK:            return "OK";
        case CFG_ERR_UNKNOWN_PARAM: return "UNKNOWN_PARAM";
        case CFG_ERR_INVALID_VALUE: return "INVALID_VALUE";
        case CFG_ERR_READ_ONLY:     return "READ_ONLY";
        case CFG_ERR_OUT_OF_RANGE:  return "OUT_OF_RANGE";
        case CFG_ERR_BUSY:          return "BUSY";
        case CFG_ERR_PARSE_ERROR:   return "PARSE_ERROR";
        case CFG_ERR_NOT_SAVED:     return "NOT_SAVED";
        default:                    return "UNKNOWN";
    }
}

// --- Internal functions ---

static cfg_error_t parse_command(const char* cmd, char* param, char* value, bool* is_get) {
    if (!cmd || !param || !value || !is_get) {
        return CFG_ERR_PARSE_ERROR;
    }
    
    // Check for GET: or SET:
    if (strncmp(cmd, CFG_CMD_GET, CFG_CMD_LEN) == 0) {
        *is_get = true;
        cmd += CFG_CMD_LEN;
    } else if (strncmp(cmd, CFG_CMD_SET, CFG_CMD_LEN) == 0) {
        *is_get = false;
        cmd += CFG_CMD_LEN;
    } else {
        return CFG_ERR_PARSE_ERROR;
    }
    
    // For GET, the rest is the parameter
    // For SET, split on '='
    if (*is_get) {
        strncpy(param, cmd, CFG_MAX_PARAM_LEN - 1);
        param[CFG_MAX_PARAM_LEN - 1] = '\0';
        value[0] = '\0';
    } else {
        const char* eq = strchr(cmd, '=');
        if (!eq) {
            // SET without value (action commands like SYS.SAVE)
            strncpy(param, cmd, CFG_MAX_PARAM_LEN - 1);
            param[CFG_MAX_PARAM_LEN - 1] = '\0';
            value[0] = '\0';
        } else {
            size_t param_len = eq - cmd;
            if (param_len >= CFG_MAX_PARAM_LEN) {
                return CFG_ERR_PARSE_ERROR;
            }
            strncpy(param, cmd, param_len);
            param[param_len] = '\0';
            strncpy(value, eq + 1, CFG_MAX_VALUE_LEN - 1);
            value[CFG_MAX_VALUE_LEN - 1] = '\0';
        }
    }
    
    // Convert parameter to uppercase for case-insensitive matching
    for (char* p = param; *p; p++) {
        if (*p >= 'a' && *p <= 'z') {
            *p = *p - 'a' + 'A';
        }
    }
    
    return CFG_ERR_OK;
}

static cfg_error_t handle_get(const char* param, char* value) {
    // Network configuration parameters (stored in flash)
    if (strncmp(param, "NET.", 4) == 0) {
        return get_net_param(param + 4, value);
    }
    
    // Current/active network parameters (read-only, actual values in use)
    if (strncmp(param, "CUR.", 4) == 0) {
        return get_current_param(param + 4, value);
    }
    
    // Channel parameters (CH1, CH2, CH3)
    if (strncmp(param, "CH", 2) == 0 && param[2] >= '1' && param[2] <= '3' && param[3] == '.') {
        int channel = param[2] - '0';
        return get_ch_param(channel, param + 4, value);
    }
    
    // System parameters
    if (strncmp(param, "SYS.", 4) == 0) {
        return handle_sys_command(param + 4, true, value);
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static cfg_error_t handle_set(const char* param, const char* value) {
    // Network configuration parameters
    if (strncmp(param, "NET.", 4) == 0) {
        cfg_error_t err = set_net_param(param + 4, value);
        if (err == CFG_ERR_OK) {
            g_pending_changes = true;
        }
        return err;
    }
    
    // Current parameters are read-only
    if (strncmp(param, "CUR.", 4) == 0) {
        return CFG_ERR_READ_ONLY;
    }
    
    // Channel parameters (CH1, CH2, CH3)
    if (strncmp(param, "CH", 2) == 0 && param[2] >= '1' && param[2] <= '3' && param[3] == '.') {
        int channel = param[2] - '0';
        cfg_error_t err = set_ch_param(channel, param + 4, value);
        if (err == CFG_ERR_OK) {
            g_pending_changes = true;
        }
        return err;
    }
    
    // System parameters
    if (strncmp(param, "SYS.", 4) == 0) {
        return handle_sys_command(param + 4, false, (char*)value);
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static cfg_error_t get_net_param(const char* param, char* value) {
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        return CFG_ERR_BUSY;
    }
    
    if (strcmp(param, "DHCP") == 0) {
        sprintf(value, "%d", layout->config.network.use_dhcp ? 1 : 0);
        return CFG_ERR_OK;
    }
    if (strcmp(param, "IP") == 0) {
        format_ip_address(layout->config.network.static_ip.addr, value);
        return CFG_ERR_OK;
    }
    if (strcmp(param, "MASK") == 0) {
        format_ip_address(layout->config.network.static_netmask.addr, value);
        return CFG_ERR_OK;
    }
    if (strcmp(param, "GW") == 0) {
        format_ip_address(layout->config.network.static_gateway.addr, value);
        return CFG_ERR_OK;
    }
    if (strcmp(param, "MAC") == 0) {
        format_mac_address(layout->config.network.mac_address, value);
        return CFG_ERR_OK;
    }
    if (strcmp(param, "ETH") == 0) {
        sprintf(value, "%d", enc28j60_is_ready() ? 1 : 0);
        return CFG_ERR_OK;
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static cfg_error_t set_net_param(const char* param, const char* value) {
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        return CFG_ERR_BUSY;
    }
    
    if (strcmp(param, "DHCP") == 0) {
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0) {
            layout->config.network.use_dhcp = (value[0] == '1');
            return CFG_ERR_OK;
        }
        return CFG_ERR_INVALID_VALUE;
    }
    if (strcmp(param, "IP") == 0) {
        uint32_t ip;
        if (parse_ip_address(value, &ip)) {
            layout->config.network.static_ip.addr = ip;
            return CFG_ERR_OK;
        }
        return CFG_ERR_INVALID_VALUE;
    }
    if (strcmp(param, "MASK") == 0) {
        uint32_t mask;
        if (parse_ip_address(value, &mask)) {
            layout->config.network.static_netmask.addr = mask;
            return CFG_ERR_OK;
        }
        return CFG_ERR_INVALID_VALUE;
    }
    if (strcmp(param, "GW") == 0) {
        uint32_t gw;
        if (parse_ip_address(value, &gw)) {
            layout->config.network.static_gateway.addr = gw;
            return CFG_ERR_OK;
        }
        return CFG_ERR_INVALID_VALUE;
    }
    if (strcmp(param, "MAC") == 0) {
        if (parse_mac_address(value, layout->config.network.mac_address)) {
            return CFG_ERR_OK;
        }
        return CFG_ERR_INVALID_VALUE;
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static cfg_error_t get_current_param(const char* param, char* value) {
    // CUR.* parameters - read-only current/active network values
    
    if (strcmp(param, "IP") == 0) {
        simple_ip_addr_t ip;
        if (network_manager_get_ip_address(&ip)) {
            format_ip_address(ip.addr, value);
            return CFG_ERR_OK;
        }
        strcpy(value, "0.0.0.0");
        return CFG_ERR_OK;
    }
    
    if (strcmp(param, "MASK") == 0) {
        network_stats_t stats;
        network_manager_get_stats(&stats);
        format_ip_address(stats.current_netmask.addr, value);
        return CFG_ERR_OK;
    }
    
    if (strcmp(param, "GW") == 0) {
        network_stats_t stats;
        network_manager_get_stats(&stats);
        format_ip_address(stats.current_gateway.addr, value);
        return CFG_ERR_OK;
    }
    
    if (strcmp(param, "MAC") == 0) {
        uint8_t mac[6];
        network_manager_get_mac_address(mac);
        format_mac_address(mac, value);
        return CFG_ERR_OK;
    }
    
    if (strcmp(param, "DHCP") == 0) {
        // Returns 1 if current IP was obtained via DHCP, 0 if static
        sprintf(value, "%d", network_manager_is_dhcp_bound() ? 1 : 0);
        return CFG_ERR_OK;
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static cfg_error_t get_ch_param(int channel, const char* param, char* value) {
    if (channel < 1 || channel > 4) {
        return CFG_ERR_UNKNOWN_PARAM;
    }
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        return CFG_ERR_BUSY;
    }
    
    channel_config_t* ch = &layout->config.channels[channel];
    
    if (strcmp(param, "EN") == 0) {
        sprintf(value, "%d", ch->enabled ? 1 : 0);
        return CFG_ERR_OK;
    }
    if (strcmp(param, "PORT") == 0) {
        sprintf(value, "%u", ch->tcp_port);
        return CFG_ERR_OK;
    }
    if (strcmp(param, "BAUD") == 0) {
        sprintf(value, "%u", ch->baud_rate);
        return CFG_ERR_OK;
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static cfg_error_t set_ch_param(int channel, const char* param, const char* value) {
    if (channel < 1 || channel > 4) {
        return CFG_ERR_UNKNOWN_PARAM;
    }
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    if (!layout) {
        return CFG_ERR_BUSY;
    }
    
    channel_config_t* ch = &layout->config.channels[channel];
    
    if (strcmp(param, "EN") == 0) {
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0) {
            ch->enabled = (value[0] == '1');
            return CFG_ERR_OK;
        }
        return CFG_ERR_INVALID_VALUE;
    }
    if (strcmp(param, "PORT") == 0) {
        int port = atoi(value);
        if (port >= 1 && port <= 65535) {
            ch->tcp_port = (uint16_t)port;
            return CFG_ERR_OK;
        }
        return CFG_ERR_OUT_OF_RANGE;
    }
    if (strcmp(param, "BAUD") == 0) {
        int baud = atoi(value);
        if (baud >= 300 && baud <= 921600) {
            ch->baud_rate = (uint32_t)baud;
            return CFG_ERR_OK;
        }
        return CFG_ERR_OUT_OF_RANGE;
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static cfg_error_t handle_sys_command(const char* param, bool is_get, char* value) {
    shared_memory_layout_t* layout = shared_memory_get_layout();
    
    if (strcmp(param, "VERSION") == 0) {
        if (is_get) {
            strcpy(value, FIRMWARE_VERSION);
            return CFG_ERR_OK;
        }
        return CFG_ERR_READ_ONLY;
    }
    
    if (strcmp(param, "SAVE") == 0) {
        if (!is_get) {
            // Increment revision counter to trigger save
            layout->revision_counter++;
            
            // Force save to flash
            if (flash_persistence_force_save_configuration()) {
                g_pending_changes = false;
                
                // Apply network changes immediately
                network_manager_reconfigure(&layout->config.network);
                
                // Signal config change for TCP server updates
                layout->config_change_pending = true;
                
                printf("UART CFG: Configuration saved and applied\n");
                strcpy(value, "SAVED");
                return CFG_ERR_OK;
            }
            return CFG_ERR_BUSY;
        }
        // GET SYS.SAVE returns pending status
        strcpy(value, g_pending_changes ? "PENDING" : "SAVED");
        return CFG_ERR_OK;
    }
    
    if (strcmp(param, "REBOOT") == 0) {
        if (!is_get) {
            printf("UART CFG: Reboot requested\n");
            // Use watchdog to trigger reboot
            watchdog_reboot(0, 0, 100);  // Reboot in 100ms
            strcpy(value, "REBOOTING");
            return CFG_ERR_OK;
        }
        return CFG_ERR_READ_ONLY;
    }
    
    if (strcmp(param, "FACTORY") == 0) {
        if (!is_get) {
            printf("UART CFG: Factory reset requested\n");
            flash_persistence_factory_reset();
            strcpy(value, "RESET");
            // Reboot to apply factory settings
            watchdog_reboot(0, 0, 100);
            return CFG_ERR_OK;
        }
        return CFG_ERR_READ_ONLY;
    }
    
    return CFG_ERR_UNKNOWN_PARAM;
}

static void format_response_ok(const char* param, const char* value, uint8_t* response, size_t* len) {
    *len = snprintf((char*)response, CFG_MAX_RESPONSE, "#CFG:OK:%s=%s!\r\n", param, value);
}

static void format_response_error(cfg_error_t error, uint8_t* response, size_t* len) {
    *len = snprintf((char*)response, CFG_MAX_RESPONSE, "#CFG:ERR:%d:%s!\r\n", 
                    error, uart_config_error_string(error));
}

static void format_response_multi(const char* params[], const char* values[], int count,
                                   uint8_t* response, size_t* len) {
    char* p = (char*)response;
    size_t remaining = CFG_MAX_RESPONSE;
    *len = 0;
    
    for (int i = 0; i < count && remaining > 30; i++) {
        int written = snprintf(p, remaining, "#CFG:OK:%s=%s!\r\n", params[i], values[i]);
        p += written;
        *len += written;
        remaining -= written;
    }
}

// --- Utility functions ---

static bool parse_ip_address(const char* str, uint32_t* ip_out) {
    unsigned int a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        if (a <= 255 && b <= 255 && c <= 255 && d <= 255) {
            *ip_out = (d << 24) | (c << 16) | (b << 8) | a;  // Little-endian
            return true;
        }
    }
    return false;
}

static void format_ip_address(uint32_t ip, char* str) {
    sprintf(str, "%u.%u.%u.%u",
            (ip >> 0) & 0xFF,
            (ip >> 8) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 24) & 0xFF);
}

static bool parse_mac_address(const char* str, uint8_t mac[6]) {
    unsigned int m[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            if (m[i] > 255) return false;
            mac[i] = (uint8_t)m[i];
        }
        return true;
    }
    return false;
}

static void format_mac_address(const uint8_t mac[6], char* str) {
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
