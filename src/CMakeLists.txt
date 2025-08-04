# Source files for UART2ETH firmware

# Configuration manager
add_library(config_manager
    config/shared_memory.c
)

target_include_directories(config_manager PUBLIC ../include)
target_link_libraries(config_manager 
    pico_stdlib
    pico_sync 
    hardware_sync
)

# Log manager
add_library(log_manager
    log/log_manager.c
)

target_include_directories(log_manager PUBLIC ../include)
target_link_libraries(log_manager 
    config_manager 
    pico_stdlib
    pico_sync 
    hardware_sync
)

# Main target will link against these libraries
