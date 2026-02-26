/**
 * @file page_factory.h
 * @brief Factory defaults page generation for HTTP server (manufacturing only)
 * 
 * Generates the factory programming page with forms for setting serial
 * numbers, MAC addresses, and default configuration. Only compiled when
 * FACTORY_INTERNAL_VERSION is defined.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 * - ADR-015: Factory Defaults Web Interface
 */

#ifndef PAGE_FACTORY_H
#define PAGE_FACTORY_H

#include <stddef.h>
#include <stdbool.h>

#ifdef FACTORY_INTERNAL_VERSION

/**
 * @brief Generate factory defaults configuration page
 * 
 * Creates complete HTTP response with HTML page for manufacturing use.
 * Includes forms for:
 * - Serial number programming (year/week/unique number)
 * - MAC address assignment
 * - Board type selection
 * - Default network configuration  
 * - Default admin password
 * 
 * Also displays currently programmed values for verification.
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * @param error_msg Error message to display (NULL for none)
 * @param error_msg_size Size of error message
 * @param success_msg Success message to display (NULL for none)
 * @param success_msg_size Size of success message
 * 
 * Documentation Reference: ADR-018, ADR-015
 */
void http_generate_factory_page(char* buffer, size_t buffer_size, 
                                  const char* error_msg, size_t error_msg_size,
                                  const char* success_msg, size_t success_msg_size);

/**
 * @brief Parse factory defaults POST data and write to flash
 * 
 * Parses form submission data, validates all fields, and writes
 * factory defaults to flash memory. Performs verification after write.
 * 
 * @param post_data HTTP POST request data
 * @param data_len Length of POST data
 * @param error_msg Buffer for error messages
 * @param error_msg_size Size of error message buffer
 * @param success_msg Buffer for success messages
 * @param success_msg_size Size of success message buffer
 * @return true if factory defaults written successfully, false on error
 * 
 * Documentation Reference: ADR-015
 */
bool http_parse_factory_post_data(const char* post_data, size_t data_len,
                                    char* error_msg, size_t error_msg_size,
                                    char* success_msg, size_t success_msg_size);

#endif // FACTORY_INTERNAL_VERSION

#endif // PAGE_FACTORY_H
