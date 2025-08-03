cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

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
    pico_stdlib
    pico_sync
    hardware_sync
    hardware_watchdog   # Watchdog for stability
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_log_manager 1)
pico_enable_stdio_uart(test_log_manager 1)

# Create UF2 output for flashing to device
pico_add_extra_outputs(test_log_manager)

elseif(PICO_ON_DEVICE)
    message("Skipping hello_usb because TinyUSB submodule is not initialized in the SDK")
endif()