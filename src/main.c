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
    // Launch Core1 main function directly
    core1_main();
    
    // Should never reach here
    while (true) {
        busy_wait_us(1000000);  // 1 second - error state
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
    
    // Configure GPIO21 to output 25 MHz clock for ENC28J60
    float clk_div = (float)clock_get_hz(clk_sys) / 25000000.0f;
    clock_gpio_init(21, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, clk_div);
    
    // Wait for USB-serial connection for debugging
    sleep_ms(2000);
    
    printf("UART2ETH COPYRIGHT 2025 CASSEL MESSTECHNIK GMBH\n--------SOFTWARE START--------\n");
    
    printf("202601161220\n");
    
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