/**
 * @file pio_uart_channel1_driver.c
 * @brief PIO UART driver implementation for Channel 1 (GPIO 4,5)
 * 
 * Dedicated PIO UART implementation for Channel 1 testing.
 * Uses GPIO 4 (TX) and GPIO 5 (RX) with PIO0 state machines 2,3.
 * 
 * Based on the working Channel 2 implementation but configured for Channel 1.
 */

#include "uart/pio_uart_channel1_driver.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Import PIO programs  
#include "pio_uart_tx.pio.h"
#include "pio_uart_rx.pio.h"

// Static context for Channel 1
static pio_uart_context_t pio_uart_ch1_context;
static bool pio_uart_ch1_context_initialized = false;

// Forward declarations
static bool pio_uart_ch1_init(void* context, const uart_config_t* config);
static void pio_uart_ch1_deinit(void* context);
static bool pio_uart_ch1_is_ready(void* context);
static void pio_uart_ch1_send_byte(void* context, uint8_t byte);
static size_t pio_uart_ch1_send_data(void* context, const uint8_t* data, size_t len);
static bool pio_uart_ch1_is_tx_ready(void* context);
static bool pio_uart_ch1_is_tx_complete(void* context);
static bool pio_uart_ch1_has_rx_data(void* context);
static uint8_t pio_uart_ch1_read_byte(void* context);
static size_t pio_uart_ch1_read_data(void* context, uint8_t* buffer, size_t max_len);
static size_t pio_uart_ch1_get_rx_count(void* context);
static void pio_uart_ch1_clear_rx_buffer(void* context);
static const uart_state_t* pio_uart_ch1_get_state(void* context);
static void pio_uart_ch1_reset_stats(void* context);

// Internal helper functions
static void pio_uart_ch1_dma_tx_irq_handler(void);
static void pio_uart_ch1_dma_rx_irq_handler(void);
static void pio_uart_ch1_pio_rx_irq_handler(void);

// Interface table for Channel 1
const uart_interface_t pio_uart_ch1_interface = {
    .init = pio_uart_ch1_init,
    .deinit = pio_uart_ch1_deinit,
    .is_ready = pio_uart_ch1_is_ready,
    .send_byte = pio_uart_ch1_send_byte,
    .send_data = pio_uart_ch1_send_data,
    .is_tx_ready = pio_uart_ch1_is_tx_ready,
    .is_tx_complete = pio_uart_ch1_is_tx_complete,
    .has_rx_data = pio_uart_ch1_has_rx_data,
    .read_byte = pio_uart_ch1_read_byte,
    .read_data = pio_uart_ch1_read_data,
    .get_rx_count = pio_uart_ch1_get_rx_count,
    .clear_rx_buffer = pio_uart_ch1_clear_rx_buffer,
    .get_state = pio_uart_ch1_get_state,
    .reset_stats = pio_uart_ch1_reset_stats
};

// Context management for Channel 1
void* pio_ch1_create_context(uint8_t pio_num, uint8_t sm_num) {
    (void)pio_num;  // We use fixed PIO0
    (void)sm_num;   // We use fixed state machines 2,3
    
    if (pio_uart_ch1_context_initialized) {
        printf("ERROR: PIO UART Channel 1 context already initialized\n");
        return NULL;
    }
    
    memset(&pio_uart_ch1_context, 0, sizeof(pio_uart_context_t));
    
    // Configure PIO settings for Channel 1
    pio_uart_ch1_context.pio_instance = pio0;
    pio_uart_ch1_context.tx_sm = 2;  // Use state machine 2 for TX
    pio_uart_ch1_context.rx_sm = 3;  // Use state machine 3 for RX
    pio_uart_ch1_context.tx_gpio = 4;  // GPIO 4 for Channel 1 TX
    pio_uart_ch1_context.rx_gpio = 5;  // GPIO 5 for Channel 1 RX
    
    // Initialize DMA channels as unclaimed
    pio_uart_ch1_context.tx_dma_chan = (uint)-1;
    pio_uart_ch1_context.rx_dma_chan = (uint)-1;
    pio_uart_ch1_context.dma_channels_claimed = false;
    
    // Initialize receive buffer
    uart_receive_buffer_init(&pio_uart_ch1_context.rx_ring, 
                           pio_uart_ch1_context.rx_buffer, 
                           PIO_UART_RX_BUFFER_SIZE);
    
    // Initialize state
    pio_uart_ch1_context.state.initialized = false;
    pio_uart_ch1_context.state.ready = false;
    pio_uart_ch1_context.state.error = false;
    pio_uart_ch1_context.tx_in_progress = false;
    pio_uart_ch1_context.rx_bytes_ready = 0;
    
    pio_uart_ch1_context_initialized = true;
    
    printf("PIO UART Channel 1 context created successfully\n");
    return &pio_uart_ch1_context;
}

void pio_ch1_destroy_context(void* context) {
    if (context && context == &pio_uart_ch1_context) {
        if (pio_uart_ch1_context.state.initialized) {
            pio_uart_ch1_deinit(context);
        }
        pio_uart_ch1_context_initialized = false;
        printf("PIO UART Channel 1 context destroyed\n");
    }
}

// Setup PIO programs for Channel 1
static bool pio_uart_ch1_setup_programs(pio_uart_context_t* ctx) {
    // Add TX program
    ctx->tx_offset = pio_add_program(ctx->pio_instance, &uart_tx_program);
    printf("PIO UART Ch1: TX program loaded at offset %u\n", ctx->tx_offset);
    
    // Add RX program  
    ctx->rx_offset = pio_add_program(ctx->pio_instance, &uart_rx_program);
    printf("PIO UART Ch1: RX program loaded at offset %u\n", ctx->rx_offset);
    
    return true;
}

// Setup DMA for Channel 1
static bool pio_uart_ch1_setup_dma(pio_uart_context_t* ctx) {
    // Claim DMA channels
    ctx->tx_dma_chan = dma_claim_unused_channel(true);
    ctx->rx_dma_chan = dma_claim_unused_channel(true);
    
    if (ctx->tx_dma_chan == (uint)-1 || ctx->rx_dma_chan == (uint)-1) {
        printf("PIO UART Ch1: Failed to claim DMA channels\n");
        return false;
    }
    
    ctx->dma_channels_claimed = true;
    printf("PIO UART Ch1: DMA setup complete - TX:%u RX:%u\n", ctx->tx_dma_chan, ctx->rx_dma_chan);
    
    return true;
}

// Initialize PIO UART for Channel 1
static bool pio_uart_ch1_init(void* context, const uart_config_t* config) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || ctx->state.initialized) {
        return false;
    }
    
    printf("PIO UART Ch1: Initializing - %u baud, TX:%u RX:%u\n", 
           config->baud_rate, config->tx_gpio, config->rx_gpio);
    
    // Store configuration
    ctx->current_config = *config;
    
    // Override GPIO pins to ensure Channel 1 uses GPIO 4,5
    ctx->tx_gpio = 4;
    ctx->rx_gpio = 5;
    
    printf("PIO UART Ch1: Setting up PIO programs...\n");
    if (!pio_uart_ch1_setup_programs(ctx)) {
        printf("PIO UART Ch1: Failed to setup PIO programs\n");
        return false;
    }
    
    printf("PIO UART Ch1: Setting up DMA channels...\n");
    if (!pio_uart_ch1_setup_dma(ctx)) {
        printf("PIO UART Ch1: Failed to setup DMA\n");
        return false;
    }
    
    // Configure TX state machine
    pio_sm_set_pins_with_mask(ctx->pio_instance, ctx->tx_sm, 1u << ctx->tx_gpio, 1u << ctx->tx_gpio);
    pio_sm_set_pindirs_with_mask(ctx->pio_instance, ctx->tx_sm, 1u << ctx->tx_gpio, 1u << ctx->tx_gpio);
    pio_gpio_init(ctx->pio_instance, ctx->tx_gpio);
    gpio_set_outover(ctx->tx_gpio, GPIO_OVERRIDE_NORMAL);
    
    // Configure RX state machine
    pio_gpio_init(ctx->pio_instance, ctx->rx_gpio);
    gpio_set_inover(ctx->rx_gpio, GPIO_OVERRIDE_NORMAL);
    
    // Calculate clock dividers for baud rate
    float div = (float)clock_get_hz(clk_sys) / (8 * config->baud_rate);
    
    // Initialize TX state machine
    pio_sm_config tx_config = uart_tx_program_get_default_config(ctx->tx_offset);
    sm_config_set_out_pins(&tx_config, ctx->tx_gpio, 1);
    sm_config_set_fifo_join(&tx_config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&tx_config, div);
    pio_sm_init(ctx->pio_instance, ctx->tx_sm, ctx->tx_offset, &tx_config);
    pio_sm_set_enabled(ctx->pio_instance, ctx->tx_sm, true);
    
    // Initialize RX state machine  
    pio_sm_config rx_config = uart_rx_program_get_default_config(ctx->rx_offset);
    sm_config_set_in_pins(&rx_config, ctx->rx_gpio);
    sm_config_set_fifo_join(&rx_config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&rx_config, div);
    pio_sm_init(ctx->pio_instance, ctx->rx_sm, ctx->rx_offset + uart_rx_offset_wait, &rx_config);
    pio_sm_set_enabled(ctx->pio_instance, ctx->rx_sm, true);
    
    // Mark as initialized
    ctx->state.initialized = true;
    ctx->state.ready = true;
    ctx->state.error = false;
    
    printf("PIO UART Ch1: Initialization complete\n");
    return true;
}

// Send data implementation for Channel 1
static size_t pio_uart_ch1_send_data(void* context, const uint8_t* data, size_t len) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.ready || !data || len == 0) {
        return 0;
    }
    
    printf("PIO UART Ch1: Sending %zu bytes\n", len);
    
    // For simplicity, send data directly to PIO FIFO
    size_t sent = 0;
    for (size_t i = 0; i < len; i++) {
        if (!pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm)) {
            pio_sm_put(ctx->pio_instance, ctx->tx_sm, data[i]);
            sent++;
        } else {
            break;
        }
    }
    
    if (sent > 0) {
        ctx->state.tx_count++;
        ctx->state.tx_bytes += sent;
        printf("PIO UART Ch1: Sent %zu bytes to FIFO\n", sent);
    }
    
    return sent;
}

// Check if RX data available for Channel 1
static bool pio_uart_ch1_has_rx_data(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.ready) {
        return false;
    }
    
    // Check PIO RX FIFO and transfer to ring buffer
    while (!pio_sm_is_rx_fifo_empty(ctx->pio_instance, ctx->rx_sm)) {
        uint32_t data = pio_sm_get(ctx->pio_instance, ctx->rx_sm);
        uint8_t byte = (uint8_t)(data & 0xFF);
        
        if (uart_receive_buffer_put(&ctx->rx_ring, byte)) {
            ctx->state.rx_count++;
            ctx->state.rx_bytes++;
        } else {
            ctx->state.overrun_errors++;
        }
    }
    
    return uart_receive_buffer_available(&ctx->rx_ring) > 0;
}

// Read data from Channel 1
static size_t pio_uart_ch1_read_data(void* context, uint8_t* buffer, size_t max_len) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.ready || !buffer || max_len == 0) {
        return 0;
    }
    
    // First check for new data in PIO FIFO
    pio_uart_ch1_has_rx_data(context);
    
    // Read from ring buffer
    size_t read = 0;
    for (size_t i = 0; i < max_len; i++) {
        uint8_t byte;
        if (uart_receive_buffer_get(&ctx->rx_ring, &byte)) {
            buffer[i] = byte;
            read++;
        } else {
            break;
        }
    }
    
    if (read > 0) {
        printf("PIO UART Ch1: Read %zu bytes from buffer\n", read);
    }
    
    return read;
}

// Other required interface functions - simplified implementations
static void pio_uart_ch1_deinit(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx) return;
    
    ctx->state.initialized = false;
    ctx->state.ready = false;
    printf("PIO UART Ch1: Deinitialized\n");
}

static bool pio_uart_ch1_is_ready(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    return ctx && ctx->state.ready;
}

static void pio_uart_ch1_send_byte(void* context, uint8_t byte) {
    pio_uart_ch1_send_data(context, &byte, 1);
}

static bool pio_uart_ch1_is_tx_ready(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    return ctx && ctx->state.ready && !pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm);
}

static bool pio_uart_ch1_is_tx_complete(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    return ctx && !ctx->tx_in_progress && pio_sm_is_tx_fifo_empty(ctx->pio_instance, ctx->tx_sm);
}

static uint8_t pio_uart_ch1_read_byte(void* context) {
    uint8_t byte = 0;
    pio_uart_ch1_read_data(context, &byte, 1);
    return byte;
}

static size_t pio_uart_ch1_get_rx_count(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.ready) return 0;
    
    // Check for new data first
    pio_uart_ch1_has_rx_data(context);
    return uart_receive_buffer_available(&ctx->rx_ring);
}

static void pio_uart_ch1_clear_rx_buffer(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (ctx && ctx->state.ready) {
        uart_receive_buffer_clear(&ctx->rx_ring);
    }
}

static const uart_state_t* pio_uart_ch1_get_state(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    return ctx ? &ctx->state : NULL;
}

static void pio_uart_ch1_reset_stats(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (ctx) {
        ctx->state.tx_count = 0;
        ctx->state.tx_bytes = 0;
        ctx->state.rx_count = 0;
        ctx->state.rx_bytes = 0;
        ctx->state.overrun_errors = 0;
        ctx->pio_errors = 0;
        ctx->dma_errors = 0;
    }
}

// Placeholder interrupt handlers (not used in simplified implementation)
static void pio_uart_ch1_dma_tx_irq_handler(void) {}
static void pio_uart_ch1_dma_rx_irq_handler(void) {}
static void pio_uart_ch1_pio_rx_irq_handler(void) {}
