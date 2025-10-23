cmake_minimum_required(VERSION 3.13)

# Project declaration
project(uart2eth_tests C CXX ASM)

include(pico_sdk_import.cmake)

# Initialize the SDK
pico_sdk_init()

if (TARGET tinyusb_device)

# Unity test framework - simple embedded testing
add_library(unity
    unity/src/unity.c
)

target_include_directories(unity PUBLIC unity/src)

# Test for shared memory layout
add_executable(test_shared_memory
    test_shared_memory.c
    ../src/config/shared_memory.c
)

target_link_libraries(test_shared_memory
    unity
    network_manager     # Added: needed for shared_memory.c 
    pico_stdlib
    pico_sync
    hardware_sync
    hardware_gpio       # Added: needed for network_manager
    hardware_spi        # Added: needed for network_manager
    log_manager         # Added: needed for network_manager
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_shared_memory 1)
pico_enable_stdio_uart(test_shared_memory 1)  

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_shared_memory)

# Test for log manager
add_executable(test_log_manager
    test_log_manager.c
    ../src/config/shared_memory.c
    ../src/log/log_manager.c
)

target_include_directories(test_log_manager PRIVATE ../include)

target_link_libraries(test_log_manager
    unity
    network_manager     # Added: needed for shared_memory.c
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for performance tests
    hardware_sync
    hardware_gpio       # Added: needed for network_manager
    hardware_spi        # Added: needed for network_manager
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_log_manager 1)
pico_enable_stdio_uart(test_log_manager 1)  

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_log_manager)

# Test for flash persistence
add_executable(test_flash_persistence
    test_flash_persistence.c
    ../src/config/shared_memory.c
    ../src/config/flash_persistence.c
    ../src/log/log_manager.c
)

target_include_directories(test_flash_persistence PRIVATE ../include)

target_link_libraries(test_flash_persistence
    unity
    network_manager     # Added: needed for IP string conversion
    pico_stdlib
    pico_sync
    pico_multicore      # For Core1 integration testing
    hardware_sync
    hardware_gpio       # Added: needed for network_manager
    hardware_spi        # Added: needed for network_manager
    log_manager         # Added: needed for network_manager
    hardware_flash      # Flash operations
    hardware_sha256     # Hardware crypto for SHA256
    pico_bootrom        # Bootrom APIs for partition table
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_flash_persistence 1)
pico_enable_stdio_uart(test_flash_persistence 1)  

# Embed partition table for flash persistence testing
pico_embed_pt_in_binary(test_flash_persistence "${CMAKE_CURRENT_SOURCE_DIR}/../partition_table.json")

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_flash_persistence)

# Test for state machine
add_executable(test_state_machine
    test_state_machine.c
    ../src/state_machine/state_machine.c
    ../src/config/shared_memory.c
    ../src/log/log_manager.c
)

target_include_directories(test_state_machine PRIVATE ../include)

target_link_libraries(test_state_machine
    unity
    network_manager     # Added: needed for shared_memory.c dependency
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for state machine coordination
    hardware_sync       # Atomic operations for state transitions
    hardware_gpio       # Added: needed for network_manager
    hardware_spi        # Added: needed for network_manager
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_state_machine 1)
pico_enable_stdio_uart(test_state_machine 1)  

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_state_machine)

# Test for Core0 timer subsystem
add_executable(test_core0_timer
    test_core0_timer.c
    ../src/core0_timer.c
)

target_include_directories(test_core0_timer PRIVATE ../include)

target_link_libraries(test_core0_timer
    unity
    pico_stdlib
    hardware_timer
    hardware_irq
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_core0_timer 1)
pico_enable_stdio_uart(test_core0_timer 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_core0_timer)

# Test for Core1 timer subsystem
add_executable(test_core1_timer
    test_core1_timer.c
    ../src/core1_timer.c
)

target_include_directories(test_core1_timer PRIVATE ../include)

target_link_libraries(test_core1_timer
    unity
    pico_stdlib
    hardware_timer
    hardware_irq
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_core1_timer 1)
pico_enable_stdio_uart(test_core1_timer 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_core1_timer)

# Test for ring buffer
add_executable(test_ringbuffer
    test_ringbuffer.c
    ../src/ringbuffer/ringbuffer.c
    ../src/state_machine/state_machine.c
    ../src/config/shared_memory.c
    ../src/log/log_manager.c
)

target_include_directories(test_ringbuffer PRIVATE ../include)

target_link_libraries(test_ringbuffer
    unity
    network_manager     # Added: needed for shared_memory.c dependency
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for doorbell testing
    hardware_sync       # Mutex operations
    hardware_gpio       # Added: needed for network_manager
    hardware_spi        # Added: needed for network_manager
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_ringbuffer 1)
pico_enable_stdio_uart(test_ringbuffer 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_ringbuffer)

# Test for ring buffer - minimal version for debugging
add_executable(test_ringbuffer_minimal
    test_ringbuffer_minimal.c
    ../src/ringbuffer/ringbuffer.c
)

target_include_directories(test_ringbuffer_minimal PRIVATE ../include)

target_link_libraries(test_ringbuffer_minimal
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for doorbell testing
    hardware_sync       # Mutex operations
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_ringbuffer_minimal 1)
pico_enable_stdio_uart(test_ringbuffer_minimal 0)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_ringbuffer_minimal)

# Test for ring buffer - debug version with extensive printf
add_executable(test_ringbuffer_debug
    test_ringbuffer_debug.c
    ../src/ringbuffer/ringbuffer.c
)

target_include_directories(test_ringbuffer_debug PRIVATE ../include)

target_link_libraries(test_ringbuffer_debug
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for doorbell testing
    hardware_sync       # Mutex operations
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_ringbuffer_debug 1)
pico_enable_stdio_uart(test_ringbuffer_debug 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_ringbuffer_debug)

# Test for Core0 ringbuffer separation (ADR-012)
add_executable(test_core0_ringbuffer_separation
    test_core0_ringbuffer_separation.c
    ../src/core0_main.c
    ../src/state_machine/state_machine.c
    ../src/config/shared_memory.c
    ../src/log/log_manager.c
    ../src/ringbuffer/ringbuffer.c
)

target_include_directories(test_core0_ringbuffer_separation PRIVATE ../include)

target_link_libraries(test_core0_ringbuffer_separation
    unity
    network_manager     # Added: needed for shared_memory.c dependency
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for state machine and ringbuffer
    hardware_sync       # Atomic operations and mutex
    hardware_gpio       # Added: needed for network_manager
    hardware_spi        # Added: needed for network_manager
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_core0_ringbuffer_separation 1)
pico_enable_stdio_uart(test_core0_ringbuffer_separation 1)  
# Create UF2 output for flashing to device
pico_add_extra_outputs(test_core0_ringbuffer_separation)

# Test for Core0 minimal (debugging)
add_executable(test_core0_minimal
    test_core0_minimal.c
)

target_include_directories(test_core0_minimal PRIVATE ../include)

target_link_libraries(test_core0_minimal
    pico_stdlib
    hardware_sync       # Basic atomic operations
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_core0_minimal 1)
pico_enable_stdio_uart(test_core0_minimal 1)  

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_core0_minimal)

# Test for UART Hardware Manager (Issue #76)
add_executable(test_uart_manager
    test_uart_manager.c
    ../src/state_machine/state_machine.c
    ../src/config/shared_memory.c
    ../src/log/log_manager.c
    ../src/ringbuffer/ringbuffer.c
    ../src/uart/uart_manager.c
    ../src/uart/pl011_driver.c
    ../src/uart/uart_receive_buffer.c
)

target_include_directories(test_uart_manager PRIVATE 
    ../include
    ../include/uart
)

target_link_libraries(test_uart_manager
    unity
    network_manager     # Added: needed for shared_memory.c dependency  
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for state machine and ringbuffer
    hardware_sync       # Atomic operations and mutex
    hardware_gpio       # GPIO configuration for UART pins
    hardware_uart       # UART hardware support
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_uart_manager 1)
pico_enable_stdio_uart(test_uart_manager 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_uart_manager)

# Test for UART minimal (debugging UART hardware issues)
add_executable(test_uart_minimal
    test_uart_minimal.c
    ../src/uart/uart_manager.c
    ../src/uart/pl011_driver.c
    ../src/uart/uart_receive_buffer.c
    ../src/log/log_manager.c
    ../src/config/shared_memory.c
    ../src/ringbuffer/ringbuffer.c
    ../src/state_machine/state_machine.c
)

target_include_directories(test_uart_minimal PRIVATE 
    ../include
    ../include/uart
)

target_link_libraries(test_uart_minimal
    unity
    network_manager     # Added: needed for shared_memory.c dependency
    pico_stdlib
    pico_sync
    hardware_sync       # Atomic operations
    hardware_gpio       # GPIO configuration for UART pins
    hardware_uart       # UART hardware support
    hardware_irq        # Interrupt support
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_uart_minimal 1)
pico_enable_stdio_uart(test_uart_minimal 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_uart_minimal)

# Test for PIO UART Driver (Issue #82)
add_executable(test_pio_uart_driver
    test_pio_uart_driver.c
    ../src/state_machine/state_machine.c
    ../src/config/shared_memory.c
    ../src/log/log_manager.c
    ../src/ringbuffer/ringbuffer.c
    ../src/uart/uart_manager.c
    ../src/uart/pl011_driver.c
    ../src/uart/pio_uart_driver.c
    ../src/uart/uart_receive_buffer.c
)

# Generate PIO headers for PIO UART test
pico_generate_pio_header(test_pio_uart_driver ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_tx.pio)
pico_generate_pio_header(test_pio_uart_driver ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_rx.pio)

target_include_directories(test_pio_uart_driver PRIVATE 
    ../include
    ../include/uart
)

target_link_libraries(test_pio_uart_driver
    unity
    network_manager     # Added: needed for shared_memory.c dependency
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for state machine and ringbuffer
    hardware_sync       # Atomic operations and mutex
    hardware_gpio       # GPIO configuration for UART pins
    hardware_pio        # PIO hardware support
    hardware_dma        # DMA support for PIO UART
    hardware_irq        # Interrupt support
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_pio_uart_driver 1)
pico_enable_stdio_uart(test_pio_uart_driver 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_pio_uart_driver)

# Minimal PIO UART test - direct driver testing
add_executable(test_pio_uart_minimal
    test_pio_uart_minimal.c
    ../src/uart/pio_uart_driver.c
    ../src/uart/uart_receive_buffer.c
)

# Generate PIO headers for minimal test
pico_generate_pio_header(test_pio_uart_minimal ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_tx.pio)
pico_generate_pio_header(test_pio_uart_minimal ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_rx.pio)

target_include_directories(test_pio_uart_minimal PRIVATE
    ../include
    ../include/uart
)

target_link_libraries(test_pio_uart_minimal
    unity
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_gpio
)

pico_enable_stdio_usb(test_pio_uart_minimal 1)
pico_enable_stdio_uart(test_pio_uart_minimal 1)

pico_add_extra_outputs(test_pio_uart_minimal)

# Direct PIO UART test - bypassing all UART manager logic
add_executable(test_pio_direct
    test_pio_direct.c
)

# Generate PIO headers for direct test
pico_generate_pio_header(test_pio_direct ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_tx.pio)
pico_generate_pio_header(test_pio_direct ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_rx.pio)

target_link_libraries(test_pio_direct
    pico_stdlib
    hardware_pio
    hardware_gpio
)

pico_enable_stdio_usb(test_pio_direct 1)
pico_enable_stdio_uart(test_pio_direct 1)

pico_add_extra_outputs(test_pio_direct)

# PIO UART Integration test - through UART manager but bypassing ring buffer
add_executable(test_pio_uart_integration
    test_pio_uart_integration.c
    ../src/state_machine/state_machine.c
    ../src/config/shared_memory.c
    ../src/log/log_manager.c
    ../src/ringbuffer/ringbuffer.c
    ../src/uart/uart_manager.c
    ../src/uart/pl011_driver.c
    ../src/uart/pio_uart_driver.c
    ../src/uart/uart_receive_buffer.c
)

# Generate PIO headers for integration test
pico_generate_pio_header(test_pio_uart_integration ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_tx.pio)
pico_generate_pio_header(test_pio_uart_integration ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_rx.pio)

target_include_directories(test_pio_uart_integration PRIVATE 
    ../include
    ../include/uart
)

target_link_libraries(test_pio_uart_integration
    network_manager
    pico_stdlib
    pico_sync
    pico_multicore
    hardware_sync
    hardware_gpio
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_watchdog
)

pico_enable_stdio_usb(test_pio_uart_integration 1)
pico_enable_stdio_uart(test_pio_uart_integration 1)

pico_add_extra_outputs(test_pio_uart_integration)

# PIO Interrupt test - verify RX interrupt triggering
add_executable(test_pio_interrupt
    test_pio_interrupt.c
)

pico_generate_pio_header(test_pio_interrupt ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_tx.pio)
pico_generate_pio_header(test_pio_interrupt ${CMAKE_CURRENT_LIST_DIR}/../src/uart/pio_uart_rx.pio)

target_link_libraries(test_pio_interrupt
    pico_stdlib
    hardware_pio
    hardware_gpio
    hardware_irq
)

pico_enable_stdio_usb(test_pio_interrupt 1)
pico_enable_stdio_uart(test_pio_interrupt 1)

pico_add_extra_outputs(test_pio_interrupt)

# GPIO Loopback Connectivity Test - diagnose hardware connections
add_executable(test_gpio_loopback
    test_gpio_loopback.c
)

target_link_libraries(test_gpio_loopback
    pico_stdlib
    hardware_gpio
)

pico_enable_stdio_usb(test_gpio_loopback 1)
pico_enable_stdio_uart(test_gpio_loopback 1)

pico_add_extra_outputs(test_gpio_loopback)

# Add network tests subdirectory
add_subdirectory(network)

elseif(PICO_ON_DEVICE)
    message("Skipping EVERYTHING! because TinyUSB submodule is not initialized in the SDK")
endif()