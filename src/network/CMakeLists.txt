# Network module CMakeLists.txt

# Create network library
add_library(network_manager
    enc28j60_driver.c
    network_manager.c
)

# Link required libraries
target_link_libraries(network_manager
    pico_stdlib
    hardware_spi
    hardware_gpio
    log_manager
)

# Include directories
target_include_directories(network_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)