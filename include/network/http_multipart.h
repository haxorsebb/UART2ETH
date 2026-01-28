/**
 * @file http_multipart.h
 * @brief Generic HTTP multipart/form-data handler
 * 
 * Provides utilities for parsing and handling multipart/form-data uploads,
 * including streaming large file uploads across multiple TCP callbacks.
 * 
 * This module is designed to be generic and reusable for any type of file upload,
 * not just firmware updates.
 * 
 * Documentation Reference:
 * - ADR-016: Firmware Update Web Interface
 * - arc42 Chapter 5: HTTP Server Module
 */

#ifndef HTTP_MULTIPART_H
#define HTTP_MULTIPART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum boundary string length (RFC 2046)
#define MULTIPART_BOUNDARY_MAX_LENGTH 70

/**
 * @brief Multipart upload context
 * 
 * Tracks state for a multipart upload across multiple callbacks
 */
typedef struct {
    bool active;                        ///< True if upload is in progress
    bool headers_parsed;                ///< True if HTTP headers have been parsed
    char boundary[MULTIPART_BOUNDARY_MAX_LENGTH]; ///< Multipart boundary string
    uint32_t content_length;            ///< Total Content-Length from HTTP header
    uint32_t file_size;                 ///< Actual file size (content_length - multipart overhead)
    uint32_t bytes_received;            ///< File bytes received so far
    uint32_t http_headers_length;       ///< Length of HTTP headers to skip
    uint32_t multipart_overhead;        ///< Total multipart overhead bytes
} multipart_context_t;

/**
 * @brief Callback function for processing file data chunks
 * 
 * Called for each chunk of actual file data (excluding multipart overhead).
 * 
 * @param data Pointer to file data
 * @param size Number of bytes in this chunk
 * @param finished True if this is the final chunk
 * @param user_data User-provided context pointer
 * @return true if chunk was processed successfully, false on error
 */
typedef bool (*multipart_chunk_callback_t)(const uint8_t* data, uint32_t size, bool finished, void* user_data);

/**
 * @brief Parse multipart boundary from Content-Type header
 * 
 * Extracts the boundary string from a Content-Type header like:
 * "Content-Type: multipart/form-data; boundary=----WebKitFormBoundary..."
 * 
 * @param request_buffer Full HTTP request buffer
 * @param boundary Output buffer for boundary string (must be at least MULTIPART_BOUNDARY_MAX_LENGTH)
 * @return true if boundary was found and extracted, false otherwise
 */
bool multipart_parse_boundary(const char* request_buffer, char* boundary);

/**
 * @brief Initialize multipart upload context from HTTP request
 * 
 * Parses HTTP headers, extracts boundary, calculates file size, and prepares
 * the context for streaming upload.
 * 
 * @param ctx Multipart context to initialize
 * @param request_buffer Full HTTP request buffer
 * @param request_length Length of request buffer
 * @return true if initialization successful, false on error
 */
bool multipart_init_context(multipart_context_t* ctx, const char* request_buffer, size_t request_length);

/**
 * @brief Process a chunk of data from TCP callback
 * 
 * Handles stripping multipart overhead and feeding actual file data to the callback.
 * Manages state across multiple TCP receive callbacks for large uploads.
 * 
 * @param ctx Multipart context
 * @param data Raw data from TCP callback
 * @param length Length of raw data
 * @param chunk_callback Callback to receive actual file data
 * @param user_data User context to pass to callback
 * @return true if chunk processed successfully, false on error
 */
bool multipart_process_chunk(multipart_context_t* ctx, const uint8_t* data, size_t length,
                             multipart_chunk_callback_t chunk_callback, void* user_data);

/**
 * @brief Check if upload is complete
 * 
 * @param ctx Multipart context
 * @return true if all expected file bytes have been received
 */
bool multipart_is_complete(const multipart_context_t* ctx);

/**
 * @brief Reset multipart context
 * 
 * Clears context to idle state. Call after upload completes or on error.
 * 
 * @param ctx Multipart context to reset
 */
void multipart_reset_context(multipart_context_t* ctx);

/**
 * @brief Get upload progress
 * 
 * @param ctx Multipart context
 * @param bytes_received Output: bytes received so far
 * @param total_bytes Output: total expected bytes
 */
void multipart_get_progress(const multipart_context_t* ctx, uint32_t* bytes_received, uint32_t* total_bytes);

#ifdef __cplusplus
}
#endif

#endif // HTTP_MULTIPART_H
