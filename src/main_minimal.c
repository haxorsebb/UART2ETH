/**
 * @file main_minimal.c  
 * @brief Minimal test to isolate crt0.S flash initialization issue
 * 
 * This eliminates all complex initialization to test if the issue is
 * related to XOSC configuration, memory layout, or basic RP2350 functionality.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

/**
 * Absolute minimal main function for debugging crt0.S issues
 */
int main() {
    // Minimal stdio initialization
    stdio_init_all();
    
    // Brief delay for USB enumeration
    sleep_ms(2000);
    
    printf("MINIMAL TEST: RP2350 basic functionality\n");
    printf("If you see this message, crt0.S flash init worked!\n");
    
    // Initialize LED 
    const uint LED_PIN = 25;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    printf("Starting LED blink test...\n");
    
    // Simple blink loop
    uint32_t counter = 0;
    while (true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(250);
        gpio_put(LED_PIN, 0);
        sleep_ms(250);
        
        counter++;
        if (counter % 10 == 0) {
            printf("LED blink count: %u\n", counter);
        }
        
        // Test different clock frequencies
        if (counter == 50) {
            printf("Clock test: System running at %u Hz\n", clock_get_hz(clk_sys));
        }
    }
    
    return 0;
}
