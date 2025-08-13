/**
 * @file debug_example.c
 * @brief Example demonstrating DEBUG flag usage in UART2ETH project
 * 
 * This file shows how to use the debug infrastructure properly.
 * It's compiled conditionally based on the DEBUG flag.
 * 
 * To build with debug:   cmake -DDEBUG=ON ..
 * To build without debug: cmake -DDEBUG=OFF .. (or just cmake ..)
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "debug.h"
#include "log_manager.h"
#include "shared_memory.h"

/**
 * Example function showing debug utilities
 */
void debug_example_function(void) {
    DEBUG_FUNCTION_ENTER(__func__);
    
    // Debug-only code that won't be compiled in release builds
    DEBUG_ONLY({
        printf("This code only runs in debug builds\n");
    });
    
    // Conditional compilation based on debug mode
    const char* build_type = IF_DEBUG("DEBUG", "RELEASE");
    printf("Running in %s mode\n", build_type);
    
    // Debug logging (only appears in debug builds)
    DEBUG_LOG(EVENT_SOURCE_SYSTEM, LOG_EVENT_SYSTEM_READY, 42);
    DEBUG_PRINTF("Debug printf with value: %d", 123);
    
    // Regular logging (appears in both debug and release if level >= minimum)
    INFO_LOG(EVENT_SOURCE_SYSTEM, LOG_EVENT_SYSTEM_READY, 0);
    WARN_LOG(EVENT_SOURCE_SYSTEM, LOG_EVENT_SYSTEM_ERROR, 1);
    ERROR_LOG(EVENT_SOURCE_SYSTEM, LOG_EVENT_SYSTEM_ERROR, 2);
    
    // Debug assertion (only active in debug builds)
    DEBUG_ASSERT(1 == 1, "This assertion should pass");
    
    // Timing measurement (only in debug builds)
    debug_timer_t timer;
    DEBUG_TIMER_START(timer, "example_operation");
    
    // Simulate some work
    sleep_ms(10);
    
    DEBUG_TIMER_END(timer);
    
    // Memory marking (debug builds only)
    DEBUG_MEMORY_MARK("After simulated work");
    
    DEBUG_FUNCTION_EXIT(__func__);
}

/**
 * Example of network debugging
 */
void debug_network_example(void) {
    DEBUG_FUNCTION_ENTER(__func__);
    
    // Simulate network activity with debug logging
    DEBUG_NETWORK_CONNECTION(true, 8080);   // Connection established
    DEBUG_NETWORK_PACKET(false, 256, 8080); // Received 256 bytes
    DEBUG_NETWORK_PACKET(true, 128, 8080);  // Sent 128 bytes
    DEBUG_NETWORK_CONNECTION(false, 8080);  // Connection closed
    
    DEBUG_FUNCTION_EXIT(__func__);
}

/**
 * Example of UART debugging
 */
void debug_uart_example(void) {
    DEBUG_FUNCTION_ENTER(__func__);
    
    // Simulate UART activity with debug logging
    DEBUG_UART_DATA(0, false, 32);  // UART0 received 32 bytes
    DEBUG_UART_DATA(0, true, 16);   // UART0 sent 16 bytes
    DEBUG_UART_DATA(1, false, 64);  // UART1 received 64 bytes
    
    DEBUG_FUNCTION_EXIT(__func__);
}

/**
 * Main function for debug example
 */
int main() {
    // Initialize standard I/O
    stdio_init_all();
    
    // Show build information
    DEBUG_BUILD_INFO();
    
    printf("=== UART2ETH Debug Example ===\n");
    
    // Initialize shared memory and log manager
    if (!shared_memory_init()) {
        printf("ERROR: Failed to initialize shared memory\n");
        return 1;
    }
    
    if (!log_manager_init()) {
        printf("ERROR: Failed to initialize log manager\n");
        return 1;
    }
    
    printf("Initialized successfully\n");
    
    // Run debug examples
    debug_example_function();
    debug_network_example();
    debug_uart_example();
    
    printf("Debug examples completed\n");
    
    // Process and display any pending log entries
    uint32_t processed = 0;
    do {
        processed = log_manager_format_pending();
        if (processed > 0) {
            printf("Processed %u log entries\n", processed);
        }
        sleep_ms(10);
    } while (processed > 0);
    
    // Show log manager statistics
    printf("\n=== Log Manager Statistics ===\n");
    printf("Total events logged: %u\n", log_manager_get_total_count());
    printf("Buffer utilization: %u%%\n", log_manager_get_utilization());
    printf("Pending count: %u\n", log_manager_get_pending_count());
    printf("Core 0 sequence: %u\n", log_manager_get_core_sequence(0));
    printf("Core 1 sequence: %u\n", log_manager_get_core_sequence(1));
    
    // Keep running forever as required by development environment
    while (true) {
        printf("Debug example completed - running forever\n");
        sleep_ms(5000);
    }
    
    return 0;
}