/**
 * @file uart_manager.c
 * @brief UART Manager implementation - manages 4 UART channels
 */

#include "uart/uart_manager.h"
#include "shared_memory.h"
#include "device_mode.h"
#include "uart/uart_interface.h"
#include "uart_config_protocol.h"
#include "ringbuffer.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// External interfaces
extern const uart_interface_t pl011_uart_interface;
extern void* pl011_create_context(uint8_t hw_uart_num);
extern void pl011_destroy_context(void* context);

// PIO UART interface (Issue #82)
extern const uart_interface_t pio_uart_interface;
extern void* pio_create_context(uint8_t pio_num, uint8_t sm_num);
extern void pio_destroy_context(void* context);

// PIO UART Channel 3 interface (PIO1)
extern const uart_interface_t pio_uart_ch3_interface;
extern void* pio_ch3_create_context(uint8_t pio_num, uint8_t sm_num);
extern void pio_ch3_destroy_context(void* context);

// PIO UART Channel 4 interface (PIO2)
extern const uart_interface_t pio_uart_ch4_interface;
extern void* pio_ch4_create_context(uint8_t pio_num, uint8_t sm_num);
extern void pio_ch4_destroy_context(void* context);

// Line assembly for incoming data
#define MINIMUM_MESSAGE_LENGTH 8  // '#0000!\r\n' minimum

// Manager state
typedef struct {
    bool initialized;
    uart_manager_stats_t stats;
    uint32_t start_time;
    bool debug_enabled;
    
    // UART instances (4 channels)
    uart_instance_t uarts[UART_MANAGER_MAX_CHANNELS];

} uart_manager_t;

static uart_manager_t g_manager = {0};

// Forward declarations
static bool init_channel(channel_id_t channel);
static void deinit_channel(channel_id_t channel);
static bool process_channel_incoming_data(channel_id_t channel);
static bool process_channel_outgoing_data(channel_id_t channel);
static void update_manager_stats(void);
static bool check_message_end(const uint8_t* buffer, size_t length);

bool uart_manager_init(void) {
    if (g_manager.initialized) {
        return true;
    }
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 0);
    
    memset(&g_manager, 0, sizeof(uart_manager_t));
    g_manager.stats.status = UART_MANAGER_STATUS_INITIALIZING;
    g_manager.start_time = to_ms_since_boot(get_absolute_time());
    
    // Initialize UART configuration protocol
    uart_config_protocol_init();
    
    // Initialize UART channels
    bool success = true;
    for (channel_id_t channel = 0; channel < UART_MANAGER_MAX_CHANNELS; channel++) {
        if (!init_channel(channel)) {
            log_event(EVENT_SOURCE_UART1, LOG_LEVEL_ERROR, LOG_EVENT_UART1_ERROR, channel);
            success = false;
            break;
        }
        // Small delay between channel inits to let PIO state machines settle
        // This helps prevent intermittent hangs when initializing multiple PIO blocks
        if (channel >= CHANNEL_2) {
            sleep_ms(10);
        }
    }
    
    if (success) {
        g_manager.initialized = true;
        g_manager.stats.status = UART_MANAGER_STATUS_READY;
        log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 1);
    } else {
        // Cleanup on failure
        for (channel_id_t channel = 0; channel < UART_MANAGER_MAX_CHANNELS; channel++) {
            deinit_channel(channel);
        }
        g_manager.stats.status = UART_MANAGER_STATUS_ERROR;
    }
    
    return success;
}

void uart_manager_deinit(void) {
    if (!g_manager.initialized) {
        return;
    }
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 0);
    
    for (channel_id_t channel = 0; channel < UART_MANAGER_MAX_CHANNELS; channel++) {
        deinit_channel(channel);
    }
    
    memset(&g_manager, 0, sizeof(uart_manager_t));
    
    log_event(EVENT_SOURCE_UART1, LOG_LEVEL_INFO, LOG_EVENT_UART_INIT, 1);
}

bool uart_manager_is_ready(void) {
    return g_manager.initialized && (g_manager.stats.status == UART_MANAGER_STATUS_READY);
}

uart_manager_status_t uart_manager_get_status(void) {
    return g_manager.stats.status;
}

bool uart_manager_has_incoming_work(void) {
    if (!uart_manager_is_ready()) {
        return false;
    }
    
    // Check if any channel has incoming data
    for (channel_id_t channel_idx = CHANNEL_0; channel_idx < CHANNEL_MAX; channel_idx++) {
        if (!DEVICE_CHANNEL_AVAILABLE(channel_idx)) continue;
        if (!shared_memory_get_layout()->config.channels[channel_idx].enabled) continue;
        
        uart_instance_t* uart = &g_manager.uarts[channel_idx];
        if (uart->ops && uart->ops->has_rx_data(uart->driver_context)) {
            return true;
        }
    }
    
    return false;
}

bool uart_manager_process_incoming_data(void) {
    
    bool data_processed = false;
    
    for (channel_id_t channel_idx = CHANNEL_0; channel_idx < CHANNEL_MAX; channel_idx++) {
        if (!DEVICE_CHANNEL_AVAILABLE(channel_idx)) continue;
        if (!shared_memory_get_layout()->config.channels[channel_idx].enabled) continue;
        
        if (process_channel_incoming_data(channel_idx)) {
            data_processed = true;
        }
    }
    
    if (data_processed) {
        update_manager_stats();
    }
    
    return data_processed;
}

bool uart_manager_process_outgoing_data(void) {
    static uint32_t call_counter = 0;
    call_counter++;
    
    if (!uart_manager_is_ready()) {
        return false;
    }
    
    bool data_processed = false;
 
    for (channel_id_t channel_idx = CHANNEL_0; channel_idx < CHANNEL_MAX; channel_idx++) {
        if (!DEVICE_CHANNEL_AVAILABLE(channel_idx)) continue;
        if (!shared_memory_get_layout()->config.channels[channel_idx].enabled) continue;
        
        if (process_channel_outgoing_data(channel_idx)) {
            data_processed = true;
        }
    }
    
    if (data_processed) {
        update_manager_stats();
    }
    
    return data_processed;
}

void uart_manager_get_stats(uart_manager_stats_t* stats) {
    if (!stats) return;
    
    update_manager_stats();
    memcpy(stats, &g_manager.stats, sizeof(uart_manager_stats_t));
}

void uart_manager_reset_stats(void) {
    memset(&g_manager.stats, 0, sizeof(uart_manager_stats_t));
    g_manager.stats.status = UART_MANAGER_STATUS_READY;
    g_manager.start_time = to_ms_since_boot(get_absolute_time());
    
    // Reset individual UART stats
    for (channel_id_t channel = 0; channel < UART_MANAGER_MAX_CHANNELS; channel++) {
        uart_instance_t* uart = &g_manager.uarts[channel];
        if (uart->ops && uart->ops->reset_stats) {
            uart->ops->reset_stats(uart->driver_context);
        }
    }
}

void uart_manager_set_debug(bool enable) {
    g_manager.debug_enabled = enable;
}

int uart_manager_get_diagnostic_info(char* info_buffer, size_t buffer_size) {
    if (!info_buffer || buffer_size == 0) {
        return 0;
    }
    
    update_manager_stats();
    
    int len = snprintf(info_buffer, buffer_size,
        "UART Manager Diagnostics:\n"
        "  Status: %s\n"
        "  Uptime: %lu seconds\n"
        "  Messages processed: %lu\n"
        "  TCP->UART: %lu, UART->TCP: %lu\n"
        "  Bytes TX: %lu, RX: %lu\n"
        "  Errors - TX: %lu, RX: %lu, Overflows: %lu\n"
        "  Active channels: %d\n",
        uart_manager_status_to_string(g_manager.stats.status),
        g_manager.stats.uptime_seconds,
        g_manager.stats.messages_processed,
        g_manager.stats.messages_tcp_to_uart,
        g_manager.stats.messages_uart_to_tcp,
        g_manager.stats.bytes_transmitted,
        g_manager.stats.bytes_received,
        g_manager.stats.transmission_errors,
        g_manager.stats.reception_errors,
        g_manager.stats.ring_buffer_overflows,
        UART_MANAGER_MAX_CHANNELS
    );
    // Add per-channel information
    for (channel_id_t channel_idx = CHANNEL_0; ((channel_idx < CHANNEL_MAX) && (len < buffer_size - 1)); channel_idx++) {
        channel_config_t channel = shared_memory_get_layout()->config.channels[channel_idx];
        if(channel.enabled) {
            const char* type_str = (channel.type == UART_TYPE_PL011) ? "PL011" : "PIO";
            len += snprintf(info_buffer + len, buffer_size - len,
                "  Channel %d: %s UART, %u baud, GP%u/GP%u\n",
                channel_idx, type_str, channel.baud_rate,
                channel.tx_gpio, channel.rx_gpio);
        }
    }
    
    return len;
}

const char* uart_manager_status_to_string(uart_manager_status_t status) {
    switch (status) {
        case UART_MANAGER_STATUS_UNINITIALIZED: return "Uninitialized";
        case UART_MANAGER_STATUS_INITIALIZING:  return "Initializing";
        case UART_MANAGER_STATUS_READY:         return "Ready";
        case UART_MANAGER_STATUS_ACTIVE:        return "Active";
        case UART_MANAGER_STATUS_ERROR:         return "Error";
        default:                                return "Unknown";
    }
}

uart_instance_t* uart_manager_get_channel_instance(channel_id_t channel) {
    if (!g_manager.initialized || channel >= UART_MANAGER_MAX_CHANNELS) {
        return NULL;
    }
    
    return &g_manager.uarts[channel];
}

// Private function implementations

static bool init_channel(channel_id_t channel_id) {
    if (channel_id >= UART_MANAGER_MAX_CHANNELS) {
        return false;
    }
    
    channel_config_t channel = shared_memory_get_layout()->config.channels[channel_id];
    
    // Skip disabled channels
    if (!channel.enabled) {
        return true;
    }
    
    uart_instance_t* uart = &g_manager.uarts[channel_id];
    
    uart->channel_id = channel_id;
    uart->type = channel.type;
    
    // Create context and set interface based on type
    if (channel.type == UART_TYPE_PL011) {
        // For PL011: channel 0 -> hw_uart 0, channel 1 -> hw_uart 1
        uart->driver_context = pl011_create_context(channel_id);
        uart->ops = &pl011_uart_interface;
    } else if (channel.type == UART_TYPE_PIO) {
        // PIO UART implementation - different drivers for different channels
        if (channel_id == CHANNEL_2) {
            // Channel 2: PIO0 SM0/SM1
            uart->driver_context = pio_create_context(0, 0);
            uart->ops = &pio_uart_interface;
        } else if (channel_id == CHANNEL_3) {
            // Channel 3: PIO1 SM0/SM1
            uart->driver_context = pio_ch3_create_context(1, 0);
            uart->ops = &pio_uart_ch3_interface;
        } else if (channel_id == CHANNEL_4) {
            // Channel 4: PIO2 SM0/SM1
            uart->driver_context = pio_ch4_create_context(2, 0);
            uart->ops = &pio_uart_ch4_interface;
        } else {
            printf("ERROR: PIO UART not supported for channel %u\n", channel_id);
            return false;
        }
    } else {
        printf("ERROR: Unknown UART type for channel %u\n", channel_id);
        return false;
    }
    
    if (!uart->driver_context || !uart->ops) {
        return false;
    }
    
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = channel.baud_rate,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .tx_gpio = channel.tx_gpio,
        .rx_gpio = channel.rx_gpio,
        .enable_flow_control = false
    };
    
    // Initialize the UART
    if (!uart->ops->init(uart->driver_context, &uart_config)) {
        if (channel.type == UART_TYPE_PL011) {
            pl011_destroy_context(uart->driver_context);
        }
        uart->driver_context = NULL;
        uart->ops = NULL;
        return false;
    }
    
    uart->config = uart_config;
    
    return true;
}

static void deinit_channel(channel_id_t channel) {
    if (channel >= UART_MANAGER_MAX_CHANNELS) {
        return;
    }
    
    uart_instance_t* uart = &g_manager.uarts[channel];
    
    if (uart->ops && uart->driver_context) {
        uart->ops->deinit(uart->driver_context);
    }
    
    if (uart->type == UART_TYPE_PL011 && uart->driver_context) {
        pl011_destroy_context(uart->driver_context);
    } else if (uart->type == UART_TYPE_PIO && uart->driver_context) {
        if (uart->channel_id == CHANNEL_2) {
            // Channel 2: Use general PIO UART destroy (PIO0)
            pio_destroy_context(uart->driver_context);
        } else if (uart->channel_id == CHANNEL_3) {
            // Channel 3: Use dedicated Channel 3 PIO UART destroy (PIO1)
            pio_ch3_destroy_context(uart->driver_context);
        } else if (uart->channel_id == CHANNEL_4) {
            // Channel 4: Use dedicated Channel 4 PIO UART destroy (PIO2)
            pio_ch4_destroy_context(uart->driver_context);
        }
    }
    
    memset(uart, 0, sizeof(uart_instance_t));
}

static bool process_channel_incoming_data(channel_id_t channel) {
    uart_instance_t* uart = &g_manager.uarts[channel];
    
    if (!uart->ops || !uart->ops->has_rx_data(uart->driver_context)) {
        return false;
    }

    uint8_t buffer[32];
    
    
    // Try to reuse an existing FILLING entry for this channel
    ring_entry_t* entry = ringbuffer_dequeue_entry(RX_UART_TO_TCP, channel, ENTRY_STATUS_FILLING);

    // Validate the existing entry or get a new one
    if (!entry || entry->fill_index >= RINGBUFFER_PAYLOAD_MAX_SIZE) {
        entry = ringbuffer_get_free_entry(RX_UART_TO_TCP, channel);
        
        // BUFFER PADDING FIX: Ensure payload is completely clear for new entries
        if (entry) {
            memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
            entry->fill_index = 0;
        }
    }
    
    if (!entry) {
        return false;
    }
            
    // Read data from UART
    size_t bytes_read = 0;
    int rounds = 0;
    int empty_rounds = 0;  // Count rounds with no data (for waiting on slow UARTs)
    const int MAX_EMPTY_ROUNDS = 20;  // Max retries when waiting for more data (20ms total)
    
    do {
        bytes_read = uart->ops->read_data(uart->driver_context, buffer, sizeof(buffer));
        
        // NOTE: No printf() allowed in this hot path!
        // printf() uses spin_lock_blocking() which disables interrupts,
        // causing PIO RX FIFO overflow and data loss.
        
        if (bytes_read == 0) {
            // No data available right now
            if (entry && entry->fill_index > 0) {
                // We have partial data - wait a bit for more to arrive (PIO UART timing)
                empty_rounds++;
                if (empty_rounds < MAX_EMPTY_ROUNDS) {
                    sleep_us(1000);  // Wait 1ms for more data to arrive
                    continue;
                }
                // Timeout waiting for more data - this is normal for end of message
            }
            break;  // No partial data or timeout - exit
        }
        
        empty_rounds = 0;  // Reset empty counter when we get data
        
        for (size_t idx = 0; idx < bytes_read; idx++) {
            uint8_t byte = buffer[idx];
            g_manager.stats.bytes_received++;
           
            // Ensure we have a valid entry (critical bug fix)
            if (!entry) {
                entry = ringbuffer_get_free_entry(RX_UART_TO_TCP, channel);
                if (!entry) {
                    return false; // Stop processing if no buffers available
                }
                // BUFFER PADDING FIX: Ensure payload is completely clear for new entries
                memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
                entry->fill_index = 0;
            }
           
            //we process incoming data directly into the ring buffer, using fill_index to track the progress
            entry->payload[entry->fill_index++] = byte;
            
            if(check_message_end(entry->payload, entry->fill_index)) {
                // NOTE: No printf() allowed in this hot path!
                // printf() disables interrupts, causing PIO RX FIFO overflow.
                
                // Check if this is a configuration command (except channel 0)
                bool is_cfg_cmd = uart_config_is_command(entry->payload, entry->fill_index);
                
                if (channel != CHANNEL_0 && is_cfg_cmd) {
                    // Process config command and send response back via UART
                    uint8_t response[CFG_MAX_RESPONSE];
                    size_t response_len = 0;
                    
                    if (uart_config_process_command(channel, entry->payload, entry->fill_index,
                                                     response, &response_len)) {
                        // Send response back on the same UART channel
                        if (response_len > 0 && uart->ops->send_data) {
                            uart->ops->send_data(uart->driver_context, response, response_len);
                        }
                    }
                    
                    // Don't forward config commands to TCP - just reset entry for reuse
                    memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
                    entry->fill_index = 0;
                } else {
                    // Normal message - enqueue for TCP transmission
                    ringbuffer_enqueue_entry(entry);
                    entry = NULL; // CRITICAL FIX: Clear entry pointer to force new allocation
                    
                    // NOTE: Do NOT call clear_rx_buffer() here!
                    // While processing this message, the IRQ handler may have already 
                    // received the next message (or part of it) into the ring buffer.
                    // Clearing it would discard that data and cause truncation.
                }
                
                // If there's more data to process, get a new entry immediately
                if(idx < bytes_read - 1 && entry == NULL) {
                    entry = ringbuffer_get_free_entry(RX_UART_TO_TCP, channel);
                    // BUFFER PADDING FIX: Ensure payload is completely clear for new entries
                    if (entry) {
                        memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
                        entry->fill_index = 0;
                    }
                }
            }
            else if (entry->fill_index >= RINGBUFFER_PAYLOAD_MAX_SIZE) {
                entry = ringbuffer_get_free_entry(RX_UART_TO_TCP, channel);
            }
        }
        
        // Ensure we have an entry for the next iteration if we're continuing
        if(bytes_read == sizeof(buffer) && !entry) {
            entry = ringbuffer_get_free_entry(RX_UART_TO_TCP, channel);
            // BUFFER PADDING FIX: Ensure payload is completely clear for new entries
            if (entry) {
                memset(entry->payload, 0, RINGBUFFER_PAYLOAD_MAX_SIZE);
                entry->fill_index = 0;
            }
        }
        
        rounds++;
    } while(bytes_read > 0 || (entry && entry->fill_index > 0 && empty_rounds < MAX_EMPTY_ROUNDS));

    return true;
}

            

static bool process_channel_outgoing_data(channel_id_t channel) {
    uart_instance_t* uart = &g_manager.uarts[channel];
    
    if (!uart->ops) {
        return false;
    }
    
    // Check if ready to send or busy
    if (!uart->ops->is_tx_complete(uart->driver_context)) {
        return false; // TX busy, try again later
    }

    // Get a ready message to send
    ring_entry_t* entry = ringbuffer_dequeue_entry(RX_TCP_TO_UART, channel, ENTRY_STATUS_READY);
    
    if (entry) {
        // Send data via UART - send as much as possible in one go
        const uint8_t* data = entry->payload;
        size_t data_len = entry->fill_index;
        
        size_t sent = uart->ops->send_data(uart->driver_context, data, data_len);
        g_manager.stats.bytes_transmitted += sent;
        
        // Mark message as consumed regardless of how much was sent
        // The PIO driver will handle partial sends via DMA/FIFO as needed
        ringbuffer_mark_consumed(entry);
        
        return true;
    }
    
    // No entries ready for work
    return false;
}

static void update_manager_stats(void) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    g_manager.stats.uptime_seconds = (current_time - g_manager.start_time) / 1000;
    g_manager.stats.messages_processed = g_manager.stats.messages_tcp_to_uart + 
                                        g_manager.stats.messages_uart_to_tcp;
}

/**
 * @brief Check if buffer ends with exactly '!\r\n'
 */
bool check_message_end(const uint8_t* buffer, size_t length) {
    if (!buffer || length < MINIMUM_MESSAGE_LENGTH) {
        return false;
    }
    
    return buffer[length-3] == '!' && 
           buffer[length-2] == '\r' && 
           buffer[length-1] == '\n';
}