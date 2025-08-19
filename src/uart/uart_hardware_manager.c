/**
 * @file uart_hardware_manager.c
 * @brief UART Hardware Manager implementation for UART2ETH Core0 UART Subsystem
 * 
 * Provides high-level UART management functions that coordinate
 * between UART1 driver and ring buffer system.
 * Handles initialization, message processing, and Core0 integration.
 * 
 * This implements the interface for Issue #76 following the same
 * pattern as network_manager.c for consistency.
 * 
 * Documentation Reference:  
 * - Issue #76: Add UART Hardware Manager implementation
 * - arc42 Chapter 5 - UART Hardware Manager Implementation
 * - ADR-007: Event-Driven State Machine Architecture
 */

#include "uart/uart_hardware_manager.h"
#include "uart/uart1_driver.h"
#include "ringbuffer.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// Manager state
static bool g_manager_initialized = false;
static uart_manager_status_t g_manager_status = UART_MANAGER_STATUS_UNINITIALIZED;
static uart_manager_stats_t g_manager_stats = {0};
static uint32_t g_manager_start_time = 0;

// Line assembly buffer for incoming data
#define LINE_BUFFER_SIZE 256
static char g_line_buffer[LINE_BUFFER_SIZE];
static size_t g_line_buffer_pos = 0;

// Forward declarations
static bool process_incoming_characters(void);
static bool process_complete_line(const char* line, size_t length);
static bool send_outgoing_message(const char* data, size_t length);
static void update_manager_stats(void);
static void reset_line_buffer(void);

/**
 * @brief Initialize UART Hardware Manager
 */
bool uart_hardware_manager_init(void) {
    if (g_manager_initialized) {
        log_event(EVENT_SOURCE_UART1, LOG_LEVEL_DEBUG, LOG_EVENT_UART_INIT, 1);
        return true;  // Already initialized
    }
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 0);
    
    // Set initial status
    g_manager_status = UART_MANAGER_STATUS_INITIALIZING;
    
    // Initialize UART1 driver with default configuration
    uart1_config_t config;
    uart1_driver_get_default_config(&config);
    
    // Override with Issue #76 specific settings
    config.baud_rate = 230400;
    config.data_bits = 8;
    config.stop_bits = 1;
    config.parity = UART_PARITY_NONE;
    config.rx_gpio = 9;
    config.tx_gpio = 10;
    config.enable_loopback = true;  // Enable for testing
    
    if (!uart1_driver_init(&config)) {
        log_event(EVENT_SOURCE_UART1, LOG_LEVEL_ERROR, LOG_EVENT_UART1_ERROR, 1);
        g_manager_status = UART_MANAGER_STATUS_ERROR;
        return false;
    }
    
    // Initialize line buffer
    reset_line_buffer();
    
    // Initialize statistics
    memset(&g_manager_stats, 0, sizeof(uart_manager_stats_t));
    g_manager_stats.status = UART_MANAGER_STATUS_READY;
    g_manager_start_time = to_ms_since_boot(get_absolute_time());
    
    // Mark as initialized
    g_manager_initialized = true;
    g_manager_status = UART_MANAGER_STATUS_READY;
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 1);
    return true;
}

/**
 * @brief Deinitialize UART Hardware Manager
 */
void uart_hardware_manager_deinit(void) {
    if (!g_manager_initialized) {
        return;
    }
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 0);
    
    // Deinitialize UART1 driver
    uart1_driver_deinit();
    
    // Reset state
    g_manager_initialized = false;
    g_manager_status = UART_MANAGER_STATUS_UNINITIALIZED;
    memset(&g_manager_stats, 0, sizeof(uart_manager_stats_t));
    reset_line_buffer();
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 1);
}

/**
 * @brief Check if UART Hardware Manager is ready for operation
 */
bool uart_hardware_manager_is_ready(void) {
    return g_manager_initialized && 
           (g_manager_status == UART_MANAGER_STATUS_READY || 
            g_manager_status == UART_MANAGER_STATUS_ACTIVE);
}

/**
 * @brief Get current UART Hardware Manager status
 */
uart_manager_status_t uart_hardware_manager_get_status(void) {
    return g_manager_status;
}

/**
 * @brief Check if UART Hardware Manager has pending work
 */
bool uart_hardware_manager_has_pending_work(void) {
    if (!uart_hardware_manager_is_ready()) {
        return false;
    }
    
    // Check for incoming UART data
    if (uart1_driver_has_rx_data()) {
        return true;
    }
    
    // Check for outgoing ring buffer messages
    if (ringbuffer_get_count(RX_TCP_TO_UART) > 0) {
        return true;
    }
    
    return false;
}

/**
 * @brief Process incoming UART data (UART→TCP direction)
 */
bool uart_hardware_manager_process_incoming_data(void) {
    if (!uart_hardware_manager_is_ready()) {
        return false;
    }
    
    if (!uart1_driver_has_rx_data()) {
        return false;  // No data to process
    }
    
    g_manager_status = UART_MANAGER_STATUS_ACTIVE;
    update_manager_stats();
    
    bool result = process_incoming_characters();
    
    g_manager_status = UART_MANAGER_STATUS_READY;
    return result;
}

/**
 * @brief Process outgoing UART data (TCP→UART direction)
 */
bool uart_hardware_manager_process_outgoing_data(void) {
    if (!uart_hardware_manager_is_ready()) {
        return false;
    }
    
    // Get message from ring buffer
    ring_entry_t* entry = ringbuffer_dequeue_entry(RX_TCP_TO_UART);
    if (!entry) {
        return false;  // No data to process
    }
    
    g_manager_status = UART_MANAGER_STATUS_ACTIVE;
    update_manager_stats();
    
    // Send message via UART
    bool result = send_outgoing_message((char*)entry->payload, entry->payload_length);
    
    if (result) {
        g_manager_stats.messages_tcp_to_uart++;
        g_manager_stats.bytes_transmitted += entry->payload_length;
        // Loopback echo is now handled at the UART1 driver level when loopback is enabled
        // The echoed data will be available via process_incoming_data()
    } else {
        g_manager_stats.transmission_errors++;
    }
    
    // Mark ring buffer entry as consumed
    ringbuffer_mark_consumed(entry);
    
    g_manager_status = UART_MANAGER_STATUS_READY;
    return result;
}

/**
 * @brief Get current UART Hardware Manager statistics
 */
void uart_hardware_manager_get_stats(uart_manager_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    update_manager_stats();
    memcpy(stats, &g_manager_stats, sizeof(uart_manager_stats_t));
}

/**
 * @brief Reset UART Hardware Manager statistics counters
 */
void uart_hardware_manager_reset_stats(void) {
    memset(&g_manager_stats, 0, sizeof(uart_manager_stats_t));
    g_manager_stats.status = g_manager_status;
    g_manager_start_time = to_ms_since_boot(get_absolute_time());
    
    // Reset driver statistics too
    uart1_driver_reset_stats();
}

/**
 * @brief Test UART Hardware Manager connectivity (loopback test)
 */
bool uart_hardware_manager_test_loopback(void) {
    if (!uart_hardware_manager_is_ready()) {
        return false;
    }
    
    // Use UART1 driver loopback test
    return uart1_driver_test_loopback('T');
}

/**
 * @brief Get detailed diagnostic information for testing
 */
int uart_hardware_manager_get_diagnostic_info(char* info_buffer, size_t buffer_size) {
    if (!info_buffer || buffer_size == 0) {
        return 0;
    }
    
    update_manager_stats();
    const uart1_state_t* driver_state = uart1_driver_get_state();
    
    return snprintf(info_buffer, buffer_size,
        "UART Hardware Manager Diagnostics:\n"
        "  Status: %s\n"
        "  Uptime: %lu seconds\n"
        "  Messages processed: %lu\n"
        "  TCP->UART: %lu, UART->TCP: %lu\n"
        "  Bytes TX: %lu, RX: %lu\n"
        "  Errors - TX: %lu, RX: %lu, Overflows: %lu\n"
        "  Driver State:\n"
        "    Ready: %s, Chars sent: %lu, received: %lu\n"
        "    Driver errors - TX: %lu, RX: %lu, Overrun: %lu\n",
        uart_hardware_manager_status_to_string(g_manager_stats.status),
        g_manager_stats.uptime_seconds,
        g_manager_stats.messages_processed,
        g_manager_stats.messages_tcp_to_uart,
        g_manager_stats.messages_uart_to_tcp,
        g_manager_stats.bytes_transmitted,
        g_manager_stats.bytes_received,
        g_manager_stats.transmission_errors,
        g_manager_stats.reception_errors,
        g_manager_stats.ring_buffer_overflows,
        driver_state ? (driver_state->ready ? "Yes" : "No") : "Unknown",
        driver_state ? driver_state->chars_sent : 0,
        driver_state ? driver_state->chars_received : 0,
        driver_state ? driver_state->tx_errors : 0,
        driver_state ? driver_state->rx_errors : 0,
        driver_state ? driver_state->overrun_errors : 0
    );
}

/**
 * @brief Convert UART manager status to human-readable string
 */
const char* uart_hardware_manager_status_to_string(uart_manager_status_t status) {
    switch (status) {
        case UART_MANAGER_STATUS_UNINITIALIZED: return "Uninitialized";
        case UART_MANAGER_STATUS_INITIALIZING:  return "Initializing";
        case UART_MANAGER_STATUS_READY:         return "Ready";
        case UART_MANAGER_STATUS_ACTIVE:        return "Active";
        case UART_MANAGER_STATUS_ERROR:         return "Error";
        default:                                return "Unknown";
    }
}

// Private function implementations

/**
 * @brief Process incoming characters from UART
 */
static bool process_incoming_characters(void) {
    bool line_processed = false;
    
    while (uart1_driver_has_rx_data()) {
        char c = uart1_driver_read_char();
        g_manager_stats.bytes_received++;
        
        // Skip leading null bytes at start of new line
        if (g_line_buffer_pos == 0 && c == '\0') {
            printf("DEBUG: Skipping leading null byte\n");
            continue;  // Skip this character
        }
        
        // Add character to line buffer
        if (g_line_buffer_pos < (LINE_BUFFER_SIZE - 1)) {
            g_line_buffer[g_line_buffer_pos++] = c;
            printf("DEBUG: Added char 0x%02X ('%c') at pos %zu, new pos = %zu\n", 
                   (unsigned char)c, (c >= 32 && c < 127) ? c : '.', g_line_buffer_pos-1, g_line_buffer_pos);
            
            // Check for newline (end of message)
            if (c == '\n') {
                printf("DEBUG: Newline detected, calling process_complete_line with length %zu\n", g_line_buffer_pos);
                printf("DEBUG: Line content: '");
                for (size_t i = 0; i < g_line_buffer_pos; i++) {
                    printf("%c", g_line_buffer[i]);
                }
                printf("'\n");
                
                // Process complete line (include newline in payload)
                // Note: g_line_buffer_pos already includes the newline position
                if (process_complete_line(g_line_buffer, g_line_buffer_pos)) {
                    line_processed = true;
                    g_manager_stats.messages_uart_to_tcp++;
                }
                
                // Reset buffer for next line
                reset_line_buffer();
            }
        } else {
            // Line buffer overflow - reset and log error
            log_event(EVENT_SOURCE_UART1, LOG_LEVEL_WARN, LOG_EVENT_UART1_ERROR, 3);
            g_manager_stats.reception_errors++;
            reset_line_buffer();
        }
    }
    
    return line_processed;
}

/**
 * @brief Process complete line and send to ring buffer
 */
static bool process_complete_line(const char* line, size_t length) {
    if (!line || length == 0) {
        return false;
    }
    
    // Get free ring buffer entry
    ring_entry_t* entry = ringbuffer_get_free_entry();
    if (!entry) {
        log_event(EVENT_SOURCE_UART1, LOG_LEVEL_WARN, LOG_EVENT_UART1_OVERFLOW, 0);
        g_manager_stats.ring_buffer_overflows++;
        return false;
    }
    
    // Fill entry
    entry->uart_channel = 1;  // UART1
    entry->direction = RX_UART_TO_TCP;
    entry->payload_length = (length > RINGBUFFER_PAYLOAD_MAX_SIZE) ? RINGBUFFER_PAYLOAD_MAX_SIZE : length;
    entry->timestamp = to_ms_since_boot(get_absolute_time());
    
    printf("DEBUG: process_complete_line - input length=%zu, payload_length=%u\n", length, entry->payload_length);
    printf("DEBUG: Input line bytes: ");
    for (size_t i = 0; i < length; i++) {
        printf("%02X ", (unsigned char)line[i]);
    }
    printf("\n");
    
    memcpy(entry->payload, line, entry->payload_length);
    
    printf("DEBUG: Stored payload bytes: ");
    for (uint32_t i = 0; i < entry->payload_length; i++) {
        printf("%02X ", (unsigned char)entry->payload[i]);
    }
    printf("\n");
    
    // Enqueue entry
    if (!ringbuffer_enqueue_entry(entry)) {
        log_event(EVENT_SOURCE_UART1, LOG_LEVEL_ERROR, LOG_EVENT_UART1_ERROR, 0);
        return false;
    }
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_DEBUG, LOG_EVENT_UART_COMPLETE, 1);
    return true;
}

/**
 * @brief Send outgoing message via UART
 */
static bool send_outgoing_message(const char* data, size_t length) {
    if (!data || length == 0) {
        return false;
    }
    
    // Send data via UART1 driver
    return uart1_driver_send_data((const uint8_t*)data, length);
}

/**
 * @brief Update manager statistics
 */
static void update_manager_stats(void) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    g_manager_stats.uptime_seconds = (current_time - g_manager_start_time) / 1000;
    g_manager_stats.status = g_manager_status;
    g_manager_stats.messages_processed = g_manager_stats.messages_tcp_to_uart + 
                                        g_manager_stats.messages_uart_to_tcp;
}

/**
 * @brief Reset line buffer
 */
static void reset_line_buffer(void) {
    memset(g_line_buffer, 0, sizeof(g_line_buffer));
    g_line_buffer_pos = 0;
}
