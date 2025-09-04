# Network module CMakeLists.txt

# Create network library
add_library(network_manager
    enc28j60_driver.c
    network_manager.c
    lwip_netif_enc28j60.c  # lwIP network interface for ENC28J60
)

# Link required libraries including lwIP
target_link_libraries(network_manager
    pico_stdlib
    hardware_spi
    hardware_gpio
    log_manager
    pico_lwip_nosys     # lwIP NO_SYS implementation for raw API
)

# Include directories
target_include_directories(network_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Create TCP socket server library
add_library(tcp_socket_server
    tcp_socket_server.c
    multi_tcp_server.c
)

# Link required libraries including lwIP
target_link_libraries(tcp_socket_server
    pico_stdlib
    log_manager
    pico_lwip_nosys     # lwIP NO_SYS implementation for raw API
)

# Include directories
target_include_directories(tcp_socket_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)