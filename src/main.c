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
#include "shared_memory.h"
#include "state_machine.h"
#include "log_manager.h"

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
    printf("UART2ETH Core1 starting...\n");
    
    // Core1 runs the network, persistence, and log processing
    core1_main();
    
    // Should never reach here
    while (true) {
        printf("ERROR: Core1 main loop exited unexpectedly\n");
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
    // Initialize Pico SDK
    stdio_init_all();
    
    // Wait for USB-serial connection for debugging
    sleep_ms(2000);
    
    printf("=== UART2ETH Firmware Starting ===\n");
    printf("Initializing dual-core event-driven architecture...\n");
    
    // Initialize shared memory system
    printf("Initializing shared memory...\n");
    if (!shared_memory_init()) {
        printf("FATAL ERROR: Failed to initialize shared memory\n");
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    printf("Shared memory initialized successfully\n");
    
    // Initialize event-driven state machine
    printf("Initializing event-driven state machine...\n");
    if (!state_machine_init()) {
        printf("FATAL ERROR: Failed to initialize state machine\n");
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    printf("Event-driven state machine initialized successfully\n");
    
    // Initialize log manager
    printf("Initializing log manager...\n");
    if (!log_manager_init()) {
        printf("FATAL ERROR: Failed to initialize log manager\n");
        while (true) {
            sleep_ms(1000);  // Halt system on critical error
        }
    }
    printf("Log manager initialized successfully\n");
    
    // Log system startup
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_BOOT, 0);
    
    // Launch Core1 with network and maintenance processing
    printf("Launching Core1 for network, persistence, and log processing...\n");
    multicore_launch_core1(core1_entry);
    
    // Give Core1 a moment to initialize
    sleep_ms(100);
    
    printf("Core1 launched successfully\n");
    printf("Starting Core0 UART processing...\n");
    
    // Log dual-core launch completion
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_READY, 0);
    
    // Core0 runs the UART processing with event-driven state machine
    core0_main();
    
    // Should never reach here
    while (true) {
        printf("ERROR: Core0 main loop exited unexpectedly\n");
        sleep_ms(1000);
    }
    
    return 0;  // Never reached
}