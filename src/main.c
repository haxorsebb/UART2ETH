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
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_STARTING, 0);
    
    // Core1 runs the network, persistence, and log processing
    core1_main();
    
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
    
    DEBUG_ONLY({
        printf("UART2ETH COPYRIGHT 2025 CASSEL MESSTECHNIK GMBH\n--------SOFTWARE START--------\n");
    });
    
    // Initialize shared memory system
    if (!shared_memory_init()) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_SYSTEM_ERROR, 1);
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_BOOT, 0);
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SHARED_MEMORY_INIT, 0);
    
    // Initialize ring buffer for UART-TCP message bridging
    if (!ringbuffer_init()) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_SYSTEM_ERROR, 2);
        while (true) {
            sleep_ms(1000);  // Halt system on critical error - ringbuffer init failed
        }
    }
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 1);
    
    // Initialize event-driven state machine
    if (!state_machine_init()) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_SYSTEM_ERROR, 3);
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_STATE_MACHINE_INIT, 0);
    
    // Initialize log manager
    if (!log_manager_init()) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_SYSTEM_ERROR, 4);
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_LOG_MANAGER_INIT, 0);
    
    // Launch Core1 with network and maintenance processing
    multicore_launch_core1(core1_entry);
    
    // Give Core1 a moment to initialize
    sleep_ms(100);
    
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE1_LAUNCHED, 0);
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_CORE0_STARTING, 0);
    
    // Log dual-core launch completion
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
    
    // Core0 runs the UART processing with event-driven state machine
    core0_main();
    
    // Should never reach here
    while (true) {
        log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_CORE_EXIT_ERROR, 0);
        sleep_ms(1000);
    }
    
    return 0;  // Never reached
}