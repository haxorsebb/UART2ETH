/**
 * @file XOSC_INTEGRATION_EXAMPLE.c
 * @brief Example of how to integrate XOSC testing into your main.c
 * 
 * Add these code snippets to your main.c file to test the 12MHz XOSC signal
 */

// ADD THIS INCLUDE at the top of your main.c:
#include "xosc_test.h"

// ADD THIS FUNCTION CALL in your main() function, after stdio initialization:
int main() {
    // Initialize dual stdio like production system
    stdio_usb_init();
    stdio_uart_init_full(uart0, 115200, 0, 1);
    
    // Wait for USB-serial connection for debugging
    sleep_ms(2000);
    
    printf("UART2ETH COPYRIGHT 2025 CASSEL MESSTECHNIK GMBH\n");
    
    // *** ADD XOSC TEST HERE ***
    printf("=== TESTING 12MHz XOSC SIGNAL ===\n");
    test_xosc_signal();
    
    // Optional: Output XOSC to GPIO 22 for oscilloscope measurement
    // Uncomment the next line if you want to measure with scope:
    // output_xosc_for_measurement(22);
    
    printf("=== XOSC TEST COMPLETE ===\n");
    printf("Press any key to continue with normal startup...\n");
    getchar();  // Wait for user input before continuing
    
    // Continue with your normal initialization...
    printf("DEBUG: About to initialize shared memory...\n");
    // ... rest of your existing code
}

// OPTIONAL: Add periodic XOSC monitoring in your main loop:
void periodic_xosc_check(void) {
    static uint32_t last_check = 0;
    uint32_t now = time_us_32();
    
    // Check XOSC every 10 seconds
    if (now - last_check > 10000000) {
        if (!is_xosc_working()) {
            printf("WARNING: XOSC problem detected!\n");
            xosc_diagnostic_check();
        }
        last_check = now;
    }
}