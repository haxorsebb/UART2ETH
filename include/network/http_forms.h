/**
 * @file http_forms.h
 * @brief HTTP Form Handling Module
 * 
 * Provides form data parsing, validation, and URL decoding utilities
 * for processing POST requests in the UART2ETH web interface.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization - Phase 3
 */

#ifndef HTTP_FORMS_H
#define HTTP_FORMS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief HTTP form field structure
 * 
 * Represents a single key-value pair from parsed form data.
 */
typedef struct {
    const char* key;    // Field name
    const char* value;  // Field value
} http_form_field_t;

/**
 * @brief Parse URL-encoded form data
 * 
 * Parses application/x-www-form-urlencoded form data into
 * an array of key-value pairs.
 * 
 * @param form_data Raw form data string
 * @param fields Output array for parsed fields
 * @param max_fields Maximum number of fields to parse
 * @param field_count Output: actual number of fields parsed
 * @return true if parsing successful, false on error
 */
bool http_parse_form_data(const char* form_data, http_form_field_t* fields, 
                          size_t max_fields, size_t* field_count);

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
const char* http_get_form_field(const http_form_field_t* fields, size_t field_count, const char* key);

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
                            const char* key, const char* value);

/**
 * @brief Decode URL-encoded string in place
 * 
 * Decodes URL percent-encoding (%20, %2B, etc.) in the string.
 * Modifies the string in place.
 * 
 * @param str String to decode (modified in place)
 */
void http_url_decode(char* str);

/**
 * @brief Parse configuration POST data and update system
 * 
 * Parses POST form data for configuration changes and updates
 * the system configuration accordingly. Handles network settings,
 * UART channel configuration, etc.
 * 
 * @param post_data Raw POST request data
 * @param data_len Length of POST data
 * @param error_msg Buffer for error message (filled on failure)
 * @param error_msg_size Size of error message buffer
 * @param success_msg Buffer for success message (filled on success)
 * @param success_msg_size Size of success message buffer
 * @return true if configuration updated successfully, false on error
 */
bool http_parse_post_data(const char* post_data, size_t data_len,
                          char* error_msg, size_t error_msg_size,
                          char* success_msg, size_t success_msg_size);

/**
 * @brief Handle password change request
 * 
 * Parses and processes password change form data. Validates current
 * password, checks new password requirements, and updates if valid.
 * 
 * @param post_data Raw POST request data
 * @param data_len Length of POST data
 * @param error_msg Buffer for error message (filled on failure)
 * @param error_msg_size Size of error message buffer
 * @param success_msg Buffer for success message (filled on success)
 * @param success_msg_size Size of success message buffer
 * @return true if password changed successfully, false on error
 */
bool http_handle_password_change(const char* post_data, size_t data_len,
                                 char* error_msg, size_t error_msg_size,
                                 char* success_msg, size_t success_msg_size);

#endif // HTTP_FORMS_H
