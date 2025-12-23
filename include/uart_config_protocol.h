/**
 * @file uart_config_protocol.h
 * @brief UART Configuration Protocol - allows configuration via UART commands
 * 
 * Command Format:
 *   Read:  #CFG:GET:<parameter>!(CR)(LF)
 *   Write: #CFG:SET:<parameter>=<value>!(CR)(LF)
 * 
 * Response Format:
 *   Success: #CFG:OK:<parameter>=<value>!(CR)(LF)
 *   Error:   #CFG:ERR:<code>:<message>!(CR)(LF)
 * 
 * Supported Parameters:
 *   NET.DHCP, NET.IP, NET.MASK, NET.GW, NET.MAC
 *   CH1.EN, CH1.PORT, CH1.BAUD (also CH2, CH3)
 *   SYS.SAVE, SYS.REBOOT, SYS.FACTORY, SYS.VERSION
 */

#ifndef UART_CONFIG_PROTOCOL_H
#define UART_CONFIG_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "shared_memory.h"

// Protocol constants
#define CFG_PREFIX          "#CFG:"
#define CFG_PREFIX_LEN      5
#define CFG_CMD_GET         "GET:"
#define CFG_CMD_SET         "SET:"
#define CFG_CMD_LEN         4
#define CFG_MAX_PARAM_LEN   32
#define CFG_MAX_VALUE_LEN   64
#define CFG_MAX_RESPONSE    128

// Error codes
typedef enum {
    CFG_ERR_OK = 0,
    CFG_ERR_UNKNOWN_PARAM = 1,
    CFG_ERR_INVALID_VALUE = 2,
    CFG_ERR_READ_ONLY = 3,
    CFG_ERR_OUT_OF_RANGE = 4,
    CFG_ERR_BUSY = 5,
    CFG_ERR_PARSE_ERROR = 6,
    CFG_ERR_NOT_SAVED = 7
} cfg_error_t;

/**
 * @brief Initialize the UART config protocol module
 * @return true if initialization successful
 */
bool uart_config_protocol_init(void);

/**
 * @brief Check if a message is a configuration command
 * @param data Pointer to message data
 * @param length Length of message
 * @return true if message starts with #CFG:
 */
bool uart_config_is_command(const uint8_t* data, size_t length);

/**
 * @brief Process a configuration command
 * @param channel The UART channel the command came from (1-3, channel 0 is ignored)
 * @param data Pointer to complete message including #CFG: prefix
 * @param length Length of message
 * @param response Buffer to store response (must be at least CFG_MAX_RESPONSE bytes)
 * @param response_len Pointer to store response length
 * @return true if command was processed (response generated), false if not a valid command
 */
bool uart_config_process_command(channel_id_t channel, const uint8_t* data, size_t length,
                                  uint8_t* response, size_t* response_len);

/**
 * @brief Check if there are pending configuration changes
 * @return true if changes are pending (need SYS.SAVE)
 */
bool uart_config_has_pending_changes(void);

/**
 * @brief Get error message string
 * @param error Error code
 * @return Human-readable error string
 */
const char* uart_config_error_string(cfg_error_t error);

#endif // UART_CONFIG_PROTOCOL_H
