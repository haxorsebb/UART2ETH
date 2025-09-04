/**
 * @file pio_uart_driver.c
 * @brief PIO UART driver implementation for Channel 2
 * 
 * Implements software UART functionality using RP2350 PIO0 state machines
 * with DMA acceleration and robust error handling.
 * 
 * Documentation Reference:
 * - Issue #82: PIO UART Driver Implementation
 * - ADR-013: PIO UART Implementation for Channel 2
 * - arc42 Chapter 5 - PIO UART Driver Implementation
 */

#include "uart/pio_uart_driver.h"

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

// Static single PIO UART instance context  
static pio_uart_context_t pio_uart_context;
static bool pio_uart_context_initialized = false;

// Forward declarations
static bool pio_uart_init(void* context, const uart_config_t* config);
static void pio_uart_deinit(void* context);
static bool pio_uart_is_ready(void* context);
static void pio_uart_send_byte(void* context, uint8_t byte);
static size_t pio_uart_send_data(void* context, const uint8_t* data, size_t len);
static bool pio_uart_is_tx_ready(void* context);
static bool pio_uart_is_tx_complete(void* context);
static bool pio_uart_has_rx_data(void* context);
static uint8_t pio_uart_read_byte(void* context);
static size_t pio_uart_read_data(void* context, uint8_t* buffer, size_t max_len);
static size_t pio_uart_get_rx_count(void* context);
static void pio_uart_clear_rx_buffer(void* context);
static const uart_state_t* pio_uart_get_state(void* context);
static void pio_uart_reset_stats(void* context);

// Internal helper functions
static void pio_uart_dma_tx_irq_handler(void);
static void pio_uart_dma_rx_irq_handler(void);
static void pio_uart_pio_rx_irq_handler(void);

// Interface table
const uart_interface_t pio_uart_interface = {
    .init = pio_uart_init,
    .deinit = pio_uart_deinit,
    .is_ready = pio_uart_is_ready,
    .send_byte = pio_uart_send_byte,
    .send_data = pio_uart_send_data,
    .is_tx_ready = pio_uart_is_tx_ready,
    .is_tx_complete = pio_uart_is_tx_complete,
    .has_rx_data = pio_uart_has_rx_data,
    .read_byte = pio_uart_read_byte,
    .read_data = pio_uart_read_data,
    .get_rx_count = pio_uart_get_rx_count,
    .clear_rx_buffer = pio_uart_clear_rx_buffer,
    .get_state = pio_uart_get_state,
    .reset_stats = pio_uart_reset_stats
};

// Context management
void* pio_create_context(uint8_t pio_num, uint8_t sm_num) {
    (void)pio_num;  // We use fixed PIO0
    (void)sm_num;   // We use fixed state machines
    
    if (pio_uart_context_initialized) {
        printf("ERROR: PIO UART context already initialized\n");
        return NULL;
    }
    
    memset(&pio_uart_context, 0, sizeof(pio_uart_context_t));
    
    // Configure PIO settings as per ADR-013
    pio_uart_context.pio_instance = PIO_UART_PIO_INSTANCE;
    pio_uart_context.tx_sm = PIO_UART_TX_SM;
    pio_uart_context.rx_sm = PIO_UART_RX_SM;
    pio_uart_context.tx_gpio = PIO_UART_TX_GPIO;
    pio_uart_context.rx_gpio = PIO_UART_RX_GPIO;
    
    // Initialize DMA channels as unclaimed
    pio_uart_context.tx_dma_chan = (uint)-1;
    pio_uart_context.rx_dma_chan = (uint)-1;
    pio_uart_context.dma_channels_claimed = false;
    
    // Initialize receive buffer
    uart_receive_buffer_init(&pio_uart_context.rx_ring, 
                           pio_uart_context.rx_buffer, 
                           PIO_UART_RX_BUFFER_SIZE);
    
    pio_uart_context_initialized = true;
    printf("PIO UART context created successfully\n");
    
    return &pio_uart_context;
}

void pio_destroy_context(void* context) {
    if (context == &pio_uart_context && pio_uart_context_initialized) {
        if (pio_uart_context.state.initialized) {
            pio_uart_deinit(context);
        }
        
        // Release DMA channels if they were claimed
        if (pio_uart_context.dma_channels_claimed) {
            if (pio_uart_context.tx_dma_chan != (uint)-1) {
                dma_channel_unclaim(pio_uart_context.tx_dma_chan);
            }
            if (pio_uart_context.rx_dma_chan != (uint)-1) {
                dma_channel_unclaim(pio_uart_context.rx_dma_chan);
            }
            pio_uart_context.dma_channels_claimed = false;
        }
        
        pio_uart_context_initialized = false;
        printf("PIO UART context destroyed\n");
    }
}

// Setup PIO programs
bool pio_uart_setup_programs(pio_uart_context_t* ctx) {
    if (!ctx || !ctx->pio_instance) {
        printf("ERROR: Invalid context or PIO instance provided to pio_uart_setup_programs\n");
        return false;
    }
    
    printf("PIO UART: Setting up PIO programs...\n");
    
    // Initialize offsets to invalid
    ctx->tx_offset = PIO_PROGRAM_OFFSET_INVALID;
    ctx->rx_offset = PIO_PROGRAM_OFFSET_INVALID;
    
    // Load TX program
    if (!pio_can_add_program(ctx->pio_instance, &pio_uart_tx_program)) {
        printf("ERROR: Cannot add PIO TX program - insufficient space\n");
        return false;
    }
    
    ctx->tx_offset = pio_add_program(ctx->pio_instance, &pio_uart_tx_program);
    printf("PIO UART: TX program loaded at offset %u\n", ctx->tx_offset);
    
    // Load RX program
    if (!pio_can_add_program(ctx->pio_instance, &pio_uart_rx_program)) {
        printf("ERROR: Cannot add PIO RX program - insufficient space\n");
        // Clean up TX program on failure
        pio_remove_program(ctx->pio_instance, &pio_uart_tx_program, ctx->tx_offset);
        ctx->tx_offset = PIO_PROGRAM_OFFSET_INVALID;
        return false;
    }
    
    ctx->rx_offset = pio_add_program(ctx->pio_instance, &pio_uart_rx_program);
    printf("PIO UART: RX program loaded at offset %u\n", ctx->rx_offset);
    
    return true;
}

void pio_uart_cleanup_programs(pio_uart_context_t* ctx) {
    if (!ctx) {
        printf("WARNING: NULL context provided to pio_uart_cleanup_programs\n");
        return;
    }

    if (!ctx->pio_instance) {
        printf("WARNING: Invalid PIO instance in cleanup\n");
        return;
    }
    
    printf("PIO UART: Cleaning up PIO programs...\n");
    
    // Disable state machines first
    if (ctx->tx_offset > PIO_PROGRAM_OFFSET_INVALID) {
        pio_sm_set_enabled(ctx->pio_instance, ctx->tx_sm, false);
    }
    if (ctx->rx_offset > PIO_PROGRAM_OFFSET_INVALID) {
        pio_sm_set_enabled(ctx->pio_instance, ctx->rx_sm, false);
    }
    
    // Remove programs
    if (ctx->tx_offset > PIO_PROGRAM_OFFSET_INVALID) {
        pio_remove_program(ctx->pio_instance, &pio_uart_tx_program, ctx->tx_offset);
        ctx->tx_offset = PIO_PROGRAM_OFFSET_INVALID;
    }
    if (ctx->rx_offset > PIO_PROGRAM_OFFSET_INVALID) {
        pio_remove_program(ctx->pio_instance, &pio_uart_rx_program, ctx->rx_offset);
        ctx->rx_offset = PIO_PROGRAM_OFFSET_INVALID;
    }
    
    printf("PIO UART: PIO programs cleanup complete\n");
}

// Setup DMA channels
bool pio_uart_setup_dma(pio_uart_context_t* ctx) {
    if (!ctx) {
        printf("ERROR: NULL context provided to pio_uart_setup_dma\n");
        return false;
    }
    
    printf("PIO UART: Setting up DMA channels...\n");

    // Initialize DMA channels to invalid
    ctx->tx_dma_chan = PIO_UART_INVALID_DMA_CHANNEL;
    ctx->rx_dma_chan = PIO_UART_INVALID_DMA_CHANNEL;
    ctx->dma_channels_claimed = false;

    // Claim TX DMA channel
    ctx->tx_dma_chan = dma_claim_unused_channel(false);
    if (ctx->tx_dma_chan == PIO_UART_INVALID_DMA_CHANNEL) {
        printf("ERROR: Failed to claim TX DMA channel\n");
        return false;
    }
    
    // Claim RX DMA channel
    ctx->rx_dma_chan = dma_claim_unused_channel(false);
    if (ctx->rx_dma_chan == PIO_UART_INVALID_DMA_CHANNEL) {
        printf("ERROR: Failed to claim RX DMA channel\n");
        dma_channel_unclaim(ctx->tx_dma_chan);
        ctx->tx_dma_chan = PIO_UART_INVALID_DMA_CHANNEL;
        return false;
    }
    
    ctx->dma_channels_claimed = true;
    
    // Configure TX DMA channel
    dma_channel_config tx_config = dma_channel_get_default_config(ctx->tx_dma_chan);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_dreq(&tx_config, pio_get_dreq(ctx->pio_instance, ctx->tx_sm, true));
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    
    dma_channel_configure(
        ctx->tx_dma_chan,
        &tx_config,
        &ctx->pio_instance->txf[ctx->tx_sm],  // Write to TX FIFO
        NULL,  // Read address set later
        0,     // Transfer count set later
        false  // Don't start yet
    );
    
    // Configure RX DMA channel
    dma_channel_config rx_config = dma_channel_get_default_config(ctx->rx_dma_chan);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_dreq(&rx_config, pio_get_dreq(ctx->pio_instance, ctx->rx_sm, false));
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    
    dma_channel_configure(
        ctx->rx_dma_chan,
        &rx_config,
        ctx->rx_buffer,                       // Destination buffer
        &ctx->pio_instance->rxf[ctx->rx_sm],  // Read from RX FIFO
        PIO_UART_RX_BUFFER_SIZE,              // Transfer count
        false  // Don't start yet
    );
    
    // Set up DMA interrupts
    dma_channel_set_irq0_enabled(ctx->tx_dma_chan, true);
    dma_channel_set_irq1_enabled(ctx->rx_dma_chan, true);
    
    irq_set_exclusive_handler(DMA_IRQ_0, pio_uart_dma_tx_irq_handler);
    irq_set_exclusive_handler(DMA_IRQ_1, pio_uart_dma_rx_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    irq_set_enabled(DMA_IRQ_1, true);
    
    printf("PIO UART: DMA setup complete - TX:%u RX:%u\n", 
           ctx->tx_dma_chan, ctx->rx_dma_chan);
    
    return true;
}

void pio_uart_cleanup_dma(pio_uart_context_t* ctx) {
    if (!ctx->dma_channels_claimed) {
        return;
    }
    
    printf("PIO UART: Cleaning up DMA channels...\n");
    
    // Disable DMA interrupts
    irq_set_enabled(DMA_IRQ_0, false);
    irq_set_enabled(DMA_IRQ_1, false);
    dma_channel_set_irq0_enabled(ctx->tx_dma_chan, false);
    dma_channel_set_irq1_enabled(ctx->rx_dma_chan, false);
    
    // Abort any ongoing transfers
    dma_channel_abort(ctx->tx_dma_chan);
    dma_channel_abort(ctx->rx_dma_chan);
    
    // Unclaim channels
    dma_channel_unclaim(ctx->tx_dma_chan);
    dma_channel_unclaim(ctx->rx_dma_chan);
    
    ctx->dma_channels_claimed = false;
    
    printf("PIO UART: DMA cleanup complete\n");
}

// Interface implementations
static bool pio_uart_init(void* context, const uart_config_t* config) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || ctx->state.initialized) {
        printf("ERROR: PIO UART already initialized or invalid context\n");
        return false;
    }
    
    printf("PIO UART: Initializing - %u baud, TX:%u RX:%u\n", 
           config->baud_rate, config->tx_gpio, config->rx_gpio);
    
    // Store configuration
    ctx->current_config = *config;
    
    // Setup PIO programs
    if (!pio_uart_setup_programs(ctx)) {
        printf("ERROR: Failed to setup PIO programs\n");
        return false;
    }
    
    // Setup DMA channels
    if (!pio_uart_setup_dma(ctx)) {
        printf("ERROR: Failed to setup DMA\n");
        pio_uart_cleanup_programs(ctx);
        return false;
    }
    
    // Initialize PIO programs with configuration
    pio_uart_tx_program_init(ctx->pio_instance, ctx->tx_sm, ctx->tx_offset, 
                            config->tx_gpio, config->baud_rate);
    
    pio_uart_rx_program_init(ctx->pio_instance, ctx->rx_sm, ctx->rx_offset,
                            config->rx_gpio, config->baud_rate);
    
    // Setup PIO RX interrupt for data available  
    pio_set_irq0_source_enabled(ctx->pio_instance, 
                               pis_sm0_rx_fifo_not_empty + ctx->rx_sm, 
                               true);
    irq_set_exclusive_handler(pio_get_irq_num(ctx->pio_instance, 0), 
                             pio_uart_pio_rx_irq_handler);
    irq_set_enabled(pio_get_irq_num(ctx->pio_instance, 0), true);
    
    // Initialize state
    ctx->state.initialized = true;
    ctx->state.ready = true;
    ctx->state.uptime_ms = to_ms_since_boot(get_absolute_time());
    
    // Reset statistics
    pio_uart_reset_stats(context);
    
    printf("PIO UART: Initialization complete\n");
    return true;
}

static void pio_uart_deinit(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return;
    }
    
    printf("PIO UART: Deinitializing...\n");
    
    // Disable PIO interrupt
    irq_set_enabled(pio_get_irq_num(ctx->pio_instance, 0), false);
    pio_set_irq0_source_enabled(ctx->pio_instance,
                               pis_sm0_rx_fifo_not_empty + ctx->rx_sm,
                               false);
    
    // Cleanup DMA
    pio_uart_cleanup_dma(ctx);
    
    // Cleanup PIO programs  
    pio_uart_cleanup_programs(ctx);
    
    // Clear state
    ctx->state.initialized = false;
    ctx->state.ready = false;
    
    printf("PIO UART: Deinitialization complete\n");
}

static bool pio_uart_is_ready(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    return ctx && ctx->state.initialized && ctx->state.ready;
}

static void pio_uart_send_byte(void* context, uint8_t byte) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return;
    }
    
    // Send single byte directly to PIO FIFO  
    printf("PIO TX: Sending byte 0x%02X ('%c')\n", byte, (byte >= 32 && byte < 127) ? byte : '?');
    
    // Check if FIFO is full before sending
    if (pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm)) {
        printf("PIO TX: FIFO is FULL - cannot send\n");
        return;
    }
    
    printf("PIO TX: FIFO ready, sending...\n");
    pio_sm_put(ctx->pio_instance, ctx->tx_sm, (uint32_t)byte);
    printf("PIO TX: Byte sent to FIFO\n");
    
    ctx->state.bytes_sent++;
    // Direct PIO send is complete immediately
    ctx->tx_in_progress = false;
}

static size_t pio_uart_send_data(void* context, const uint8_t* data, size_t len) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized || !data || len == 0) {
        return 0;
    }
    
    // For large transfers, use DMA  
    if (len > 16 && !ctx->tx_in_progress) {
        // Copy to DMA buffer (limit to buffer size)
        size_t transfer_len = len > PIO_UART_DMA_BUFFER_SIZE ? PIO_UART_DMA_BUFFER_SIZE : len;
        memcpy(ctx->tx_dma_buffer, data, transfer_len);
        
        printf("PIO TX: Using DMA for %zu bytes (requested %zu)\n", transfer_len, len);
        
        // Configure and start DMA transfer
        dma_channel_set_trans_count(ctx->tx_dma_chan, transfer_len, false);
        dma_channel_set_read_addr(ctx->tx_dma_chan, ctx->tx_dma_buffer, true);
        
        ctx->tx_in_progress = true;
        ctx->state.bytes_sent += transfer_len;
        
        return transfer_len;
    } else {
        // Use direct PIO FIFO for small transfers or if DMA busy
        printf("PIO TX: Using FIFO for %zu bytes (DMA busy=%s)\n", len, ctx->tx_in_progress ? "yes" : "no");
        size_t sent = 0;
        for (size_t i = 0; i < len; i++) {
            if (!pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm)) {
                pio_sm_put(ctx->pio_instance, ctx->tx_sm, (uint32_t)data[i]);
                sent++;
                ctx->state.bytes_sent++;
            } else {
                break;  // FIFO full, return partial send
            }
        }
        printf("PIO TX: FIFO sent %zu/%zu bytes\n", sent, len);
        return sent;
    }
}

static bool pio_uart_is_tx_ready(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return false;
    }
    
    return !ctx->tx_in_progress && 
           !pio_sm_is_tx_fifo_full(ctx->pio_instance, ctx->tx_sm);
}

static bool pio_uart_is_tx_complete(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        printf("PIO TX Complete: context invalid/uninitialized\n");
        return true;
    }
    
    bool tx_not_in_progress = !ctx->tx_in_progress;
    bool fifo_empty = pio_sm_is_tx_fifo_empty(ctx->pio_instance, ctx->tx_sm);
    bool complete = tx_not_in_progress && fifo_empty;
    
    printf("PIO TX Complete check: tx_in_progress=%s, fifo_empty=%s, complete=%s\n",
           ctx->tx_in_progress ? "true" : "false",
           fifo_empty ? "true" : "false", 
           complete ? "true" : "false");
    
    return complete;
}

static bool pio_uart_has_rx_data(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return false;
    }
    
    return uart_receive_buffer_available(&ctx->rx_ring) > 0;
}

static uint8_t pio_uart_read_byte(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
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

static size_t pio_uart_read_data(void* context, uint8_t* buffer, size_t max_len) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized || !buffer || max_len == 0) {
        return 0;
    }
    
    size_t read_count = uart_receive_buffer_read(&ctx->rx_ring, buffer, max_len);
    ctx->state.bytes_received += read_count;
    
    return read_count;
}

static size_t pio_uart_get_rx_count(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return 0;
    }
    
    return uart_receive_buffer_available(&ctx->rx_ring);
}

static void pio_uart_clear_rx_buffer(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx || !ctx->state.initialized) {
        return;
    }
    
    uart_receive_buffer_clear(&ctx->rx_ring);
    
    // Also clear any pending PIO RX FIFO data
    while (!pio_sm_is_rx_fifo_empty(ctx->pio_instance, ctx->rx_sm)) {
        (void)pio_sm_get(ctx->pio_instance, ctx->rx_sm);
    }
}

static const uart_state_t* pio_uart_get_state(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
    if (!ctx) {
        return NULL;
    }
    
    // Update uptime
    ctx->state.uptime_ms = to_ms_since_boot(get_absolute_time());
    
    return &ctx->state;
}

static void pio_uart_reset_stats(void* context) {
    pio_uart_context_t* ctx = (pio_uart_context_t*)context;
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

// Interrupt handlers
static void pio_uart_dma_tx_irq_handler(void) {
    if (pio_uart_context_initialized && 
        dma_channel_get_irq0_status(pio_uart_context.tx_dma_chan)) {
        
        dma_channel_acknowledge_irq0(pio_uart_context.tx_dma_chan);
        pio_uart_context.tx_in_progress = false;
        pio_uart_context.tx_dma_completions++;
    }
}

static void pio_uart_dma_rx_irq_handler(void) {
    if (pio_uart_context_initialized &&
        dma_channel_get_irq0_status(pio_uart_context.rx_dma_chan)) {
        
        dma_channel_acknowledge_irq0(pio_uart_context.rx_dma_chan);
        pio_uart_context.rx_dma_completions++;
        
        // Process received data from DMA buffer to ring buffer
        // Implementation depends on specific DMA usage pattern
    }
}

static void pio_uart_pio_rx_irq_handler(void) {
    if (!pio_uart_context_initialized) {
        return;
    }
    
    pio_uart_context_t* ctx = &pio_uart_context;
    
    // Clear interrupt first to prevent runaway condition
    pio_interrupt_clear(ctx->pio_instance, pis_sm0_rx_fifo_not_empty + ctx->rx_sm);
    
    // Process available data from FIFO
    int max_bytes = 32;  // Prevent unbounded interrupt processing
    
    while (!pio_sm_is_rx_fifo_empty(ctx->pio_instance, ctx->rx_sm) && max_bytes-- > 0) {
        uint32_t word = pio_sm_get(ctx->pio_instance, ctx->rx_sm);
        uint8_t byte = (uint8_t)(word >> 24);  // Extract byte from left-justified data
        
        // Put byte into ring buffer (interrupt-safe)
        bool success = uart_receive_buffer_put(&ctx->rx_ring, byte);
        
        if (!success) {
            ctx->state.overrun_errors++;
            break;  // Buffer full, stop processing
        }
    }
}

// DMA handlers for external access
void pio_uart_tx_dma_handler(pio_uart_context_t* ctx) {
    ctx->tx_in_progress = false;
    ctx->tx_dma_completions++;
}

void pio_uart_rx_dma_handler(pio_uart_context_t* ctx) {
    ctx->rx_dma_completions++;
}

void pio_uart_rx_pio_handler(pio_uart_context_t* ctx) {
    // This function could be used for custom RX processing if needed
    (void)ctx;  // Suppress unused parameter warning
}