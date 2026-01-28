/**
 * @file http_multipart.h
 * @brief Generic HTTP multipart/form-data parser for file uploads
 * 
 * Provides streaming multipart parser that can handle large file uploads
 * across multiple TCP callbacks without buffering entire file in RAM.
 * 
 * Key features:
 * - Streaming parser (processes data as it arrives)
 * - Minimal memory footprint (no complete file buffering)
 * - Generic callback interface for data handling
 * - Boundary detection and multipart header parsing
 * 
 * Documentation Reference:
 * - ADR-016: Firmware Update Web Interface
 * - ADR-017: Update Module
 */

#ifndef HTTP_MULTIPART_H
#define HTTP_MULTIPART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum multipart boundary length (RFC 2046 specifies max 70 chars)
#define HTTP_MULTIPART_BOUNDARY_MAX 128

/**
 * @brief Multipart parser state
 */
typedef enum {
    MULTIPART_STATE_IDLE,           ///< Not parsing
    MULTIPART_STATE_HEADERS,        ///< Parsing HTTP/multipart headers
    MULTIPART_STATE_DATA,           ///< Processing file data
    MULTIPART_STATE_COMPLETE,       ///< Upload complete
    MULTIPART_STATE_ERROR           ///< Error occurred
} multipart_state_t;

/**
 * @brief Multipart session context
 * 
 * Tracks state for a single multipart upload session.
 * Should be allocated per-connection for concurrent uploads.
 */
typedef struct {
    multipart_state_t state;                    ///< Current parser state
    char boundary[HTTP_MULTIPART_BOUNDARY_MAX]; ///< Multipart boundary string
    uint32_t total_size;                        ///< Expected total file size
    uint32_t bytes_received;                    ///< Actual file bytes received
    uint32_t headers_length;                    ///< Length of HTTP + multipart headers
    bool headers_parsed;                        ///< True if headers have been parsed
    
    // Callback for processing file data
    void* user_data;                            ///< User data passed to callbacks
} multipart_session_t;

/**
 * @brief Callback function for processing file data chunks
 * 
 * Called for each chunk of actual file data (after stripping multipart overhead).
 * 
 * @param data Pointer to file data chunk
 * @param size Size of data chunk in bytes
 * @param finished True if this is the final chunk
 * @param user_data User data from multipart_session_t
 * @return true if chunk processed successfully, false on error
 */
typedef bool (*multipart_data_callback_t)(const uint8_t* data, uint32_t size, 
                                          bool finished, void* user_data);

/**
 * @brief Initialize multipart session from HTTP request
 * 
 * Parses Content-Type and Content-Length headers, extracts boundary,
 * and initializes session state.
 * 
 * @param session Session context to initialize
 * @param request_buffer HTTP request headers
 * @param request_len Length of request buffer
 * @param data_callback Callback for processing file data chunks
 * @param user_data User data to pass to callback
 * @return true if initialization successful, false on error
 */
bool multipart_session_init(multipart_session_t* session,
                            const char* request_buffer,
                            size_t request_len,
                            multipart_data_callback_t data_callback,
                            void* user_data);

/**
 * @brief Process a chunk of incoming data
 * 
 * Handles streaming processing of multipart data. Can be called multiple
 * times with sequential chunks from TCP callbacks.
 * 
 * @param session Session context
 * @param data Pointer to incoming data chunk
 * @param size Size of data chunk
 * @param callback Data callback for processed file data
 * @return true if chunk processed successfully, false on error
 */
bool multipart_process_chunk(multipart_session_t* session,
                             const uint8_t* data,
                             uint32_t size,
                             multipart_data_callback_t callback);

/**
 * @brief Check if upload is complete
 * 
 * Checks if we've received all expected data and closing boundary.
 * 
 * @param session Session context
 * @return true if upload complete, false otherwise
 */
bool multipart_is_complete(const multipart_session_t* session);

/**
 * @brief Reset/abort multipart session
 * 
 * Cleans up session state. Safe to call at any time.
 * 
 * @param session Session context to reset
 */
void multipart_session_reset(multipart_session_t* session);

/**
 * @brief Get upload progress
 * 
 * @param session Session context
 * @param bytes_received Output: bytes received so far
 * @param total_bytes Output: total expected bytes
 */
void multipart_get_progress(const multipart_session_t* session,
                            uint32_t* bytes_received,
                            uint32_t* total_bytes);

#ifdef __cplusplus
}
#endif

#endif // HTTP_MULTIPART_H
