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

# State machine (event-driven)
add_library(state_machine
    state_machine/state_machine.c
)

target_include_directories(state_machine PUBLIC ../include)
target_link_libraries(state_machine 
    pico_stdlib
    pico_sync 
    hardware_sync
)

# Core processing modules
add_library(core_modules
    core0_main.c
    core1_main.c
)

target_include_directories(core_modules PUBLIC ../include)
target_link_libraries(core_modules 
    state_machine
    config_manager
    log_manager
    pico_stdlib
    pico_multicore
    pico_sync
    hardware_sync
)

# Main target will link against these libraries