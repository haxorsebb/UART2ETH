/**
 * @file pl011_driver.c
 * @brief PL011 UART driver implementation
 */

#include "uart/pl011_driver.h"

#include "uart/uart_interface.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "uart/uart_receive_buffer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Static lookup table for ISR context resolution
static pl011_context_t pl011_contexts[2];

// Forward declarations
static bool pl011_init(void* context, const uart_config_t* config);
static void pl011_deinit(void* context);
static bool pl011_is_ready(void* context);
static void pl011_send_byte(void* context, uint8_t byte);
static size_t pl011_send_data(void* context, const uint8_t* data, size_t len);
static bool pl011_is_tx_ready(void* context);
static bool pl011_is_tx_complete(void* context);
static bool pl011_has_rx_data(void* context);
static uint8_t pl011_read_byte(void* context);
static size_t pl011_read_data(void* context, uint8_t* buffer, size_t max_len);
static size_t pl011_get_rx_count(void* context);
static void pl011_clear_rx_buffer(void* context);
static const uart_state_t* pl011_get_state(void* context);
static void pl011_reset_stats(void* context);

static void pl011_handle_interrupt(pl011_context_t* ctx);
static void uart0_isr_handler(void);
static void uart1_isr_handler(void);

// Interface table
const uart_interface_t pl011_uart_interface = {
    .init = pl011_init,
    .deinit = pl011_deinit,
    .is_ready = pl011_is_ready,
    .send_byte = pl011_send_byte,
    .send_data = pl011_send_data,
    .is_tx_ready = pl011_is_tx_ready,
    .is_tx_complete = pl011_is_tx_complete,
    .has_rx_data = pl011_has_rx_data,
    .read_byte = pl011_read_byte,
    .read_data = pl011_read_data,
    .get_rx_count = pl011_get_rx_count,
    .clear_rx_buffer = pl011_clear_rx_buffer,
    .get_state = pl011_get_state,
    .reset_stats = pl011_reset_stats
};

// Context management
void* pl011_create_context(uint8_t hw_uart_num ) {
    if (hw_uart_num > 1) return NULL;
    
    pl011_context_t* ctx = &pl011_contexts[hw_uart_num];
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(pl011_context_t));
    ctx->hw_uart_num = hw_uart_num;
    ctx->hw_uart = (hw_uart_num == 0) ? uart0 : uart1;
    ctx->irq_num = (hw_uart_num == 0) ? UART0_IRQ : UART1_IRQ;
    
    uart_receive_buffer_init(&ctx->rx_ring, ctx->rx_buffer, RX_BUFFER_SIZE);
    
    return ctx;
}

void pl011_destroy_context(void* context) {
    if (context) {
        pl011_context_t* ctx = context;
        if (ctx->state.initialized) {
            pl011_deinit(context);
        }
        //free(context);
    }
}

// Interface implementations
static bool pl011_init(void* context, const uart_config_t* config) {
    pl011_context_t* ctx = context;
    if (!ctx || ctx->state.initialized) return false;
    
    
    // Configure GPIO pins
    gpio_set_function(config->tx_gpio, UART_FUNCSEL_NUM(ctx->hw_uart, config->tx_gpio));
    gpio_set_function(config->rx_gpio, UART_FUNCSEL_NUM(ctx->hw_uart, config->rx_gpio));
    
    // Initialize UART hardware
    uart_init(ctx->hw_uart, config->baud_rate);
    uart_set_format(ctx->hw_uart, config->data_bits, config->stop_bits, config->parity);
    uart_set_hw_flow(ctx->hw_uart, false, false);
    
    // Enable interrupts
    uart_set_irq_enables(ctx->hw_uart, true, false); // RX and TX interrupts
    
    // Register ISR
    if (ctx->hw_uart_num == 0) {
        irq_set_exclusive_handler(UART0_IRQ, uart0_isr_handler);
    } else {
        irq_set_exclusive_handler(UART1_IRQ, uart1_isr_handler);
    }
    irq_set_enabled(ctx->irq_num, true);
    
    // Update state
    ctx->state.initialized = true;
    ctx->state.ready = true;
    ctx->init_time = to_ms_since_boot(get_absolute_time());
    
    return true;
}

static void pl011_deinit(void* context) {
    pl011_context_t* ctx = context;
    if (!ctx) return;
    
    // Disable interrupts
    irq_set_enabled(ctx->irq_num, false);
    uart_set_irq_enables(ctx->hw_uart, false, false);
    
    // Deinitialize UART
    uart_deinit(ctx->hw_uart);
    
    // Clear lookup table
    pl011_destroy_context(&pl011_contexts[ctx->hw_uart_num]);
    
    // Reset state
    memset(&ctx->state, 0, sizeof(uart_state_t));
}

static bool pl011_is_ready(void* context) {
    pl011_context_t* ctx = context;
    return ctx && ctx->state.ready;
}

static void pl011_send_byte(void* context, uint8_t byte) {
    // Block until byte is sent
    while (pl011_send_data(context, &byte, 1) == 0) {
        __wfi(); // Wait for interrupt
    }
}

static size_t pl011_send_data(void* context, const uint8_t* data, size_t len) {
    pl011_context_t* ctx = context;
    if (!ctx || !ctx->state.ready || !data || len == 0) return 0;
    
    size_t sent = 0;
    
    // Send directly to FIFO if space available
    while (sent < len && uart_is_writable(ctx->hw_uart)) {
        uart_putc_raw(ctx->hw_uart, data[sent]);
        sent++;
        ctx->state.bytes_sent++;
    }
    
    // If not all data sent, set up TX buffer for ISR
    if (sent < len) {
        ctx->tx_buffer_ptr = data + sent;
        ctx->tx_buffer_remaining = len - sent;
        uart_set_irq_enables(ctx->hw_uart, true, true); // RX and TX interrupts
    }
    else {
        //already sent in one go!
        ctx->tx_buffer_ptr = NULL;
        ctx->tx_buffer_remaining = 0;
        uart_set_irq_enables(ctx->hw_uart, true, false); // RX and TX interrupts
    }
    
    return sent;
}

static bool pl011_is_tx_ready(void* context) {
    pl011_context_t* ctx = context;
    return ctx && ctx->state.ready && uart_is_writable(ctx->hw_uart);
}

static bool pl011_is_tx_complete(void* context) {
    pl011_context_t* ctx = context;
    return ctx && (ctx->tx_buffer_ptr == NULL);
}

static bool pl011_has_rx_data(void* context) {
    pl011_context_t* ctx = context;
    return ctx && uart_receive_buffer_available(&ctx->rx_ring) > 0;
}

static uint8_t pl011_read_byte(void* context) {
    uint8_t byte;
    // Block until data available
    while (pl011_read_data(context, &byte, 1) == 0) {
        __wfi(); // Wait for interrupt
    }
    return byte;
}

static size_t pl011_read_data(void* context, uint8_t* buffer, size_t max_len) {
    pl011_context_t* ctx = context;
    if (!ctx || !buffer || max_len == 0) return 0;
    
    return uart_receive_buffer_read(&ctx->rx_ring, buffer, max_len);
}

static size_t pl011_get_rx_count(void* context) {
    pl011_context_t* ctx = context;
    return ctx ? uart_receive_buffer_available(&ctx->rx_ring) : 0;
}

static void pl011_clear_rx_buffer(void* context) {
    pl011_context_t* ctx = context;
    if (ctx) {
        uart_receive_buffer_clear(&ctx->rx_ring);
    }
}

static const uart_state_t* pl011_get_state(void* context) {
    pl011_context_t* ctx = context;
    if (ctx) {
        ctx->state.uptime_ms = to_ms_since_boot(get_absolute_time()) - ctx->init_time;
    }
    return ctx ? &ctx->state : NULL;
}

static void pl011_reset_stats(void* context) {
    pl011_context_t* ctx = context;
    if (ctx) {
        ctx->state.bytes_sent = 0;
        ctx->state.bytes_received = 0;
        ctx->state.tx_errors = 0;
        ctx->state.rx_errors = 0;
        ctx->state.overrun_errors = 0;
        ctx->init_time = to_ms_since_boot(get_absolute_time());
    }
}

// ISR handlers
static void pl011_handle_interrupt(pl011_context_t* ctx) {
    if (!ctx) return;
    
    // Handle RX interrupt
    while (uart_is_readable(ctx->hw_uart)) {
        uint8_t byte = uart_getc(ctx->hw_uart);
        if (!uart_receive_buffer_put(&ctx->rx_ring, byte)) {
            ctx->state.overrun_errors++;
            
        } else {
            ctx->state.bytes_received++;
        }
    }
    
    // Handle TX interrupt
    if (ctx->tx_buffer_ptr) {
        while (ctx->tx_buffer_remaining > 0 && uart_is_writable(ctx->hw_uart)) {
            uart_putc_raw(ctx->hw_uart, *ctx->tx_buffer_ptr);
            ctx->tx_buffer_ptr++;
            ctx->tx_buffer_remaining--;
            ctx->state.bytes_sent++;
        }
        
        // Clear TX buffer pointer when complete
        if (ctx->tx_buffer_remaining == 0) {
            ctx->tx_buffer_ptr = NULL;
            uart_set_irq_enables(ctx->hw_uart, true, false); // RX and TX interrupts
        }
    }
}

static void uart0_isr_handler(void) {
    pl011_handle_interrupt(&pl011_contexts[0]);
}

static void uart1_isr_handler(void) {
    pl011_handle_interrupt(&pl011_contexts[1]);
}