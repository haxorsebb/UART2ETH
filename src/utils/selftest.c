#include "utils/selftest.h"
#include "config/device_mode.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "network/enc28j60_driver.h"
#include "pio_uart_tx.pio.h"
#include "pio_uart_rx.pio.h"
#include <stdio.h>
#include <string.h>

// Selftest baud rate
#define SELFTEST_BAUD_RATE 234375

// PIO instance and state machine for selftest TX/RX
// Must use PIO2 to match Channel 4's PIO instance, otherwise GPIO5 gets
// reassigned when uart_manager_init() initializes Channel 4's PIO UART.
#define SELFTEST_PIO        pio2
#define SELFTEST_TX_SM      0
#define SELFTEST_RX_SM      1

// Track if PIO TX/RX is initialized
static bool g_selftest_pio_initialized = false;
static uint g_selftest_tx_offset = 0;
static uint g_selftest_rx_offset = 0;

// Expected trigger string from Blackfin
#define SELFTEST_TRIGGER_STRING "RP2354 run\r\n"

//define PRINT_SELFTEST_OUTPUT for selftest function in combination with boardtest software on the Blackfin DSP
#define PRINT_SELFTEST_OUTPUT

/**
 * @brief Send a string over PIO UART4 for selftest output
 * 
 * Sends each character using the PIO UART TX program.
 * Also echoes to the debug UART (printf).
 * 
 * @param str Null-terminated string to send
 */
void selftest_puts(const char* str)
{
#ifdef PRINT_SELFTEST_OUTPUT    
    while (*str) {
        printf("%c", *str);
        if (g_selftest_pio_initialized) {
            pio_uart_tx_program_putc(SELFTEST_PIO, SELFTEST_TX_SM, *str);
        }
        str++;
    }
#endif    
}

void selftest(void)
{
    // Initialize PIO UART4 TX for selftest output (uses GPIO5 due to board wiring swap)
    // This replaces the previous HW UART1 initialization
    
    // Load and initialize the PIO TX program
    g_selftest_tx_offset = pio_add_program(SELFTEST_PIO, &pio_uart_tx_program);
    pio_uart_tx_program_init(SELFTEST_PIO, SELFTEST_TX_SM, g_selftest_tx_offset, 
                              DEVICE_UART4_TX_GPIO, SELFTEST_BAUD_RATE);
    
    // Load and initialize the PIO RX program
    g_selftest_rx_offset = pio_add_program(SELFTEST_PIO, &pio_uart_rx_program);
    pio_uart_rx_program_init(SELFTEST_PIO, SELFTEST_RX_SM, g_selftest_rx_offset,
                              DEVICE_UART4_RX_GPIO, SELFTEST_BAUD_RATE);
    
    g_selftest_pio_initialized = true;

    // Wait until the Blackfin sends "RP2354 run\r\n"
    // This synchronizes the selftest with the Blackfin boardtest software.
    // The software will wait here forever until the trigger is received.
    {
        const char* trigger = SELFTEST_TRIGGER_STRING;
        size_t trigger_len = strlen(trigger);
        size_t match_pos = 0;
        
        printf("Selftest: Waiting for trigger 'RP2354 run\\r\\n'...\n");
        
        while (match_pos < trigger_len) {
            // Check if RX FIFO has data
            if (!pio_sm_is_rx_fifo_empty(SELFTEST_PIO, SELFTEST_RX_SM)) {
                // Read byte from PIO RX FIFO (data is left-justified, take upper byte)
                uint32_t word = pio_sm_get(SELFTEST_PIO, SELFTEST_RX_SM);
                char c = (char)(word >> 24);
                
                // Check if character matches expected position in trigger string
                if (c == trigger[match_pos]) {
                    match_pos++;
                } else {
                    // Mismatch - reset matching
                    match_pos = 0;
                    // Check if this char matches start of trigger
                    if (c == trigger[0]) {
                        match_pos = 1;
                    }
                }
            } else {
                // No data - small delay to avoid busy-waiting
                sleep_us(100);
            }
        }
        printf("Selftest: Trigger received, starting selftest\n");
    }
    
    // Send selftest message
    selftest_puts("RP2354 SELFTEST: V0.90 START\r\n");
    
    // Note: The PIO UART will be reconfigured later by uart_manager_init() if UART4 is used.
    // After uart_manager_init(), GPIO5 will be reassigned to PIO2, and selftest_puts()
    // using PIO0 will no longer work. All selftest messages must be sent before that point.

    // Safety: Configure all potential UART RX pins
    // and all SPI MISO pins with internal pullup.
    // Currently there are no later function calls that disable the pullup
    gpio_pull_up(17); //HW UART0 RX
    gpio_pull_up(DEVICE_UART1_RX_GPIO); //HW UART1 RX (now GPIO25)
    gpio_pull_up(DEVICE_UART2_RX_GPIO); //PIO UART2 RX
    gpio_pull_up(DEVICE_UART3_RX_GPIO); //PIO UART3 RX
    gpio_pull_up(DEVICE_UART4_RX_GPIO); //PIO UART4 RX (now GPIO4)

    gpio_pull_up(ENC28J60_MISO_PIN); //SPI1 MISO
    gpio_pull_up(16); //SPI0 MISO is not yet defined in the software, will later be GPIO16.
                           //Currently GPIO16 is used as HW UART 0 TX, not affected by the activated pullup

    // The other selftest outputs are spread around the software, mainly in enc28j60_init()
}
