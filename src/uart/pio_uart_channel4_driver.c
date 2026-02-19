/**
 * @file pio_uart_channel4_driver.c
 * @brief PIO UART driver implementation for Channel 4 (PIO2)
 * 
 * Implements software UART functionality using RP2350 PIO2 state machines.
 * Copy of Channel 2 driver adapted for PIO2.
 */

#include "uart/pio_uart_channel4_driver.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Import PIO programs  
#include "pio_uart_tx.pio.h"
#include "pio_uart_rx.pio.h"

// Static single PIO UART instance context for Channel 4
static pio_uart_ch4_context_t pio_uart_ch4_context;
static bool pio_uart_ch4_context_initialized = false;

// Forward declarations
static bool pio_uart_ch4_init(void* context, const uart_config_t* config);
static void pio_uart_ch4_deinit(void* context);
static bool pio_uart_ch4_is_ready(void* context);
static void pio_uart_ch4_send_byte(void* context, uint8_t byte);
static size_t pio_uart_ch4_send_data(void* context, const uint8_t* data, size_t len);
static bool pio_uart_ch4_is_tx_ready(void* context);
static bool pio_uart_ch4_is_tx_complete(void* context);
static bool pio_uart_ch4_has_rx_data(void* context);
static uint8_t pio_uart_ch4_read_byte(void* context);
static size_t pio_uart_ch4_read_data(void* context, uint8_t* buffer, size_t max_len);
static size_t pio_uart_ch4_get_rx_count(void* context);
static void pio_uart_ch4_clear_rx_buffer(void* context);
static const uart_state_t* pio_uart_ch4_get_state(void* context);
static void pio_uart_ch4_reset_stats(void* context);

// Internal helper functions
static void pio_uart_ch4_pio_rx_irq_handler(void);

// Interface table
const uart_interface_t pio_uart_ch4_interface = {
    .init = pio_uart_ch4_init,
    .deinit = pio_uart_ch4_deinit,
    .is_ready = pio_uart_ch4_is_ready,
    .send_byte = pio_uart_ch4_send_byte,
    .send_data = pio_uart_ch4_send_data,
    .is_tx_ready = pio_uart_ch4_is_tx_ready,
    .is_tx_complete = pio_uart_ch4_is_tx_complete,
    .has_rx_data = pio_uart_ch4_has_rx_data,
    .read_byte = pio_uart_ch4_read_byte,
    .read_data = pio_uart_ch4_read_data,
    .get_rx_count = pio_uart_ch4_get_rx_count,
    .clear_rx_buffer = pio_uart_ch4_clear_rx_buffer,
    .get_state = pio_uart_ch4_get_state,
    .reset_stats = pio_uart_ch4_reset_stats
};

// Context management
void* pio_ch4_create_context(uint8_t pio_num, uint8_t sm_num) {
    (void)pio_num;  // We use fixed PIO2
    (void)sm_num;   // We use fixed state machines
    
    if (pio_uart_ch4_context_initialized) {
        printf("ERROR: PIO UART CH4 context already initialized\n");
        return NULL;
    }
    
    memset(&pio_uart_ch4_context, 0, sizeof(pio_uart_ch4_context_t));
    
    // Configure PIO settings for Channel 4 - PIO2
    pio_uart_ch4_context.pio_instance = PIO_UART_CH4_PIO_INSTANCE;
    pio_uart_ch4_context.tx_sm = PIO_UART_CH4_TX_SM;
    pio_uart_ch4_context.rx_sm = PIO_UART_CH4_RX_SM;
    pio_uart_ch4_context.tx_gpio = PIO_UART_CH4_TX_GPIO;
    pio_uart_ch4_context.rx_gpio = PIO_UART_CH4_RX_GPIO;
    
    // No DMA for this channel (use FIFO-based transfers)
    pio_uart_ch4_context.tx_dma_chan = PIO_UART_CH4_INVALID_DMA_CHANNEL;
    pio_uart_ch4_context.rx_dma_chan = PIO_UART_CH4_INVALID_DMA_CHANNEL;
    pio_uart_ch4_context.dma_channels_claimed = false;
    
    // Initialize receive buffer
    uart_receive_buffer_init(&pio_uart_ch4_context.rx_ring, 
                           pio_uart_ch4_context.rx_buffer, 
                           PIO_UART_CH4_RX_BUFFER_SIZE);
    
    pio_uart_ch4_context_initialized = true;
    printf("PIO UART CH4 (PIO2) context created successfully\n");
    
    return &pio_uart_ch4_context;
}

void pio_ch4_destroy_context(void* context) {
    if (context == &pio_uart_ch4_context && pio_uart_ch4_context_initialized) {
        if (pio_uart_ch4_context.state.initialized) {
            pio_uart_ch4_deinit(context);
        }
        
        pio_uart_ch4_context_initialized = false;
        printf("PIO UART CH4 context destroyed\n");
    }
}

// Setup PIO programs
static bool pio_uart_ch4_setup_programs(pio_uart_ch4_context_t* ctx) {
    if (!ctx || !ctx->pio_instance) {
        printf("ERROR: Invalid context for CH4 setup_programs\n");
        return false;
    }
    
    printf("PIO UART CH4: Setting up PIO programs on PIO2...\n");
    
    // Initialize offsets to invalid
    ctx->tx_offset = PIO_UART_CH4_PROGRAM_OFFSET_INVALID;
    ctx->rx_offset = PIO_UART_CH4_PROGRAM_OFFSET_INVALID;
    
    // Load TX program
    if (!pio_can_add_program(ctx->pio_instance, &pio_uart_tx_program)) {
        printf("ERROR: Cannot add PIO TX program to PIO2\n");
        return false;
    }
    
    ctx->tx_offset = pio_add_program(ctx->pio_instance, &pio_uart_tx_program);
    printf("PIO UART CH4: TX program loaded at offset %u\n", ctx->tx_offset);
    
    // Load RX program
    if (!pio_can_add_program(ctx->pio_instance, &pio_uart_rx_program)) {
        printf("ERROR: Cannot add PIO RX program to PIO2\n");
        pio_remove_program(ctx->pio_instance, &pio_uart_tx_program, ctx->tx_offset);
        ctx->tx_offset = PIO_UART_CH4_PROGRAM_OFFSET_INVALID;
        return false;
    }
    
    ctx->rx_offset = pio_add_program(ctx->pio_instance, &pio_uart_rx_program);
    printf("PIO UART CH4: RX program loaded at offset %u\n", ctx->rx_offset);
    
    return true;
}

static void pio_uart_ch4_cleanup_programs(pio_uart_ch4_context_t* ctx) {
    if (!ctx || !ctx->pio_instance) {
        return;
    }
    
    printf("PIO UART CH4: Cleaning up PIO programs...\n");
    
    // Disable state machines first
    if (ctx->tx_offset > PIO_UART_CH4_PROGRAM_OFFSET_INVALID) {
        pio_sm_set_enabled(ctx->pio_instance, ctx->tx_sm, false);
    }
    if (ctx->rx_offset > PIO_UART_CH4_PROGRAM_OFFSET_INVALID) {
        pio_sm_set_enabled(ctx->pio_instance, ctx->rx_sm, false);
    }
    
    // Remove programs
    if (ctx->tx_offset > PIO_UART_CH4_PROGRAM_OFFSET_INVALID) {
        pio_remove_program(ctx->pio_instance, &pio_uart_tx_program, ctx->tx_offset);
        ctx->tx_offset = PIO_UART_CH4_PROGRAM_OFFSET_INVALID;
    }
    if (ctx->rx_offset > PIO_UART_CH4_PROGRAM_OFFSET_INVALID) {
        pio_remove_program(ctx->pio_instance, &pio_uart_rx_program, ctx->rx_offset);
        ctx->rx_offset = PIO_UART_CH4_PROGRAM_OFFSET_INVALID;
    }
    
    printf("PIO UART CH4: PIO programs cleanup complete\n");
}

// Interface implementations
static bool pio_uart_ch4_init(void* context, const uart_config_t* config) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || ctx->state.initialized) {
        printf("ERROR: PIO UART CH4 already initialized or invalid context\n");
        return false;
    }
    
    printf("PIO UART CH4: Initializing - %u baud, TX:GPIO%u RX:GPIO%u (ctx: TX:GPIO%u RX:GPIO%u, PIO%d)\n", 
           config->baud_rate, config->tx_gpio, config->rx_gpio, ctx->tx_gpio, ctx->rx_gpio,
           ctx->pio_instance == pio0 ? 0 : (ctx->pio_instance == pio1 ? 1 : 2));
    
    // Store configuration
    ctx->current_config = *config;
    
    // Setup PIO programs
    printf("PIO UART CH4: Loading PIO programs...\n");
    if (!pio_uart_ch4_setup_programs(ctx)) {
        printf("ERROR: Failed to setup PIO programs for CH4\n");
        return false;
    }
    
    // Initialize PIO programs with configuration
    printf("PIO UART CH4: Initializing TX SM...\n");
    pio_uart_tx_program_init(ctx->pio_instance, ctx->tx_sm, ctx->tx_offset, 
                            config->tx_gpio, config->baud_rate);
    
    printf("PIO UART CH4: Initializing RX SM...\n");
    pio_uart_rx_program_init(ctx->pio_instance, ctx->rx_sm, ctx->rx_offset,
                            config->rx_gpio, config->baud_rate);
    
    // Setup PIO RX interrupt for data available (use PIO2 IRQ0)
    printf("PIO UART CH4: Setting up IRQ...\n");
    pio_set_irq0_source_enabled(ctx->pio_instance, 
                               pis_sm0_rx_fifo_not_empty + ctx->rx_sm, 
                               true);
    irq_set_exclusive_handler(PIO2_IRQ_0, pio_uart_ch4_pio_rx_irq_handler);
    irq_set_enabled(PIO2_IRQ_0, true);
    
    // Initialize state
    ctx->state.initialized = true;
    ctx->state.ready = true;
    ctx->state.uptime_ms = to_ms_since_boot(get_absolute_time());
    
    // Reset statistics
    pio_uart_ch4_reset_stats(context);
    
    printf("PIO UART CH4: Initialization complete\n");
    return true;
}

static void pio_uart_ch4_deinit(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return;
    }
    
    printf("PIO UART CH4: Deinitializing...\n");
    
    // Disable PIO interrupt
    irq_set_enabled(PIO2_IRQ_0, false);
    pio_set_irq0_source_enabled(ctx->pio_instance,
                               pis_sm0_rx_fifo_not_empty + ctx->rx_sm,
                               false);
    
    // Cleanup PIO programs  
    pio_uart_ch4_cleanup_programs(ctx);
    
    // Clear state
    ctx->state.initialized = false;
    ctx->state.ready = false;
    
    printf("PIO UART CH4: Deinitialization complete\n");
}

static bool pio_uart_ch4_is_ready(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    return ctx && ctx->state.initialized && ctx->state.ready;
}

static void pio_uart_ch4_send_byte(void* context, uint8_t byte) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return;
    }
    
    // Wait for FIFO space with timeout
    int timeout = 1000;
    while (pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm) && timeout-- > 0) {
        tight_loop_contents();
    }
    
    if (timeout > 0) {
        pio_sm_put(ctx->pio_instance, ctx->tx_sm, (uint32_t)byte);
        ctx->state.bytes_sent++;
    }
}

static size_t pio_uart_ch4_send_data(void* context, const uint8_t* data, size_t len) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized || !data || len == 0) {
        return 0;
    }
    
    // Send all bytes, waiting for FIFO space as needed (like send_byte does)
    size_t sent = 0;
    for (size_t i = 0; i < len; i++) {
        // Wait for FIFO space with timeout
        int timeout = 10000;  // ~10ms at tight loop speed
        while (pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm) && timeout-- > 0) {
            tight_loop_contents();
        }
        
        if (timeout > 0) {
            pio_sm_put(ctx->pio_instance, ctx->tx_sm, (uint32_t)data[i]);
            sent++;
            ctx->state.bytes_sent++;
        } else {
            // Timeout - FIFO stuck, abort
            printf("PIO CH4 TX: FIFO timeout at byte %zu/%zu\n", i, len);
            break;
        }
    }
    return sent;
}

static bool pio_uart_ch4_is_tx_ready(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return false;
    }
    return !pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm);
}

static bool pio_uart_ch4_is_tx_complete(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return true;
    }
    return pio_sm_is_tx_fifo_empty(ctx->pio_instance, ctx->tx_sm);
}

static bool pio_uart_ch4_has_rx_data(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return false;
    }
    return uart_receive_buffer_available(&ctx->rx_ring) > 0;
}

static uint8_t pio_uart_ch4_read_byte(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return 0;
    }
    
    uint8_t byte = 0;
    if (uart_receive_buffer_read(&ctx->rx_ring, &byte, 1) == 1) {
        ctx->state.bytes_received++;
        return byte;
    }
    return 0;
}

static size_t pio_uart_ch4_read_data(void* context, uint8_t* buffer, size_t max_len) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized || !buffer || max_len == 0) {
        return 0;
    }
    
    size_t read_count = uart_receive_buffer_read(&ctx->rx_ring, buffer, max_len);
    ctx->state.bytes_received += read_count;
    return read_count;
}

static size_t pio_uart_ch4_get_rx_count(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return 0;
    }
    return uart_receive_buffer_available(&ctx->rx_ring);
}

static void pio_uart_ch4_clear_rx_buffer(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return;
    }
    
    uart_receive_buffer_clear(&ctx->rx_ring);
    
    // Also clear any pending PIO RX FIFO data
    while (!pio_sm_is_rx_fifo_empty(ctx->pio_instance, ctx->rx_sm)) {
        (void)pio_sm_get(ctx->pio_instance, ctx->rx_sm);
    }
}

static const uart_state_t* pio_uart_ch4_get_state(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx) {
        return NULL;
    }
    ctx->state.uptime_ms = to_ms_since_boot(get_absolute_time());
    return &ctx->state;
}

static void pio_uart_ch4_reset_stats(void* context) {
    pio_uart_ch4_context_t* ctx = (pio_uart_ch4_context_t*)context;
    if (!ctx) {
        return;
    }
    
    ctx->state.bytes_sent = 0;
    ctx->state.bytes_received = 0;
    ctx->state.tx_errors = 0;
    ctx->state.rx_errors = 0;
    ctx->state.overrun_errors = 0;
    ctx->tx_dma_completions = 0;
    ctx->rx_dma_completions = 0;
    ctx->pio_errors = 0;
    ctx->dma_errors = 0;
    ctx->state.uptime_ms = to_ms_since_boot(get_absolute_time());
}

// PIO RX Interrupt handler for Channel 4 (PIO2)
// CRITICAL: This handler must be fast to prevent FIFO overflow.
// At 234375 baud, a byte arrives every ~42.7µs. With 8-deep FIFO,
// we have ~341µs before overflow. No printf() or blocking calls allowed.
static void pio_uart_ch4_pio_rx_irq_handler(void) {
    if (!pio_uart_ch4_context_initialized) {
        return;
    }
    
    pio_uart_ch4_context_t* ctx = &pio_uart_ch4_context;
    
    // Note: FIFO-not-empty interrupt is level-sensitive and auto-clears
    // when FIFO becomes empty. No manual clearing needed.
    
    // Process all available data from FIFO as fast as possible
    while (!pio_sm_is_rx_fifo_empty(ctx->pio_instance, ctx->rx_sm)) {
        uint32_t word = pio_sm_get(ctx->pio_instance, ctx->rx_sm);
        uint8_t byte = (uint8_t)(word >> 24);  // Extract byte from left-justified data
        
        // Put byte into ring buffer
        if (!uart_receive_buffer_put(&ctx->rx_ring, byte)) {
            ctx->state.overrun_errors++;
            // Continue draining FIFO even on overrun to prevent PIO stall
        }
    }
}
