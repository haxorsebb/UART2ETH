cmake_minimum_required(VERSION 3.13)

# Include pico-sdk import cmake file
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Set project name
project(uart2eth C CXX ASM)

# Set board before initializing SDK
set(PICO_BOARD pico2)

# Target RP2350 with pico2 board
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

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
    pico_stdio_usb     # USB CDC serial
    pico_stdio_uart    # Hardware UART serial
    pico_multicore     # Dual core support
    pico_sync          # Synchronization primitives
    hardware_uart
    hardware_gpio
    hardware_spi
    config_manager     # Our config manager library
    log_manager        # Our log manager library
)

# Enable dual stdio (USB CDC + UART0)
pico_enable_stdio_usb(uart2eth 1)
pico_enable_stdio_uart(uart2eth 1)

# Create UF2 output
pico_add_extra_outputs(uart2eth)
