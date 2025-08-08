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
    pico_stdlib
    pico_sync
    hardware_sync
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_shared_memory 1)
pico_enable_stdio_uart(test_shared_memory 0)  # Disable UART0 to avoid SPI0 conflict

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
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for performance tests
    hardware_sync
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_log_manager 1)
pico_enable_stdio_uart(test_log_manager 0)  # Disable UART0 to avoid SPI0 conflict

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
    pico_stdlib
    pico_sync
    pico_multicore      # For Core1 integration testing
    hardware_sync
    hardware_flash      # Flash operations
    hardware_sha256     # Hardware crypto for SHA256
    pico_bootrom        # Bootrom APIs for partition table
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_flash_persistence 1)
pico_enable_stdio_uart(test_flash_persistence 0)  # Disable UART0 to avoid SPI0 conflict

# Embed partition table for flash persistence testing
pico_embed_pt_in_binary(test_flash_persistence "${CMAKE_CURRENT_SOURCE_DIR}/../partition_table.json")

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_flash_persistence)

# Test for state machine
add_executable(test_state_machine
    test_state_machine.c
    ../src/state_machine/state_machine.c
)

target_include_directories(test_state_machine PRIVATE ../include)

target_link_libraries(test_state_machine
    unity
    pico_stdlib
    pico_sync
    pico_multicore      # Dual core support for state machine coordination
    hardware_sync       # Atomic operations for state transitions
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_state_machine 1)
pico_enable_stdio_uart(test_state_machine 0)  # Disable UART0 to avoid SPI0 conflict

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_state_machine)

# Add network tests subdirectory
add_subdirectory(network)

elseif(PICO_ON_DEVICE)
    message("Skipping EVERYTHING! because TinyUSB submodule is not initialized in the SDK")
endif()