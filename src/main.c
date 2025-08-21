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
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
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
void core1_entry() {
    printf("DEBUG: Core1 entry point reached\n");
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_STARTING, 0);
    printf("DEBUG: Core1 logged starting event\n");
    
    // Core1 runs the network, persistence, and log processing
    printf("DEBUG: Core1 about to call core1_main()\n");
    core1_main();
    printf("DEBUG: Core1 core1_main() returned (should never happen!)\n");
    
    // Should never reach here
    while (true) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_CORE_EXIT_ERROR, 1);
        sleep_ms(1000);
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
    stdio_usb_init();
    
    // Initialize UART0 for debug output
    stdio_uart_init_full(uart0, 115200, 0, 1);
    
    // Wait for USB-serial connection for debugging
    sleep_ms(2000);
    
    printf("UART2ETH COPYRIGHT 2025 CASSEL MESSTECHNIK GMBH\n--------SOFTWARE START--------\n");
    
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
    
    // Now we can use log_event() safely - add debug around this critical point
    printf("DEBUG: About to call first log_event...\n");
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_BOOT, 0);
    printf("DEBUG: First log_event completed\n");
    printf("DEBUG: About to call second batch of log_events...\n");
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SHARED_MEMORY_INIT, 0);
    printf("DEBUG: Log event 2 completed\n");
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 1);
    printf("DEBUG: Log event 3 completed\n");
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_STATE_MACHINE_INIT, 0);
    printf("DEBUG: Log event 4 completed\n");
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_LOG_MANAGER_INIT, 0);
    printf("DEBUG: All log_events completed, about to launch Core1...\n");
    
    // Launch Core1 with network and maintenance processing
    multicore_launch_core1(core1_entry);
    printf("DEBUG: Core1 launch call completed\n");
    
    // Give Core1 a moment to initialize
    printf("DEBUG: About to sleep 100ms for Core1 init...\n");
    sleep_ms(100);
    printf("DEBUG: Sleep completed, about to log events...\n");
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_LAUNCHED, 0);
    printf("DEBUG: LOG_EVENT_CORE1_LAUNCHED completed\n");
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE0_STARTING, 0);
    printf("DEBUG: LOG_EVENT_CORE0_STARTING completed\n");
    
    // Log dual-core launch completion
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
    printf("DEBUG: LOG_EVENT_SYSTEM_READY completed\n");
    
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