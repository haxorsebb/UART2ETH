/**
 * @file http_auth.h
 * @brief HTTP Authentication Module
 * 
 * Provides HTTP Basic Authentication implementation with Base64
 * encoding/decoding support for the UART2ETH web interface.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization - Phase 2
 * - ADR-016: HTTP Basic Authentication
 */

#ifndef HTTP_AUTH_H
#define HTTP_AUTH_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declaration for connection type
typedef struct http_connection http_connection_t;

/**
 * @brief Decode Base64 encoded string
 * 
 * Decodes a Base64 encoded string to plain text bytes.
 * Commonly used for decoding HTTP Basic Authentication credentials.
 * 
 * @param input Base64 encoded input string
 * @param output Buffer to store decoded output
 * @param max_len Maximum length of output buffer
 * @return Number of bytes decoded, or <=0 on error
 */
int http_base64_decode(const char* input, char* output, size_t max_len);

/**
 * @brief Encode binary data to Base64 string
 * 
 * Encodes binary data to Base64 ASCII string.
 * 
 * @param input Binary input data
 * @param input_len Length of input data in bytes
 * @param output Buffer to store Base64 encoded output
 * @param max_len Maximum length of output buffer
 * @return Number of characters written, or <=0 on error
 */
int http_base64_encode(const uint8_t* input, size_t input_len, char* output, size_t max_len);

/**
 * @brief Check HTTP Basic Authentication credentials
 * 
 * Validates HTTP Basic Authentication header against expected password.
 * Extracts and decodes the Authorization header, then compares the
 * password portion with the expected value.
 * 
 * Format: Authorization: Basic base64(username:password)
 * 
 * @param request HTTP request string containing headers
 * @param expected_password Expected password for comparison
 * @return true if authentication successful, false otherwise
 */
bool http_check_authentication(const char* request, const char* expected_password);

/**
 * @brief Send HTTP 401 Unauthorized response
 * 
 * Sends HTTP 401 response with WWW-Authenticate header to trigger
 * browser's Basic Authentication dialog.
 * 
 * @param conn HTTP connection to send response on
 */
void http_send_auth_required(http_connection_t* conn);

#endif // HTTP_AUTH_H
