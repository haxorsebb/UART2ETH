# Source files for UART2ETH firmware

if (NOT TARGET hardware_sha256)
    return()
endif()

# Configuration manager
add_library(config_manager
    config/shared_memory.c
    config/flash_persistence.c
    config/uart_config_protocol.c
)

target_include_directories(config_manager PUBLIC ../include)
target_include_directories(config_manager PUBLIC ../include/config)
target_include_directories(config_manager PUBLIC ../include/log)
target_link_libraries(config_manager 
    pico_stdlib
    pico_sha256
    pico_sync 
    hardware_sync
    hardware_flash
    hardware_watchdog
    pico_bootrom
    utils
    boot_uf2_headers
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
target_include_directories(ringbuffer PUBLIC ../include/ringbuffer)
target_include_directories(ringbuffer PUBLIC ../include/config)
target_include_directories(ringbuffer PUBLIC ../include/log)
target_include_directories(ringbuffer PUBLIC ../include/state_machine)

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
target_include_directories(state_machine PUBLIC ../include/state_machine)
target_include_directories(state_machine PUBLIC ../include/config)
target_include_directories(state_machine PUBLIC ../include/log)

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
target_include_directories(timer_subsystems PUBLIC ../include/state_machine)
target_include_directories(timer_subsystems PUBLIC ../include/log)

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
    http_server
    timer_subsystems
    ringbuffer
    uart_manager
    pico_stdlib
    pico_multicore
    pico_sync
    hardware_sync
)

# UART Manager - 4-channel UART management (includes PIO UART for Issue #82 and Channel 3)
add_library(uart_manager
    uart/uart_manager.c
    uart/pl011_driver.c
    uart/pio_uart_driver.c
    uart/pio_uart_channel3_driver.c
    uart/pio_uart_channel4_driver.c
    uart/uart_receive_buffer.c
)


# Generate PIO headers for PIO UART implementation
pico_generate_pio_header(uart_manager ${CMAKE_CURRENT_LIST_DIR}/uart/pio_uart_tx.pio)
pico_generate_pio_header(uart_manager ${CMAKE_CURRENT_LIST_DIR}/uart/pio_uart_rx.pio)

target_include_directories(uart_manager PUBLIC ../include)
target_link_libraries(uart_manager 
    ringbuffer
    log_manager
    config_manager
    pico_stdlib
    hardware_uart
    hardware_gpio
    hardware_irq
    hardware_pio
    hardware_dma
    pico_sync
)


# utils - bits and pieces from the pico sdk and examples
add_library(utils
    utils/uf2_family_ids.c
)
target_include_directories(utils PUBLIC ../include)
target_link_libraries(utils 
    pico_bootrom
    pico_stdlib
    hardware_flash
    boot_uf2_headers
)

# Add network subdirectory
add_subdirectory(network)

# Main target will link against these libraries