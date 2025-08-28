/**
 * @file test_pio_interrupt.c
 * @brief Test PIO RX interrupt triggering
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#include "pio_uart_tx.pio.h"
#include "pio_uart_rx.pio.h"

#define TX_GPIO 14
#define RX_GPIO 15
#define BAUD_RATE 230400

static volatile int interrupt_count = 0;
static volatile bool data_received = false;
static volatile uint8_t received_byte = 0;

void pio_irq_handler() {
    interrupt_count++;
    printf("PIO IRQ triggered! Count: %d\n", interrupt_count);
    
    PIO pio = pio0;
    uint rx_sm = 1;
    
    // Check if RX SM triggered interrupt
    if (pio_interrupt_get(pio, pis_sm0_rx_fifo_not_empty + rx_sm)) {
        printf("RX SM interrupt confirmed\n");
        
        if (!pio_sm_is_rx_fifo_empty(pio, rx_sm)) {
            uint32_t word = pio_sm_get(pio, rx_sm);
            received_byte = (uint8_t)(word >> 24);  // Left-justified
            data_received = true;
            printf("Received: 0x%02X ('%c')\n", received_byte, 
                   (received_byte >= 32 && received_byte < 127) ? received_byte : '?');
        }
        
        // Clear interrupt
        pio_interrupt_clear(pio, pis_sm0_rx_fifo_not_empty + rx_sm);
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("=== PIO Interrupt Test ===\n");
    
    PIO pio = pio0;
    uint tx_sm = 0;
    uint rx_sm = 1;
    
    // Load programs
    uint tx_offset = pio_add_program(pio, &pio_uart_tx_program);
    uint rx_offset = pio_add_program(pio, &pio_uart_rx_program);
    
    printf("Programs loaded: TX %u, RX %u\n", tx_offset, rx_offset);
    
    // Initialize
    pio_uart_tx_program_init(pio, tx_sm, tx_offset, TX_GPIO, BAUD_RATE);
    pio_uart_rx_program_init(pio, rx_sm, rx_offset, RX_GPIO, BAUD_RATE);
    
    // Setup RX interrupt
    pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_sm, true);
    irq_set_exclusive_handler(pio_get_irq_num(pio, 0), pio_irq_handler);
    irq_set_enabled(pio_get_irq_num(pio, 0), true);
    
    printf("Interrupt configured\n");
    sleep_ms(100);
    
    // Send test byte
    char test_byte = 'T';
    printf("Sending: 0x%02X ('%c')\n", test_byte, test_byte);
    pio_uart_tx_program_putc(pio, tx_sm, test_byte);
    
    // Wait for interrupt
    int timeout = 1000;
    while (!data_received && timeout-- > 0) {
        sleep_ms(1);
    }
    
    printf("Final status:\n");
    printf("- Interrupt count: %d\n", interrupt_count);
    printf("- Data received: %s\n", data_received ? "Yes" : "No");
    if (data_received) {
        printf("- Received byte: 0x%02X ('%c')\n", received_byte, received_byte);
        printf("- Match: %s\n", (received_byte == test_byte) ? "YES" : "NO");
    }
    
    if (interrupt_count == 0) {
        printf("ERROR: No interrupts triggered!\n");
    } else if (!data_received) {
        printf("ERROR: Interrupt triggered but no data!\n");
    } else if (received_byte != test_byte) {
        printf("ERROR: Data mismatch!\n");
    } else {
        printf("SUCCESS: Interrupt-based RX working!\n");
    }
    
    while (true) {
        sleep_ms(1000);
    }
    
    return 0;
}
