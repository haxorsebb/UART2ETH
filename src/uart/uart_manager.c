/**
 * @file uart_manager.c
 * @brief UART Manager implementation - manages 4 UART channels
 */

#include "uart/uart_manager.h"
#include "shared_memory.h"
#include "uart/uart_interface.h"
#include "ringbuffer.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// External interfaces
extern const uart_interface_t pl011_uart_interface;
extern void* pl011_create_context(uint8_t hw_uart_num);
extern void pl011_destroy_context(void* context);

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

// Channel configuration
typedef struct {
    bool enabled;
    uart_type_t type;
    uint32_t baud_rate;
    uint8_t tx_gpio;
    uint8_t rx_gpio;
} channel_config_t;

static const channel_config_t channel_configs[UART_MANAGER_MAX_CHANNELS] = {
    // Channel 0: UART0 (PL011) - disabled
    {false, UART_TYPE_PL011, 230400, 0, 1},
    // Channel 1: UART1 (PL011) - enabled
    {true, UART_TYPE_PL011, 115200, 4, 5},
    // Channel 2: PIO UART (placeholder) - disabled
    {false, UART_TYPE_PIO, 230400, 4, 5},
    // Channel 3: PIO UART (placeholder) - disabled
    {false, UART_TYPE_PIO, 230400, 6, 7}
};

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
    
    // Initialize UART channels
    bool success = true;
    for (channel_id_t channel = 0; channel < UART_MANAGER_MAX_CHANNELS; channel++) {
        if (!init_channel(channel)) {
            log_event(EVENT_SOURCE_UART1, LOG_LEVEL_ERROR, LOG_EVENT_UART1_ERROR, channel);
            success = false;
            break;
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
    for (channel_id_t channel = 0; channel < UART_MANAGER_MAX_CHANNELS; channel++) {
        if (!channel_configs[channel].enabled) continue;
        
        uart_instance_t* uart = &g_manager.uarts[channel];
        if (uart->ops && uart->ops->has_rx_data(uart->driver_context)) {
            return true;
        }
    }
    
    return false;
}

bool uart_manager_process_incoming_data(void) {
    
    bool data_processed = false;
    
    for (channel_id_t channel = 0; channel < UART_MANAGER_MAX_CHANNELS; channel++) {
        if (!channel_configs[channel].enabled) continue;
        
        if (process_channel_incoming_data(channel)) {
            data_processed = true;
        }
    }
    
    if (data_processed) {
        update_manager_stats();
    }
    
    return data_processed;
}

bool uart_manager_process_outgoing_data(void) {
    if (!uart_manager_is_ready()) {
        return false;
    }
    
    bool data_processed = false;
    
    for (channel_id_t channel = 0; channel < CHANNEL_MAX; channel++) {
        if (!channel_configs[channel].enabled) continue;
        
        if (process_channel_outgoing_data(channel)) {
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
    
    return snprintf(info_buffer, buffer_size,
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

// Private function implementations

static bool init_channel(channel_id_t channel) {
    if (channel >= UART_MANAGER_MAX_CHANNELS) {
        return false;
    }
    
    const channel_config_t* config = &channel_configs[channel];
    
    // Skip disabled channels
    if (!config->enabled) {
        return true;
    }
    
    uart_instance_t* uart = &g_manager.uarts[channel];
    
    uart->channel_id = channel;
    uart->type = config->type;
    
    // Create context and set interface based on type
    if (config->type == UART_TYPE_PL011) {
        // For PL011: channel 0 -> hw_uart 0, channel 1 -> hw_uart 1
        uart->driver_context = pl011_create_context(channel);
        uart->ops = &pl011_uart_interface;
    } else {
        // PIO UART - placeholder for now
        uart->driver_context = NULL;
        uart->ops = NULL;
        return true; // Skip PIO initialization for now
    }
    
    if (!uart->driver_context || !uart->ops) {
        return false;
    }
    
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .tx_gpio = config->tx_gpio,
        .rx_gpio = config->rx_gpio,
        .enable_flow_control = false
    };
    
    // Initialize the UART
    if (!uart->ops->init(uart->driver_context, &uart_config)) {
        if (config->type == UART_TYPE_PL011) {
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
    }
    
    memset(uart, 0, sizeof(uart_instance_t));
}

static bool process_channel_incoming_data(channel_id_t channel) {
    uart_instance_t* uart = &g_manager.uarts[channel];
    
    if (!uart->ops || !uart->ops->has_rx_data(uart->driver_context)) {
        return false;
    }

    uint8_t buffer[32];
    
    
    ring_entry_t* entry = ringbuffer_dequeue_entry(RX_UART_TO_TCP, channel, ENTRY_STATUS_FILLING);

    if (!entry) {
        entry = ringbuffer_get_free_entry( RX_UART_TO_TCP, channel);
    }
            
    // Read data from UART
    size_t bytes_read = 0;
    int rounds=0;
    do {
        bytes_read = uart->ops->read_data(uart->driver_context, buffer, sizeof(buffer));
        
        for (size_t idx = 0; idx < bytes_read; idx++) {
            uint8_t byte = buffer[idx];
            g_manager.stats.bytes_received++;
           
            //we process incoming data directly into the ring buffer, using fill_index to track the progress
            entry->payload[entry->fill_index++] = byte;
            if(check_message_end(entry->payload, entry->fill_index)) {
                ringbuffer_enqueue_entry(entry);
                // still data left
                if(idx < bytes_read) {
                    entry = ringbuffer_get_free_entry( RX_UART_TO_TCP, channel);
                }
            }
            if (entry->fill_index >= RINGBUFFER_PAYLOAD_MAX_SIZE) {
                entry = ringbuffer_get_free_entry( RX_UART_TO_TCP, channel);
            }
        }
        rounds++;
    } while(bytes_read == sizeof(buffer));

    return true;
}

            

static bool process_channel_outgoing_data(channel_id_t channel) {
    uart_instance_t* uart = &g_manager.uarts[channel];
    
    if (!uart->ops) {
        return false;
    }
    
    //check if ready to send or busy
    if(!uart->ops->is_tx_complete)
    {
        printf("UART DEBUG: trying to send on channel %d, BUT TX BUSY!\n", channel );
        return false;
    }

    // Get active draining message from ring buffer for this channel
    ring_entry_t* entry = ringbuffer_dequeue_entry(RX_TCP_TO_UART, channel,ENTRY_STATUS_DRAINING);
    //is this TX COMPLETE?
    if(entry)
    {
        //mark done
        entry->status = ENTRY_STATUS_READY;
        //mark free
        ringbuffer_mark_consumed(entry);
    }
    
    //check if there are any other messages waiting for draining
    entry = ringbuffer_dequeue_entry(RX_TCP_TO_UART, channel,ENTRY_STATUS_READY);
    if(entry)
    {
        // Send data via UART
        const uint8_t* data = entry->payload;
        size_t remaining = entry->fill_index - entry->drain_index;
        entry->status = ENTRY_STATUS_DRAINING;
        size_t sent = uart->ops->send_data(uart->driver_context, data + entry->drain_index, remaining);
        g_manager.stats.bytes_transmitted += sent;
        
        return true;
    }
    
    //there was not entry ready for some work
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