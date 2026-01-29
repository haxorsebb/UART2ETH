/**
 * @file page_styles.h
 * @brief HTTP stylesheet generation for web interface
 * 
 * Provides common CSS styles shared across all HTML pages in the
 * management interface.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 */

#ifndef PAGE_STYLES_H
#define PAGE_STYLES_H

#include <stddef.h>

/**
 * @brief Generate CSS stylesheet response
 * 
 * Creates a complete HTTP response with minified CSS stylesheet
 * containing common styles for all web interface pages.
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * 
 * Documentation Reference: ADR-018
 */
void http_generate_stylesheet(char* buffer, size_t buffer_size);

#endif // PAGE_STYLES_H
