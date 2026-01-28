/**
 * @file http_multipart.c
 * @brief Generic HTTP multipart/form-data parser implementation
 * 
 * Implements streaming multipart parser for large file uploads.
 * 
 * Documentation Reference:
 * - ADR-016: Firmware Update Web Interface
 * - ADR-017: Update Module
 */

#include "network/http_multipart.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Initialize multipart session from HTTP request
 */
bool multipart_session_init(multipart_session_t* session,
                            const char* request_buffer,
                            size_t request_len,
                            multipart_data_callback_t data_callback,
                            void* user_data) {
    if (!session || !request_buffer || request_len == 0) {
        return false;
    }
    
    // Reset session state
    memset(session, 0, sizeof(multipart_session_t));
    session->state = MULTIPART_STATE_HEADERS;
    session->user_data = user_data;
    
    // Parse Content-Length header
    const char* content_length_str = strstr(request_buffer, "Content-Length: ");
    if (!content_length_str) {
        printf("MULTIPART: Missing Content-Length header\n");
        session->state = MULTIPART_STATE_ERROR;
        return false;
    }
    
    uint32_t content_length = 0;
    if (sscanf(content_length_str + 16, "%u", &content_length) != 1) {
        printf("MULTIPART: Invalid Content-Length header\n");
        session->state = MULTIPART_STATE_ERROR;
        return false;
    }
    
    printf("MULTIPART: Content-Length = %u bytes (multipart body)\n", content_length);
    
    // Parse multipart boundary from Content-Type header
    // Format: "Content-Type: multipart/form-data; boundary=----WebKitFormBoundary..."
    const char* content_type_str = strstr(request_buffer, "Content-Type:");
    const char* boundary_str = NULL;
    
    if (content_type_str) {
        boundary_str = strstr(content_type_str, "boundary=");
        if (boundary_str) {
            boundary_str += 9; // Skip "boundary="
            // Copy boundary until \r or end of buffer
            int i = 0;
            while (i < HTTP_MULTIPART_BOUNDARY_MAX - 3 && 
                   boundary_str[i] != '\r' && boundary_str[i] != '\n') {
                session->boundary[i] = boundary_str[i];
                i++;
            }
            session->boundary[i] = '\0';
            printf("MULTIPART: Boundary: '%s'\n", session->boundary);
        }
    }
    
    if (strlen(session->boundary) == 0) {
        printf("MULTIPART: Missing multipart boundary in Content-Type\n");
        session->state = MULTIPART_STATE_ERROR;
        return false;
    }
    
    // Find end of HTTP headers (double CRLF)
    const char* headers_end = strstr(request_buffer, "\r\n\r\n");
    if (!headers_end) {
        printf("MULTIPART: Malformed HTTP request\n");
        session->state = MULTIPART_STATE_ERROR;
        return false;
    }
    
    const char* body_start = headers_end + 4;
    
    // Find where actual file data starts (after opening boundary and field headers)
    // Pattern: --boundary\r\nContent-Disposition...\r\nContent-Type...\r\n\r\n[FILE DATA]
    char opening_boundary[HTTP_MULTIPART_BOUNDARY_MAX + 10];
    snprintf(opening_boundary, sizeof(opening_boundary), "--%s", session->boundary);
    
    const char* file_data_start = strstr(body_start, opening_boundary);
    if (file_data_start) {
        // Skip past boundary line
        file_data_start = strstr(file_data_start, "\r\n");
        if (file_data_start) {
            file_data_start += 2; // Skip \r\n
            // Find end of multipart field headers (blank line)
            const char* field_headers_end = strstr(file_data_start, "\r\n\r\n");
            if (field_headers_end) {
                file_data_start = field_headers_end + 4; // Start of actual file
                session->headers_length = file_data_start - request_buffer;
                printf("MULTIPART: File data starts at offset %u\n", session->headers_length);
            }
        }
    }
    
    // Calculate expected file size (Content-Length - multipart overhead)
    // Overhead = opening boundary + field headers + closing boundary
    // Closing boundary format: \r\n--boundary--\r\n
    char closing_boundary[HTTP_MULTIPART_BOUNDARY_MAX + 10];
    snprintf(closing_boundary, sizeof(closing_boundary), "\r\n--%s--", session->boundary);
    uint32_t closing_boundary_size = strlen(closing_boundary) + 2; // +2 for final \r\n
    
    uint32_t multipart_header_overhead = session->headers_length - (headers_end + 4 - request_buffer);
    uint32_t total_overhead = multipart_header_overhead + closing_boundary_size;
    uint32_t expected_file_size = content_length - total_overhead;
    
    printf("MULTIPART: Multipart overhead: %u bytes (header: %u, closing: %u)\n",
           total_overhead, multipart_header_overhead, closing_boundary_size);
    printf("MULTIPART: Expected file size: %u bytes\n", expected_file_size);
    
    session->total_size = expected_file_size;
    session->bytes_received = 0;
    
    return true;
}

/**
 * @brief Process a chunk of incoming data
 */
bool multipart_process_chunk(multipart_session_t* session,
                             const uint8_t* data,
                             uint32_t size,
                             multipart_data_callback_t callback) {
    if (!session || !data || size == 0 || !callback) {
        return false;
    }
    
    if (session->state != MULTIPART_STATE_HEADERS && 
        session->state != MULTIPART_STATE_DATA) {
        printf("MULTIPART: Invalid state for processing chunk\n");
        return false;
    }
    
    // If still in headers state, we're processing first chunk
    if (session->state == MULTIPART_STATE_HEADERS) {
        // Skip headers in first chunk only
        if (!session->headers_parsed) {
            if (size <= session->headers_length) {
                // This chunk is all headers, skip it
                session->headers_parsed = true;
                return true;
            }
            // Part of this chunk is headers, part is data
            uint32_t header_bytes = session->headers_length;
            uint32_t data_bytes = size - header_bytes;
            
            // Limit to expected file size
            if (session->bytes_received + data_bytes > session->total_size) {
                data_bytes = session->total_size - session->bytes_received;
            }
            
            if (data_bytes > 0) {
                bool finished = (session->bytes_received + data_bytes >= session->total_size);
                if (!callback(data + header_bytes, data_bytes, finished, session->user_data)) {
                    session->state = MULTIPART_STATE_ERROR;
                    return false;
                }
                session->bytes_received += data_bytes;
            }
            
            session->headers_parsed = true;
            session->state = MULTIPART_STATE_DATA;
            return true;
        }
    }
    
    // Process data chunk
    uint32_t bytes_to_process = size;
    
    // Limit to not exceed expected file size (stop before multipart closing boundary)
    if (session->bytes_received + bytes_to_process > session->total_size) {
        bytes_to_process = session->total_size - session->bytes_received;
    }
    
    if (bytes_to_process > 0) {
        bool finished = (session->bytes_received + bytes_to_process >= session->total_size);
        if (!callback(data, bytes_to_process, finished, session->user_data)) {
            session->state = MULTIPART_STATE_ERROR;
            return false;
        }
        session->bytes_received += bytes_to_process;
        
        if (finished) {
            session->state = MULTIPART_STATE_COMPLETE;
        }
    }
    
    return true;
}

/**
 * @brief Check if upload is complete
 */
bool multipart_is_complete(const multipart_session_t* session) {
    if (!session) {
        return false;
    }
    return (session->state == MULTIPART_STATE_COMPLETE);
}

/**
 * @brief Reset/abort multipart session
 */
void multipart_session_reset(multipart_session_t* session) {
    if (session) {
        memset(session, 0, sizeof(multipart_session_t));
        session->state = MULTIPART_STATE_IDLE;
    }
}

/**
 * @brief Get upload progress
 */
void multipart_get_progress(const multipart_session_t* session,
                            uint32_t* bytes_received,
                            uint32_t* total_bytes) {
    if (session) {
        if (bytes_received) {
            *bytes_received = session->bytes_received;
        }
        if (total_bytes) {
            *total_bytes = session->total_size;
        }
    }
}
