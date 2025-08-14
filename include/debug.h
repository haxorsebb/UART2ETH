/**
 * @file debug.h
 * @brief Debug utilities and macros for UART2ETH project
 * 
 * Provides debug-specific functionality that integrates with the existing
 * log manager system. Enables conditional compilation of debug code and
 * convenient debug logging macros.
 * 
 * Documentation Reference:
 * - arc42 Chapter 9 - Development Environment - Debug Infrastructure
 */

#ifndef DEBUG_H
#define DEBUG_H

#include "log_manager.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Debug mode detection (set by CMake)
#ifndef DEBUG
#define DEBUG 0
#endif

// Default minimum log level if not set by CMake
#ifndef LOG_MINIMUM_LEVEL
#if DEBUG
#define LOG_MINIMUM_LEVEL LOG_LEVEL_DEBUG
#else
#define LOG_MINIMUM_LEVEL LOG_LEVEL_INFO
#endif
#endif

/**
 * Debug-only code compilation macro
 * Use this to wrap code that should only be compiled in debug builds
 */
#if DEBUG
#define DEBUG_ONLY(code) do { code } while(0)
#define IF_DEBUG(debug_code, release_code) debug_code
#else
#define DEBUG_ONLY(code) do { } while(0)
#define IF_DEBUG(debug_code, release_code) release_code
#endif

/**
 * Debug assertion macro
 * In debug mode: checks condition and logs error if false
 * In release mode: disabled for performance
 */
#if DEBUG
#define DEBUG_ASSERT(condition, message, ...) \
    do { \
        if (!(condition)) { \
            printf("ASSERTION FAILED: %s:%d - " message "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
            log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_SYSTEM_ERROR, __LINE__); \
        } \
    } while(0)
#else
#define DEBUG_ASSERT(condition, message, ...) do { } while(0)
#endif

/**
 * Conditional logging macros that respect minimum log level
 * Only log if the message level >= LOG_MINIMUM_LEVEL
 */
#define SHOULD_LOG(level) ((level) >= LOG_MINIMUM_LEVEL)

/**
 * Convenient debug logging macros
 * These integrate with the existing log manager system
 */
#if DEBUG
#define DEBUG_LOG(source, event_type, extra_value) \
    do { \
        if (SHOULD_LOG(LOG_LEVEL_DEBUG)) { \
            log_event((source), LOG_LEVEL_DEBUG, (event_type), (extra_value)); \
        } \
    } while(0)

#define DEBUG_PRINTF(format, ...) \
    do { \
        printf("[DEBUG] " format "\n", ##__VA_ARGS__); \
        fflush(stdout); \
    } while(0)
#else
#define DEBUG_LOG(source, event_type, extra_value) do { } while(0)
#define DEBUG_PRINTF(format, ...) do { } while(0)
#endif

/**
 * Standard logging macros that work in both debug and release builds
 * but respect the minimum log level
 */
#define INFO_LOG(source, event_type, extra_value) \
    do { \
        if (SHOULD_LOG(LOG_LEVEL_INFO)) { \
            log_event((source), LOG_LEVEL_INFO, (event_type), (extra_value)); \
        } \
    } while(0)

#define WARN_LOG(source, event_type, extra_value) \
    do { \
        if (SHOULD_LOG(LOG_LEVEL_WARN)) { \
            log_event((source), LOG_LEVEL_WARN, (event_type), (extra_value)); \
        } \
    } while(0)

#define ERROR_LOG(source, event_type, extra_value) \
    do { \
        if (SHOULD_LOG(LOG_LEVEL_ERROR)) { \
            log_event((source), LOG_LEVEL_ERROR, (event_type), (extra_value)); \
        } \
    } while(0)

/**
 * Debug timing utilities
 * For measuring execution time in debug builds
 */
#if DEBUG
typedef struct {
    uint32_t start_time;
    const char* operation_name;
} debug_timer_t;

#define DEBUG_TIMER_START(timer_var, name) \
    do { \
        (timer_var).operation_name = (name); \
        (timer_var).start_time = (uint32_t)(get_absolute_time() / 1000); \
    } while(0)

#define DEBUG_TIMER_END(timer_var) \
    do { \
        uint32_t end_time = (uint32_t)(get_absolute_time() / 1000); \
        uint32_t duration = end_time - (timer_var).start_time; \
        DEBUG_PRINTF("TIMING: %s took %u ms", (timer_var).operation_name, duration); \
    } while(0)
#else
typedef struct { int dummy; } debug_timer_t;
#define DEBUG_TIMER_START(timer_var, name) do { } while(0)
#define DEBUG_TIMER_END(timer_var) do { } while(0)
#endif

/**
 * Memory debug utilities
 * For tracking memory usage in debug builds
 */
#if DEBUG
#define DEBUG_MEMORY_MARK(description) \
    do { \
        DEBUG_PRINTF("MEMORY: %s", description); \
    } while(0)
#else
#define DEBUG_MEMORY_MARK(description) do { } while(0)
#endif

/**
 * Function entry/exit tracing
 * For debugging control flow in complex scenarios
 */
#if DEBUG
#define DEBUG_FUNCTION_ENTER(func_name) \
    DEBUG_PRINTF("ENTER: %s", func_name)

#define DEBUG_FUNCTION_EXIT(func_name) \
    DEBUG_PRINTF("EXIT: %s", func_name)

#define DEBUG_FUNCTION_EXIT_WITH_VALUE(func_name, value) \
    DEBUG_PRINTF("EXIT: %s -> %u", func_name, (uint32_t)(value))
#else
#define DEBUG_FUNCTION_ENTER(func_name) do { } while(0)
#define DEBUG_FUNCTION_EXIT(func_name) do { } while(0)
#define DEBUG_FUNCTION_EXIT_WITH_VALUE(func_name, value) do { } while(0)
#endif

/**
 * GPIO debug utilities for hardware debugging
 */
#if DEBUG
#define DEBUG_GPIO_TOGGLE(pin) \
    do { \
        static bool toggle_state = false; \
        gpio_put(pin, toggle_state); \
        toggle_state = !toggle_state; \
    } while(0)

#define DEBUG_GPIO_SET(pin, value) \
    do { \
        gpio_put(pin, value); \
        DEBUG_PRINTF("GPIO %u set to %s", pin, (value) ? "HIGH" : "LOW"); \
    } while(0)
#else
#define DEBUG_GPIO_TOGGLE(pin) do { } while(0)
#define DEBUG_GPIO_SET(pin, value) do { } while(0)
#endif

/**
 * Network debug utilities
 */
#if DEBUG
#define DEBUG_NETWORK_PACKET(direction, size, port) \
    DEBUG_LOG(EVENT_SOURCE_NETWORK, \
              (direction) ? LOG_EVENT_TCP_DATA_TX_BYTES : LOG_EVENT_TCP_DATA_RX_BYTES, \
              (size))

#define DEBUG_NETWORK_CONNECTION(connected, port) \
    DEBUG_LOG(EVENT_SOURCE_NETWORK, \
              (connected) ? LOG_EVENT_TCP_CONNECT : LOG_EVENT_TCP_DISCONNECT, \
              (port))
#else
#define DEBUG_NETWORK_PACKET(direction, size, port) do { } while(0)
#define DEBUG_NETWORK_CONNECTION(connected, port) do { } while(0)
#endif

/**
 * UART debug utilities
 */
#if DEBUG
#define DEBUG_UART_DATA(uart_num, direction, byte_count) \
    do { \
        event_type_t event_type; \
        switch(uart_num) { \
            case 0: event_type = (direction) ? LOG_EVENT_UART0_DATA_TX : LOG_EVENT_UART0_DATA_RX; break; \
            case 1: event_type = (direction) ? LOG_EVENT_UART1_DATA_TX : LOG_EVENT_UART1_DATA_RX; break; \
            case 2: event_type = (direction) ? LOG_EVENT_UART2_DATA_TX : LOG_EVENT_UART2_DATA_RX; break; \
            case 3: event_type = (direction) ? LOG_EVENT_UART3_DATA_TX : LOG_EVENT_UART3_DATA_RX; break; \
            default: event_type = LOG_EVENT_UART_INIT_GENERIC; break; \
        } \
        DEBUG_LOG(EVENT_SOURCE_UART0 + (uart_num), event_type, (byte_count)); \
    } while(0)
#else
#define DEBUG_UART_DATA(uart_num, direction, byte_count) do { } while(0)
#endif

/**
 * Compile-time debug information
 */
#if DEBUG
#define DEBUG_BUILD_INFO() \
    do { \
        printf("DEBUG BUILD: %s %s\n", __DATE__, __TIME__); \
        printf("LOG_MINIMUM_LEVEL: %u\n", LOG_MINIMUM_LEVEL); \
    } while(0)
#else
#define DEBUG_BUILD_INFO() do { } while(0)
#endif

#endif // DEBUG_H