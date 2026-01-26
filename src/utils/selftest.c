#include "utils/selftest.h"
#include "config/device_mode.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "network/enc28j60_driver.h"
#include <stdio.h>

// Selftest baud rate
#define SELFTEST_BAUD_RATE 230400

//define PRINT_SELFTEST_OUTPUT for selftest function in combination with boardtest software on the Blackfin DSP
#define PRINT_SELFTEST_OUTPUT

/**
 * @brief Send a string over UART1 for selftest output
 * 
 * Sends each character directly to the UART TX FIFO, waiting for
 * space in the FIFO before each character. Waits for transmission
 * to complete before returning.
 * 
 * @param str Null-terminated string to send
 */
void selftest_puts(const char* str)
{
#ifdef PRINT_SELFTEST_OUTPUT    
    while (*str) {
        uart_putc_raw(uart1, *str++);
    }
    // Wait for transmission to complete
    uart_tx_wait_blocking(uart1);
#endif    
}

void selftest(void)
{
    // The diag uart 0 is already configured. Now configure uart 1 that goes to the Blackfin processor.
    // Initialize UART1 GPIO pins
    // GPIO 4/5 use GPIO_FUNC_UART (function 2) for UART1 TX/RX
    gpio_set_function(DEVICE_UART1_TX_GPIO, UART_FUNCSEL_NUM(uart1, DEVICE_UART1_TX_GPIO));
    gpio_set_function(DEVICE_UART1_RX_GPIO, UART_FUNCSEL_NUM(uart1, DEVICE_UART1_RX_GPIO));
    
    // Initialize UART1 hardware
    uart_init(uart1, SELFTEST_BAUD_RATE);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    
    // Send selftest message
    selftest_puts("RP2354 SELFTEST: START\r\n");
    
    //Note: UART1 will be reconfigured later by uart_manager_init().
    //The SDK's uart_init() can be called multiple times safely

    //Safety: Configure all potential UART RX pins
    //and all SPI MISO pins
    //with internal pullup.
    //Currently there are no later function calls that disable the pullup
    gpio_pull_up(17); //HW UART0 RX
    gpio_pull_up(DEVICE_UART1_RX_GPIO); //HW UART1 RX
    gpio_pull_up(DEVICE_UART2_RX_GPIO); //PIO UART2 RX
    gpio_pull_up(DEVICE_UART3_RX_GPIO); //PIO UART3 RX
    gpio_pull_up(DEVICE_UART4_RX_GPIO); //PIO UART4 RX

    gpio_pull_up(ENC28J60_MISO_PIN); //SPI1 MISO
    gpio_pull_up(16); //SPI0 MISO is not yet defined in the software, will later be GPIO16.
                           //Currently GPIO16 is used as HW UART 0 TX, not affected by the activated pullup




    //The other selftest outputs are spread around the software, mainly in enc28j60_init()

}
