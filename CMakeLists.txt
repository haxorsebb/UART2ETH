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
    pico_lwip_nosys    # lwIP NO_SYS implementation for raw API
    config_manager     # Our config manager library
    log_manager        # Our log manager library
    state_machine      # Our event-driven state machine library
    network_manager    # Our network manager library (now with lwIP)
    core_modules       # Core0 and Core1 main functions
)

# Enable stdio: USB CDC for debugging, UART0 disabled to avoid SPI0 conflict
# UART1 is manually configured in main.c for hardware logging (GP12/GP13)
pico_enable_stdio_usb(uart2eth 1)
pico_enable_stdio_uart(uart2eth 1)  # Disable UART0 to avoid SPI0 conflict

# Set UF2 family for RP2350
pico_set_uf2_family(uart2eth "rp2350-arm-s")

# Embed partition table for flash isolation
pico_embed_pt_in_binary(uart2eth "${CMAKE_CURRENT_SOURCE_DIR}/partition_table.json")

# Create UF2 output
pico_add_extra_outputs(uart2eth)

# Standalone ping test target for debugging network issues
add_executable(ping_test_standalone
    ping_test_standalone.c
)

target_link_libraries(ping_test_standalone 
    pico_stdlib
    network_manager
    pico_lwip
    hardware_spi
    hardware_uart
)

target_include_directories(ping_test_standalone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

pico_enable_stdio_usb(ping_test_standalone 1)
pico_enable_stdio_uart(ping_test_standalone 1)
pico_set_uf2_family(ping_test_standalone "rp2350-arm-s")
pico_add_extra_outputs(ping_test_standalone)
