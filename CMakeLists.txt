# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Configure compiler flags based on debug mode
# set early to make it work
if(DEBUG)
    message(STATUS "UART2ETH: Debug mode enabled")
    add_compile_definitions(DEBUG=1)
    add_compile_definitions(LOG_MINIMUM_LEVEL=LOG_LEVEL_DEBUG)
    add_compile_options(-g -O0)  # Debug symbols, no optimization
    set(CMAKE_BUILD_TYPE Debug)
else()
    message(STATUS "UART2ETH: Release mode")
    # add_compile_definitions(DEBUG=0)
    add_compile_definitions(LOG_MINIMUM_LEVEL=LOG_LEVEL_DEBUG)
    add_compile_options(-O2)     # Optimization for release
    set(CMAKE_BUILD_TYPE Release)
endif()

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards)

# Set board before initializing SDK
# OPTION 1: Original custom board with 12MHz external clock (comment out for testing)
set(PICO_BOARD uart2eth_board)
# OPTION 2: Test board with internal oscillator (uncomment for debugging)
#set(PICO_BOARD uart2eth_board_rosc)
#set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards)
#set(PICO_BOARD pico2)

# Set platform explicitly for RP2350 (required for uart2eth_board which inherits from pico2)
set(PICO_PLATFORM rp2350)

# Pull in SDK (must be before project)
include(src/pico_sdk_import.cmake)

# build time in UTC ISO 8601
FILE (WRITE ${CMAKE_BINARY_DIR}/timestamp.cmake "STRING(TIMESTAMP TIMEZ UTC)\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(WRITE timestamp.h \"#ifndef TIMESTAMP_H\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(APPEND timestamp.h \"#define TIMESTAMP_H\\n\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(APPEND timestamp.h \"#define _TIMEZ_ \\\"\${TIMEZ}\\\"\\n\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(APPEND timestamp.h \"#endif // TIMESTAMP_H\\n\")\n")
ADD_CUSTOM_TARGET (
    timestamp
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/timestamp.cmake
    ADD_DEPENDENCIES ${CMAKE_BINARY_DIR}/timestamp.cmake)
    
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

# ====================================================================================
# Device Mode Selection
# ====================================================================================
# To change the device mode, modify the line below:
#   "SHARK"     - 2 UARTs (Channel 0 debug + Channel 1), ENC28J60 Ethernet
#   "PRIMARY"   - 5 UARTs, ENC28J60 Ethernet + SPI link to SECONDARY (future)
#   "SECONDARY" - 5 UARTs, SPI link to PRIMARY, no Ethernet (future)
# ====================================================================================
set(DEVICE_MODE "SHARK" CACHE STRING "Device mode: SHARK, PRIMARY, or SECONDARY")

# Configure device mode
if(DEVICE_MODE STREQUAL "SHARK")
    message(STATUS "UART2ETH: Device mode = SHARK (2 UARTs, ENC28J60)")
    add_compile_definitions(DEVICE_MODE_SHARK)
elseif(DEVICE_MODE STREQUAL "PRIMARY")
    message(STATUS "UART2ETH: Device mode = PRIMARY (5 UARTs, ENC28J60, SPI link)")
    add_compile_definitions(DEVICE_MODE_PRIMARY)
elseif(DEVICE_MODE STREQUAL "SECONDARY")
    message(STATUS "UART2ETH: Device mode = SECONDARY (5 UARTs, SPI link)")
    add_compile_definitions(DEVICE_MODE_SECONDARY)
else()
    message(FATAL_ERROR "Invalid DEVICE_MODE: ${DEVICE_MODE}. Must be SHARK, PRIMARY, or SECONDARY")
endif()



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
    # OPTION 1: Full application (use when XOSC issue is resolved)
    src/main.c
    # OPTION 2: Minimal test for crt0.S debugging
    # src/main_minimal.c
    # DISABLED FOR TESTING - only enable when using uart2eth_board (not uart2eth_board_rosc)
    # src/xosc_workaround.c
    # src/xosc_test.c
)

# Add linker flags to wrap XOSC functions (prevent hanging on boards without crystal)
# DISABLED FOR TESTING - only enable when using uart2eth_board (not uart2eth_board_rosc)
# target_link_options(uart2eth PRIVATE 
#     "LINKER:--wrap=xosc_init"
#     "LINKER:--wrap=xosc_disable" 
#     "LINKER:--wrap=xosc_dormant"
#     "LINKER:--wrap=xosc_hz"
# )

# Source - https://stackoverflow.com/a
# Posted by dbanet, modified by community. See post 'Timeline' for change history
# Retrieved 2026-01-17, License - CC BY-SA 3.0
ADD_DEPENDENCIES (uart2eth timestamp)
INCLUDE_DIRECTORIES (${CMAKE_BINARY_DIR})

# Link libraries  
target_link_libraries(uart2eth 
    pico_stdlib        # Basic functionality
    hardware_gpio      # For LED control
    hardware_clocks    # For clock testing
    pico_multicore     # Dual core support
    pico_sync          # Synchronization primitives
    hardware_uart
    hardware_spi
    hardware_watchdog  # Watchdog for stability
    pico_lwip_nosys    # lwIP NO_SYS implementation for raw API
    config_manager     # Our config manager library
    log_manager        # Our log manager library
    state_machine      # Our event-driven state machine library
    network_manager    # Our network manager library (now with lwIP)
    tcp_socket_server  # all sockets are handled here
    http_server        # HTTP server for device information web interface
    ringbuffer         # Ring buffer for UART-TCP message bridging
    uart_manager       # New 4-channel UART manager
    core_modules       # Core0 and Core1 main functions
)

# Enable stdio: USB CDC for debugging, UART0 for debugging
# pico_enable_stdio_usb(uart2eth 1)
pico_enable_stdio_uart(uart2eth 1) 

# Set UF2 family for RP2350
pico_set_uf2_family(uart2eth "rp2350-arm-s")

# Embed partition table for flash isolation
# pico_embed_pt_in_binary(uart2eth "${CMAKE_CURRENT_SOURCE_DIR}/partition_table.json")

# Hash binary for TBYB (Try-Before-You-Buy)
pico_hash_binary(uart2eth)

# Create UF2 output
pico_add_extra_outputs(uart2eth)

# ====================================================================================
# OTA (Over-The-Air) Update Build Target
# ====================================================================================
# This target creates a firmware image specifically for OTA updates.
# It enables TBYB (Try-Before-You-Buy) which allows the bootloader to revert
# to the previous firmware if the new firmware fails to confirm itself.
# ====================================================================================

# Create OTA executable target
add_executable(uart2eth_ota
    src/main.c
)

# Source - https://stackoverflow.com/a
# Posted by dbanet, modified by community. See post 'Timeline' for change history
# Retrieved 2026-01-17, License - CC BY-SA 3.0
ADD_DEPENDENCIES(uart2eth_ota timestamp)
INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR})

# Link libraries (same as uart2eth)
target_link_libraries(uart2eth_ota
    pico_stdlib        # Basic functionality
    hardware_gpio      # For LED control
    hardware_clocks    # For clock testing
    pico_multicore     # Dual core support
    pico_sync          # Synchronization primitives
    hardware_uart
    hardware_spi
    hardware_watchdog  # Watchdog for stability
    pico_lwip_nosys    # lwIP NO_SYS implementation for raw API
    config_manager     # Our config manager library
    log_manager        # Our log manager library
    state_machine      # Our event-driven state machine library
    network_manager    # Our network manager library (now with lwIP)
    tcp_socket_server  # all sockets are handled here
    http_server        # HTTP server for device information web interface
    ringbuffer         # Ring buffer for UART-TCP message bridging
    uart_manager       # New 4-channel UART manager
    core_modules       # Core0 and Core1 main functions
)

# Enable TBYB (Try-Before-You-Buy) compile definition for OTA builds
target_compile_definitions(uart2eth_ota PRIVATE
    PICO_CRT0_IMAGE_TYPE_TBYB=1
)

# Enable stdio: UART0 for debugging (same as uart2eth)
pico_enable_stdio_uart(uart2eth_ota 1)

# Set UF2 family for RP2350 (same as uart2eth)
pico_set_uf2_family(uart2eth_ota "rp2350-arm-s")

# Embed partition table for flash isolation (same as uart2eth)
# pico_embed_pt_in_binary(uart2eth_ota "${CMAKE_CURRENT_SOURCE_DIR}/partition_table.json")

# Hash binary for TBYB verification
pico_hash_binary(uart2eth_ota)

# Create UF2 output
pico_add_extra_outputs(uart2eth_ota)

# ====================================================================================
# Factory Internal Build Target
# ====================================================================================
# This target is used for manufacturing to program factory defaults to devices.
# It enables write/erase functions in factory_defaults.c via FACTORY_INTERNAL_VERSION.
# ====================================================================================

# Create factory internal executable target (same as uart2eth but with FACTORY_INTERNAL_VERSION)
add_executable(uart2eth_factory_internal
    src/main.c
)

# Add factory internal version compile definition
target_compile_definitions(uart2eth_factory_internal PRIVATE
    FACTORY_INTERNAL_VERSION=1
)

# Source - https://stackoverflow.com/a
# Posted by dbanet, modified by community. See post 'Timeline' for change history
# Retrieved 2026-01-17, License - CC BY-SA 3.0
ADD_DEPENDENCIES(uart2eth_factory_internal timestamp)

# Link libraries (same as uart2eth)
target_link_libraries(uart2eth_factory_internal 
    pico_stdlib        # Basic functionality
    hardware_gpio      # For LED control
    hardware_clocks    # For clock testing
    pico_multicore     # Dual core support
    pico_sync          # Synchronization primitives
    hardware_uart
    hardware_spi
    hardware_watchdog  # Watchdog for stability
    pico_lwip_nosys    # lwIP NO_SYS implementation for raw API
    config_manager     # Our config manager library
    log_manager        # Our log manager library
    state_machine      # Our event-driven state machine library
    network_manager    # Our network manager library (now with lwIP)
    tcp_socket_server  # all sockets are handled here
    http_server        # HTTP server for device information web interface
    ringbuffer         # Ring buffer for UART-TCP message bridging
    uart_manager       # New 4-channel UART manager
    core_modules       # Core0 and Core1 main functions
)

# Enable stdio: UART0 for debugging (same as uart2eth)
pico_enable_stdio_uart(uart2eth_factory_internal 1)

# Set UF2 family for RP2350 (same as uart2eth)
pico_set_uf2_family(uart2eth_factory_internal "rp2350-arm-s")

# Embed partition table for flash isolation (same as uart2eth)
# pico_embed_pt_in_binary(uart2eth_factory_internal "${CMAKE_CURRENT_SOURCE_DIR}/partition_table.json")

# Create UF2 output
pico_add_extra_outputs(uart2eth_factory_internal)

