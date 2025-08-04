/**
 * @file main.c
 * @brief Main entry point for UART2ETH firmware
 * 
 * Implements dual-core startup sequence and main system loop
 * as documented in arc42 runtime view.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "shared_memory.h"

int main() {
    // Initialize Pico SDK
    stdio_init_all();
    
    // Wait for USB-serial connection
    sleep_ms(2000);
    
    printf("UART2ETH Firmware Starting...\n");
    
    // Initialize shared memory
    if (!shared_memory_init()) {
        printf("ERROR: Failed to initialize shared memory\n");
        return -1;
    }
    
    printf("Shared memory initialized successfully\n");
    
    // Main loop
    while (true) {
        printf("System running...\n");
        sleep_ms(5000);
    }
    
    return 0;
}
