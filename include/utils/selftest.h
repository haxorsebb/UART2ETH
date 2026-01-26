#pragma once

/**
 * @brief Send a string over UART1 for selftest output
 * 
 * Sends each character directly to the UART TX FIFO, waiting for
 * space in the FIFO before each character. Waits for transmission
 * to complete before returning.
 * 
 * @param str Null-terminated string to send
 */
void selftest_puts(const char* str);

/**
 * @brief Run selftest at startup
 */
void selftest(void);
