cmake_minimum_required(VERSION 3.13)

# Set board before initializing SDK
set(PICO_BOARD pico2)

# Pull in SDK (must be before project)
include(src/pico_sdk_import.cmake)


# Set project name
project(uart2eth C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.2.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.2.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()


set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()


add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        )

# Add include directories
include_directories(include)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)

# Create main executable target
add_executable(uart2eth
    src/main.c
)

# Link libraries
target_link_libraries(uart2eth 
    pico_stdlib
    pico_multicore     # Dual core support
    pico_sync          # Synchronization primitives
    hardware_uart
    hardware_gpio
    hardware_spi
    hardware_watchdog  # Watchdog for stability
    config_manager     # Our config manager library
    log_manager        # Our log manager library
)

# Enable dual stdio (USB CDC + UART0)
pico_enable_stdio_usb(uart2eth 1)
pico_enable_stdio_uart(uart2eth 1)

# Create UF2 output
pico_add_extra_outputs(uart2eth)
