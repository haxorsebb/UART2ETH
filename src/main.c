/**
 * @file main.c
 * @brief Main entry point for UART2ETH firmware with dual-core launch
 * 
 * Implements dual-core startup sequence with event-driven state machine
 * coordination as documented in arc42 runtime view.
 * 
 * Architecture:
 * - Core0: UART processing with event-driven state machine
 * - Core1: Network, persistence, and log processing
 * - State machine: Three independent event-driven state machines
 * 
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Building Block View
 *
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "shared_memory.h"
#include "state_machine.h"
#include "log_manager.h"
#include "ringbuffer.h"
#include "network/enc28j60_driver.h"
#include "debug.h"

// Forward declarations for core main functions

void core0_main(void);
void core1_main(void);

/**
 * Core1 entry point function
 * 
 * This function is called when Core1 is launched. It performs
 * Core1-specific initialization and then calls the main Core1 loop.
 */
// Minimal Core1 entry point for debugging hard fault
void core1_entry() {
    // ABSOLUTE MINIMAL - no printf, no shared memory, no complex operations
    
    // Simple infinite loop with basic operations to test Core1 viability
    volatile uint32_t core1_alive_counter = 0;
    volatile bool core1_basic_test_passed = false;
    
    // Test 1: Basic arithmetic and memory access
    for (volatile int i = 0; i < 1000; i++) {
        core1_alive_counter++;
    }
    
    // Test 2: Simple GPIO toggle for oscilloscope (GPIO 21)
    // Note: This might conflict with Core0, but it's a simple test
    gpio_init(21);
    gpio_set_dir(21, GPIO_OUT);
    
    // Test 3: Basic timer access  
    absolute_time_t start_time = get_absolute_time();
    (void)start_time;  // Suppress unused variable warning
    
    // If we reach here, basic Core1 functionality works
    core1_basic_test_passed = true;
    
    // Simple toggle pattern for oscilloscope to indicate Core1 is alive
    while (true) {
        gpio_put(21, 1);
        busy_wait_us(100000);  // 100ms - using busy wait instead of sleep_ms
        gpio_put(21, 0);
        busy_wait_us(100000);  // 100ms
        
        core1_alive_counter++;
        
        // After basic tests pass, try to enable printf
        if (core1_basic_test_passed && (core1_alive_counter % 10 == 0)) {
            // Try printf only after basic operations prove stable
            printf("DEBUG: Core1 alive counter: %u\n", core1_alive_counter);
            
            // If printf works, try more complex operations
            if (core1_alive_counter > 50) {
                printf("DEBUG: Core1 basic tests passed, attempting core1_main()\n");
                break;  // Exit to try core1_main()
            }
        }
    }
    
    // Only call core1_main() if basic tests pass
    if (core1_basic_test_passed) {
        core1_main();
    }
    
    // Should never reach here
    while (true) {
        gpio_put(21, 1);
        busy_wait_us(1000000);  // 1 second
        gpio_put(21, 0);
        busy_wait_us(1000000);  // 1 second - error pattern: slow toggle
    }
}

/**
 * Main entry point - runs on Core0
 * 
 * Initializes system components and launches both cores with their
 * respective main functions using the event-driven state machine.
 */
int main() {

    // Initialize dual stdio like production system
    //stdio_usb_init();
    
    // Initialize UART0 for debug output


    stdio_uart_init_full(uart0, 115200, 16, 17);
    
    // Wait for USB-serial connection for debugging
    sleep_ms(2000);
    
    printf("UART2ETH COPYRIGHT 2025 CASSEL MESSTECHNIK GMBH\n--------SOFTWARE START--------\n");
    
    printf("202601071447\n");
    
    // Debug: Add explicit debug prints to isolate hang location
    printf("DEBUG: About to initialize shared memory...\n");
    
    // Initialize shared memory system
    if (!shared_memory_init()) {
        printf("ERROR: Shared memory init failed!\n");
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    printf("DEBUG: Shared memory init completed\n");
    
    // Initialize ring buffer for UART-TCP message bridging
    printf("DEBUG: About to initialize ringbuffer...\n");
    if (!ringbuffer_init()) {
        printf("ERROR: Ringbuffer init failed!\n");
        while (true) {
            sleep_ms(1000);  // Halt system on critical error - ringbuffer init failed
        }
    }
    printf("DEBUG: Ringbuffer init completed\n");
    
    // Initialize event-driven state machine
    printf("DEBUG: About to initialize state machine...\n");
    if (!state_machine_init()) {
        printf("ERROR: State machine init failed!\n");
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    printf("DEBUG: State machine init completed\n");
    
    // Initialize log manager
    printf("DEBUG: About to initialize log manager...\n");
    if (!log_manager_init()) {
        printf("ERROR: Log manager init failed!\n");
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    printf("DEBUG: Log manager init completed\n");
    
    // Ensure all memory operations are complete before launching Core1
    __dsb();  // Data Synchronization Barrier
    __isb();  // Instruction Synchronization Barrier
    
    // RP2350 specific: Ensure clocks are stable before Core1 launch
    printf("DEBUG: Verifying system clocks are stable...\n");
    uint32_t sys_clk = clock_get_hz(clk_sys);
    printf("DEBUG: System clock: %u Hz\n", sys_clk);
    
    if (sys_clk < 1000000) {  // Less than 1MHz indicates clock issues
        printf("ERROR: System clock too low (%u Hz), multicore unsafe\n", sys_clk);
        while(1) sleep_ms(1000);
    }
    
    printf("DEBUG: Clocks stable, launching Core1...\n");
    fflush(stdout);
    
    // Launch Core1 with network and maintenance processing
    // Note: On RP2350, this should automatically handle stack allocation
    multicore_launch_core1(core1_entry);
    
    printf("DEBUG: multicore_launch_core1() call completed\n");
    fflush(stdout);
    
    // Small delay to let any immediate hard fault surface
    sleep_ms(1);
    printf("DEBUG: No immediate hard fault detected\n");
    fflush(stdout);
    
    // Core0 runs the UART processing with event-driven state machine
    printf("DEBUG: About to call core0_main()...\n");
    core0_main();
    printf("DEBUG: core0_main() returned (this should never happen!)\n");
    
    // Should never reach here
    while (true) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_CORE_EXIT_ERROR, 0);
        sleep_ms(1000);
    }
    
    return 0;  // Never reached
}