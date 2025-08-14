# DEBUG Flag Usage Guide

## Overview

The UART2ETH project includes a comprehensive DEBUG flag system that integrates with the existing log manager infrastructure. This allows for conditional compilation of debug code and runtime control of logging verbosity.

## Build Configuration

### Debug Build
```bash
cd /home/shueltenschmidt/projects/UART2ETH/build
PICO_SDK_PATH=~/projects/pico-sdk cmake -DPICO_BOARD=pico2 -DDEBUG=ON ..
make -j$(nproc)
```

### Release Build (default)
```bash
cd /home/shueltenschmidt/projects/UART2ETH/build
PICO_SDK_PATH=~/projects/pico-sdk cmake -DPICO_BOARD=pico2 -DDEBUG=OFF ..
# or simply:
PICO_SDK_PATH=~/projects/pico-sdk cmake -DPICO_BOARD=pico2 ..
make -j$(nproc)
```

## What Changes with DEBUG=ON

### 1. Compiler Settings
- **Debug builds**: `-g -O0` (debug symbols, no optimization)
- **Release builds**: `-O2` (optimization enabled)

### 2. Log Level Filtering
- **Debug builds**: `LOG_MINIMUM_LEVEL=LOG_LEVEL_DEBUG` (all logs shown)
- **Release builds**: `LOG_MINIMUM_LEVEL=LOG_LEVEL_INFO` (debug logs filtered out)

### 3. Debug-Only Code
- Debug-only code is compiled only when `DEBUG=ON`
- Uses `DEBUG_ONLY()` macro for conditional compilation

### 4. Additional Targets
- **Debug builds**: Includes `debug_example` target
- **Release builds**: Debug example not built

## Debug Macros and Utilities

### Basic Debug Macros

```c
#include "debug.h"

// Debug-only code (compiled out in release)
DEBUG_ONLY({
    printf("This only runs in debug builds\n");
});

// Conditional compilation
const char* build_type = IF_DEBUG("DEBUG", "RELEASE");

// Debug assertions (only active in debug builds)
DEBUG_ASSERT(condition, "Error message with %d", value);
```

### Logging Macros

```c
// Debug logging (only in debug builds)
DEBUG_LOG(EVENT_SOURCE_SYSTEM, LOG_EVENT_SYSTEM_READY, 42);
DEBUG_PRINTF("Debug message: %d", value);

// Standard logging (filtered by minimum level)
INFO_LOG(EVENT_SOURCE_UART0, LOG_EVENT_UART0_DATA_RX, byte_count);
WARN_LOG(EVENT_SOURCE_NETWORK, LOG_EVENT_NETWORK_ERROR, error_code);
ERROR_LOG(EVENT_SOURCE_SYSTEM, LOG_EVENT_SYSTEM_ERROR, error_code);
```

### Timing and Performance

```c
debug_timer_t timer;
DEBUG_TIMER_START(timer, "operation_name");
// ... your code ...
DEBUG_TIMER_END(timer);  // Prints execution time in debug builds
```

### Function Tracing

```c
void my_function(void) {
    DEBUG_FUNCTION_ENTER(__func__);
    
    // ... function body ...
    
    DEBUG_FUNCTION_EXIT_WITH_VALUE(__func__, return_value);
}
```

### Hardware-Specific Debug

```c
// GPIO debugging
DEBUG_GPIO_SET(pin, value);      // Set GPIO and log the change
DEBUG_GPIO_TOGGLE(pin);          // Toggle GPIO for timing analysis

// Network debugging
DEBUG_NETWORK_CONNECTION(true, port);    // Log connection events
DEBUG_NETWORK_PACKET(false, size, port); // Log packet traffic

// UART debugging  
DEBUG_UART_DATA(uart_num, direction, byte_count); // Log UART traffic
```

## Testing the Debug System

### Build and Test Debug Example

```bash
# Build with debug enabled
cd /home/shueltenschmidt/projects/UART2ETH/build
PICO_SDK_PATH=~/projects/pico-sdk cmake -DPICO_BOARD=pico2 -DDEBUG=ON ..
make debug_example

# Flash and test
picotool load -F -p 0 debug_example.uf2 && picotool load -F -p 1 debug_example.uf2
picotool reboot -f

# Monitor output
./tools/persistent_uart_logger.sh tail
```

Expected debug output includes:
- Build information (date, time, log level)
- Function entry/exit traces
- Debug timer measurements
- Network and UART activity simulation
- Log manager statistics

### Compare Debug vs Release

```bash
# Build release version
cmake -DPICO_BOARD=pico2 -DDEBUG=OFF ..
make uart2eth

# Compare binary sizes
ls -la *.uf2
# Debug build will be larger due to debug symbols and extra code
```

## Integration with Existing Code

### In Source Files

```c
#include "debug.h"
#include "log_manager.h"

void uart_process_data(uint8_t uart_num, const uint8_t* data, size_t len) {
    DEBUG_FUNCTION_ENTER(__func__);
    
    // Debug-only validation
    DEBUG_ASSERT(data != NULL, "Data pointer is NULL for UART %u", uart_num);
    DEBUG_ASSERT(len > 0, "Zero length data for UART %u", uart_num);
    
    // Log the activity (appears in both debug and release if level >= INFO)
    INFO_LOG(EVENT_SOURCE_UART0 + uart_num, LOG_EVENT_UART0_DATA_RX + (uart_num * 4), len);
    
    // Debug-specific logging with more detail
    DEBUG_UART_DATA(uart_num, false, len);
    DEBUG_PRINTF("Processing %zu bytes on UART %u", len, uart_num);
    
    // ... actual processing ...
    
    DEBUG_FUNCTION_EXIT(__func__);
}
```

### In Header Files

```c
// Use debug macros for conditional function declarations
#if DEBUG
void debug_dump_uart_state(uint8_t uart_num);
void debug_validate_network_config(void);
#endif
```

## Log Level Control

The debug system controls the minimum log level at compile time:

- **DEBUG=ON**: Shows DEBUG, INFO, WARN, ERROR
- **DEBUG=OFF**: Shows INFO, WARN, ERROR (DEBUG filtered out)

Runtime log filtering happens automatically in the `log_event()` function, so there's no performance penalty for disabled log levels.

## Best Practices

1. **Use appropriate macros**: `DEBUG_ONLY()` for debug-specific code, logging macros for structured events
2. **Respect log levels**: Use DEBUG for verbose tracing, INFO for important events, WARN/ERROR for issues
3. **Include context**: Use the extra_value parameter in log events for meaningful data
4. **Test both modes**: Verify functionality works in both DEBUG=ON and DEBUG=OFF builds
5. **Performance**: Debug builds are slower due to extra logging and no optimization

## Memory Impact

- Debug builds use more flash memory (additional strings, debug code)
- Debug builds may use slightly more RAM (debug variables, extended log messages)
- Release builds are optimized for size and performance

## Troubleshooting

### Debug Example Won't Build
- Ensure `DEBUG=ON` is set in CMake configuration
- Check that all dependencies (log_manager, config_manager) are built
- Verify include paths are correct

### No Debug Output
- Confirm debug build: check for "DEBUG BUILD:" message in output
- Verify minimum log level: `LOG_MINIMUM_LEVEL` should be `LOG_LEVEL_DEBUG` (0)
- Check log manager initialization

### Performance Issues in Release
- Confirm release build with `DEBUG=OFF`
- Verify optimization flags (`-O2`) are applied
- Check that debug macros compile to empty statements