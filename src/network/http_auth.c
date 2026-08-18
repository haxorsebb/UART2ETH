/**
 * @file http_auth.c
 * @brief HTTP Authentication Module Implementation
 * 
 * Implements HTTP Basic Authentication with Base64 encoding/decoding
 * for the UART2ETH web interface security.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization - Phase 2
 * - ADR-016: HTTP Basic Authentication
 */

#include "network/http_auth.h"
#include "network/http_server.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// External function from http_server.c for sending responses
extern void http_send_response(http_connection_t* conn, const char* response, size_t response_len);

/**
 * @brief Decode Base64 encoded string
 * 
 * Decodes a Base64 encoded string to plain text bytes.
 * Used for decoding HTTP Basic Authentication credentials.
 * 
 * @param input Base64 encoded string
 * @param output Buffer to store decoded output
 * @param max_len Maximum length of output buffer
 * @return Number of bytes decoded, or <=0 on error
 * 
 * Reference: ADR-016 HTTP Basic Authentication
 */
int http_base64_decode(const char* input, char* output, size_t max_len) {
    if (!input || !output || max_len == 0) {
        return -1;
    }
    
    // Base64 decode table
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t input_len = strlen(input);
    if (input_len == 0) {
        output[0] = '\0';
        return 0;
    }
    
    size_t output_idx = 0;
    uint32_t buf = 0;
    int buf_len = 0;
    
    for (size_t i = 0; i < input_len && output_idx < max_len - 1; i++) {
        char c = input[i];
        
        // Skip padding and whitespace
        if (c == '=' || c == ' ' || c == '\r' || c == '\n') {
            continue;
        }
        
        // Find character in base64 table
        const char* pos = strchr(base64_chars, c);
        if (!pos) {
            // Invalid character
            return -1;
        }
        
        int val = pos - base64_chars;
        buf = (buf << 6) | val;
        buf_len += 6;
        
        if (buf_len >= 8) {
            buf_len -= 8;
            output[output_idx++] = (buf >> buf_len) & 0xFF;
        }
    }
    
    output[output_idx] = '\0';
    return output_idx;
}

/**
 * @brief Encode binary data to Base64 string
 * 
 * Encodes binary data to Base64 ASCII string.
 * Currently a placeholder - not yet implemented.
 * 
 * @param input Binary input data
 * @param input_len Length of input data in bytes
 * @param output Buffer to store Base64 encoded output
 * @param max_len Maximum length of output buffer
 * @return Number of characters written, or <=0 on error
 */
int http_base64_encode(const uint8_t* input, size_t input_len, char* output, size_t max_len) {
    // TODO: Implement Base64 encoding if needed in future
    (void)input;
    (void)input_len;
    (void)output;
    (void)max_len;
    return -1;  // Not implemented
}

/**
 * @brief Send HTTP 401 Unauthorized response
 * 
 * Sends HTTP 401 response with WWW-Authenticate header to
 * trigger browser authentication dialog.
 * 
 * @param conn HTTP connection to send response on
 * 
 * Reference: ADR-016 HTTP Basic Authentication
 */
void http_send_auth_required(http_connection_t* conn) {
    static char auth_response[512];
    int len = snprintf(auth_response, sizeof(auth_response),
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic realm=\"UART2ETH Device\"\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>401 Unauthorized</title></head>\n"
        "<body>\n"
        "    <h1>401 Unauthorized</h1>\n"
        "    <p>Access denied. Please provide valid credentials.</p>\n"
        "    <p>Username: <strong>admin</strong></p>\n"
        "</body>\n"
        "</html>\r\n");
    
    // Calculate content length (HTML body only)
    const char* content_start = strstr(auth_response, "\r\n\r\n");
    if (content_start) {
        content_start += 4; // Skip the \r\n\r\n
        int content_len = strlen(content_start);
        
        // Rebuild with correct Content-Length
        len = snprintf(auth_response, sizeof(auth_response),
            "HTTP/1.1 401 Unauthorized\r\n"
            "WWW-Authenticate: Basic realm=\"UART2ETH Device\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head><title>401 Unauthorized</title></head>\n"
            "<body>\n"
            "    <h1>401 Unauthorized</h1>\n"
            "    <p>Access denied. Please provide valid credentials.</p>\n"
            "    <p>Username: <strong>admin</strong></p>\n"
            "</body>\n"
            "</html>\r\n", content_len);
    }

    http_send_response(conn, auth_response, len);
    // Don't close connection - let browser close it after receiving 401
    // or reuse it for authenticated request
}

/**
 * @brief Check HTTP Basic Authentication
 * 
 * Validates Authorization header against expected credentials.
 * Username must be "admin" and password must match expected_password.
 * 
 * @param request Full HTTP request string
 * @param expected_password Password to validate against
 * @return true if authenticated, false otherwise
 * 
 * Reference: ADR-016 HTTP Basic Authentication
 */
bool http_check_authentication(const char* request, const char* expected_password) {
    if (!request || !expected_password) {
        return false;
    }
    
    // Find Authorization header
    const char* auth_header = strstr(request, "Authorization: Basic ");
    if (!auth_header) {
        /* printf("HTTP Auth: No Authorization header found\n"); */
        return false;
    }
    
    // Extract base64 credentials (skip "Authorization: Basic ")
    auth_header += 21;  // strlen("Authorization: Basic ")
    
    // Find end of base64 string (CR or LF)
    char base64_creds[128] = {0};
    const char* end = auth_header;
    while (*end && *end != '\r' && *end != '\n' && (end - auth_header) < 127) {
        end++;
    }
    size_t cred_len = end - auth_header;
    strncpy(base64_creds, auth_header, cred_len);
    base64_creds[cred_len] = '\0';
    
    // Decode base64 to "username:password"
    char decoded[128] = {0};
    int decoded_len = http_base64_decode(base64_creds, decoded, sizeof(decoded));
    if (decoded_len <= 0) {
        /* printf("HTTP Auth: Base64 decode failed\n"); */
        return false;
    }
    
    // Split on ':' to extract username and password
    char* colon = strchr(decoded, ':');
    if (!colon) {
        /* printf("HTTP Auth: No colon separator in credentials\n"); */
        return false;
    }
    
    *colon = '\0';  // Split string
    char* username = decoded;
    char* password = colon + 1;
    
    // Validate username (must be "admin")
    if (strcmp(username, "admin") != 0) {
        /* printf("HTTP Auth: Invalid username '%s'\n", username); */
        return false;
    }
    
    // Validate password
    if (strcmp(password, expected_password) != 0) {
        /* printf("HTTP Auth: Invalid password\n"); */
        return false;
    }
    
    /* printf("HTTP Auth: Authentication successful for user 'admin'\n"); */
    return true;
}
