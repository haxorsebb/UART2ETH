/**
 * @file test_ringbuffer_minimal.c
 * @brief Minimal ring buffer test to isolate initialization issues
 */

#include "pico/stdlib.h"
#include <stdio.h>

// Test without including our ring buffer initially
int main(void) {
    // Initialize pico SDK
    stdio_init_all();
    
    // Give time for USB-CDC to initialize
    sleep_ms(2000);
    
    printf("=== MINIMAL RING BUFFER TEST ===\n");
    printf("Starting basic initialization test...\n");
    
    // Test basic functionality step by step
    printf("Step 1: Basic printf working\n");
    
    printf("Step 2: Sleep test\n");
    sleep_ms(1000);
    printf("Step 2: Sleep completed\n");
    
    printf("Step 3: Testing include\n");
    #include "ringbuffer.h"
    printf("Step 3: Ring buffer header included successfully\n");
    
    printf("Step 4: Testing ring buffer init\n");
    bool init_result = ringbuffer_init();
    printf("Step 4: Ring buffer init result: %s\n", init_result ? "SUCCESS" : "FAILED");
    
    if (init_result) {
        printf("Step 5: Testing capacity check\n");
        uint32_t capacity = ringbuffer_get_capacity();
        printf("Step 5: Ring buffer capacity: %u\n", capacity);
        
        printf("Step 6: Testing free count\n");
        uint32_t free_count = ringbuffer_get_free_count();
        printf("Step 6: Free entries: %u\n", free_count);
    }
    
    printf("=== MINIMAL TEST COMPLETED ===\n");
    
    // Keep running
    uint32_t counter = 0;
    while (true) {
        printf("Minimal test running... counter: %u\n", counter++);
        sleep_ms(3000);
    }
}