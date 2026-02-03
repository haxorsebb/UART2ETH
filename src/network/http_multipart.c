/**
 * @file http_multipart.c
 * @brief Generic HTTP multipart/form-data handler implementation
 * 
 * Documentation Reference:
 * - ADR-016: Firmware Update Web Interface
 * - arc42 Chapter 5: HTTP Server Module
 */

#include "network/http_multipart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Parse multipart boundary from Content-Type header
 */
bool multipart_parse_boundary(const char* request_buffer, char* boundary) {
    if (!request_buffer || !boundary) {
        return false;
    }
    
    // Find Content-Type header
    const char* content_type = strstr(request_buffer, "Content-Type:");
    if (!content_type) {
        printf("MULTIPART: No Content-Type header found\n");
        return false;
    }
    
    // Find boundary parameter
    const char* boundary_param = strstr(content_type, "boundary=");
    if (!boundary_param) {
        printf("MULTIPART: No boundary parameter in Content-Type\n");
        return false;
    }
    
    // Extract boundary value (until CR/LF)
    boundary_param += 9; // Skip "boundary="
    int i = 0;
    while (i < MULTIPART_BOUNDARY_MAX_LENGTH - 1 && 
           boundary_param[i] != '\r' && boundary_param[i] != '\n' && boundary_param[i] != '\0') {
        boundary[i] = boundary_param[i];
        i++;
    }
    boundary[i] = '\0';
    
    if (i == 0) {
        printf("MULTIPART: Empty boundary\n");
        return false;
    }
    
    printf("MULTIPART: Boundary extracted: '%s'\n", boundary);
    return true;
}

/**
 * @brief Initialize multipart upload context from HTTP request
 */
bool multipart_init_context(multipart_context_t* ctx, const char* request_buffer, size_t request_length) {
    if (!ctx || !request_buffer || request_length == 0) {
        return false;
    }
    
    // Reset context
    memset(ctx, 0, sizeof(multipart_context_t));
    
    // Parse Content-Length
    const char* content_length_str = strstr(request_buffer, "Content-Length:");
    if (!content_length_str) {
        printf("MULTIPART: No Content-Length header\n");
        return false;
    }
    
    if (sscanf(content_length_str + 15, "%u", &ctx->content_length) != 1) {
        printf("MULTIPART: Invalid Content-Length\n");
        return false;
    }
    
    printf("MULTIPART: Content-Length=%u (multipart envelope)\n", ctx->content_length);
    
    // Parse boundary
    if (!multipart_parse_boundary(request_buffer, ctx->boundary)) {
        return false;
    }
    
    // Find start of HTTP body (after double CRLF)
    const char* body_start = strstr(request_buffer, "\r\n\r\n");
    if (!body_start) {
        printf("MULTIPART: Malformed HTTP request (no header/body separator)\n");
        return false;
    }
    body_start += 4;
    ctx->http_headers_length = body_start - request_buffer;
    
    // Calculate multipart overhead
    // Opening boundary: --boundary\r\n
    // Field headers: Content-Disposition: form-data; name="..."; filename="..."\r\nContent-Type: ...\r\n\r\n
    // Closing boundary: \r\n--boundary--\r\n
    
    // Try to find the start of actual file data
    char opening_boundary[MULTIPART_BOUNDARY_MAX_LENGTH + 10];
    snprintf(opening_boundary, sizeof(opening_boundary), "--%s", ctx->boundary);
    
    const char* field_start = strstr(body_start, opening_boundary);
    if (field_start) {
        // Skip to end of boundary line
        const char* field_headers = strstr(field_start, "\r\n");
        if (field_headers) {
            field_headers += 2;
            // Find end of field headers (blank line)
            const char* file_data_start = strstr(field_headers, "\r\n\r\n");
            if (file_data_start) {
                file_data_start += 4;
                uint32_t multipart_header_overhead = file_data_start - body_start;
                
                // Closing boundary: \r\n--boundary--\r\n (length = 2 + 2 + boundary_len + 2 + 2)
                uint32_t closing_boundary_len = 2 + 2 + strlen(ctx->boundary) + 2 + 2;
                
                ctx->multipart_overhead = multipart_header_overhead + closing_boundary_len;
                ctx->file_size = ctx->content_length - ctx->multipart_overhead;
                
                printf("MULTIPART: Overhead=%u bytes (header=%u, closing=%u)\n",
                       ctx->multipart_overhead, multipart_header_overhead, closing_boundary_len);
                printf("MULTIPART: Expected file size=%u bytes\n", ctx->file_size);
            }
        }
    }
    
    if (ctx->file_size == 0) {
        printf("MULTIPART: Failed to calculate file size\n");
        return false;
    }
    
    ctx->active = true;
    ctx->headers_parsed = false;
    ctx->bytes_received = 0;
    
    return true;
}

/**
 * @brief Process a chunk of data from TCP callback
 */
bool multipart_process_chunk(multipart_context_t* ctx, const uint8_t* data, size_t length,
                             multipart_chunk_callback_t chunk_callback, void* user_data) {
    if (!ctx || !ctx->active || !data || length == 0 || !chunk_callback) {
        return false;
    }
    
    uint32_t data_offset = 0;
    uint32_t data_remaining = length;
    
    // First chunk: skip HTTP and multipart headers
    if (!ctx->headers_parsed) {
        // This chunk contains the HTTP headers and possibly multipart headers
        // We need to skip to the actual file data
        
        // Calculate how much of this chunk is headers
        uint32_t total_header_size = ctx->http_headers_length + 
                                     (ctx->multipart_overhead - (ctx->multipart_overhead - ctx->http_headers_length));
        
        // For simplicity, skip the calculated multipart overhead from the body
        // The http_headers_length already accounts for HTTP headers, but the first chunk
        // after HTTP headers still contains the multipart field headers
        
        // Find actual file data start in the body portion
        // We already know multipart header overhead from init
        uint32_t skip_bytes = ctx->http_headers_length + 
                             (ctx->multipart_overhead - (2 + 2 + strlen(ctx->boundary) + 2 + 2));
        
                                
        if (length <= skip_bytes) {
            // This entire chunk is headers, skip it
            ctx->headers_parsed = true;
            return true;
        }
        
        // Part of this chunk is headers, rest is file data
        data_offset = skip_bytes;
        data_remaining = length - skip_bytes;
        ctx->headers_parsed = true;
    }
    
    // Don't exceed expected file size (stop before closing boundary)
    if (ctx->bytes_received + data_remaining > ctx->file_size) {
        data_remaining = ctx->file_size - ctx->bytes_received;
    }
    
    if (data_remaining > 0) {
        bool is_last_chunk = (ctx->bytes_received + data_remaining >= ctx->file_size);
        
        //printf("MULTIPART: DATA [%02X] [%02X] [%02X] [%02X] \n", *(data + data_offset), *(data + data_offset+1), *(data + data_offset+2), *(data + data_offset+3));
            
        // Call the callback with actual file data
        if (!chunk_callback(data + data_offset, data_remaining, is_last_chunk, user_data)) {
            printf("MULTIPART: Chunk callback failed\n");
            return false;
        }
        
        ctx->bytes_received += data_remaining;
        /*
        // Progress reporting (every 64KB or on completion)
        if (is_last_chunk || (ctx->bytes_received % (64 * 1024)) < data_remaining) {

            printf("MULTIPART: Progress %u / %u bytes (%u%%)\n",
                   ctx->bytes_received, ctx->file_size,
                   (ctx->bytes_received * 100) / ctx->file_size);
        }
        */
    }
    
    return true;
}

/**
 * @brief Check if upload is complete
 */
bool multipart_is_complete(const multipart_context_t* ctx) {
    if (!ctx || !ctx->active) {
        return false;
    }
    return (ctx->bytes_received >= ctx->file_size);
}

/**
 * @brief Reset multipart context
 */
void multipart_reset_context(multipart_context_t* ctx) {
    if (ctx) {
        memset(ctx, 0, sizeof(multipart_context_t));
    }
}

/**
 * @brief Get upload progress
 */
void multipart_get_progress(const multipart_context_t* ctx, uint32_t* bytes_received, uint32_t* total_bytes) {
    if (ctx) {
        if (bytes_received) *bytes_received = ctx->bytes_received;
        if (total_bytes) *total_bytes = ctx->file_size;
    }
}
