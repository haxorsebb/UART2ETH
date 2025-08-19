# Source files for UART2ETH firmware

# Configuration manager
add_library(config_manager
    config/shared_memory.c
    config/flash_persistence.c
)

target_include_directories(config_manager PUBLIC ../include)
target_link_libraries(config_manager 
    pico_stdlib
    pico_sync 
    hardware_sync
    hardware_flash
    pico_bootrom
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

# Ring buffer for UART-TCP message bridging
add_library(ringbuffer
    ringbuffer/ringbuffer.c
)

target_include_directories(ringbuffer PUBLIC ../include)
target_link_libraries(ringbuffer 
    pico_stdlib
    pico_sync
    pico_multicore
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

# Timer subsystems (per-core hardware timer alarms)
add_library(timer_subsystems
    core0_timer.c
    core1_timer.c
)

target_include_directories(timer_subsystems PUBLIC ../include)
target_link_libraries(timer_subsystems 
    pico_stdlib
    hardware_timer
    hardware_irq
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
    network_manager
    tcp_socket_server
    timer_subsystems
    ringbuffer
    pico_stdlib
    pico_multicore
    pico_sync
    hardware_sync
)

# Add network subdirectory
add_subdirectory(network)

# Main target will link against these libraries