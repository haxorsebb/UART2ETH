# Network tests CMakeLists.txt

# Create test executable for ENC28J60 driver
add_executable(test_enc28j60_driver
    test_enc28j60_driver.c
)

# Link required libraries
target_link_libraries(test_enc28j60_driver
    unity
    network_manager
    pico_stdlib
    hardware_spi
    hardware_gpio
    log_manager
    state_machine
    config_manager
    timer_subsystems
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_enc28j60_driver 1)
pico_enable_stdio_uart(test_enc28j60_driver 1)  

# Set UF2 family for RP2350
pico_set_uf2_family(test_enc28j60_driver "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_enc28j60_driver)

# Create test executable for network integration (ENC28J60 + lwIP + DHCP + ping)
# This test uses the actual production dual-core system
add_executable(test_network_integration
    test_network_integration.c
    ../../src/core1_main.c  # Include production Core1 implementation
    ../../src/core0_main.c  # Include production Core0 implementation
)

# Link required libraries including lwIP stack and multicore support
target_link_libraries(test_network_integration
    unity
    network_manager
    tcp_socket_server
    pico_stdlib
    pico_multicore        # Required for dual-core operation
    hardware_spi
    hardware_gpio
    log_manager
    state_machine
    config_manager
    timer_subsystems
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_network_integration 1)
pico_enable_stdio_uart(test_network_integration 1) 

# Set UF2 family for RP2350
pico_set_uf2_family(test_network_integration "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_network_integration)

# Create test executable for DHCP debug (focused debugging for receive issues)
add_executable(test_dhcp_debug
    test_dhcp_debug.c
)

# Link required libraries including lwIP stack
target_link_libraries(test_dhcp_debug
    unity
    network_manager
    tcp_socket_server
    pico_stdlib
    hardware_spi
    hardware_gpio
    log_manager
    state_machine
    config_manager
    timer_subsystems
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_dhcp_debug 1)
pico_enable_stdio_uart(test_dhcp_debug 0)  

# Set UF2 family for RP2350
pico_set_uf2_family(test_dhcp_debug "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_dhcp_debug)

# Create test executable for TCP Socket Server
add_executable(test_tcp_socket_server
    test_tcp_socket_server.c
)

# Link required libraries including lwIP stack
target_link_libraries(test_tcp_socket_server
    unity
    network_manager
    tcp_socket_server
    pico_stdlib
    pico_multicore      # Added: needed for state_machine doorbell functions
    hardware_spi
    hardware_gpio
    log_manager
    state_machine
    config_manager
    timer_subsystems
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_tcp_socket_server 1)
pico_enable_stdio_uart(test_tcp_socket_server 1)

# Set UF2 family for RP2350
pico_set_uf2_family(test_tcp_socket_server "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_tcp_socket_server)

# Create test executable for message framing
add_executable(test_message_framing
    test_message_framing.c
)

# Link required libraries
target_link_libraries(test_message_framing
    unity
    tcp_socket_server
    pico_stdlib
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_message_framing 1)
pico_enable_stdio_uart(test_message_framing 1)

# Set UF2 family for RP2350
pico_set_uf2_family(test_message_framing "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_message_framing)

# Create test executable for HTTP authentication
add_executable(test_http_authentication
    test_http_authentication.c
)

# Link required libraries including HTTP server
# Note: config_manager depends on network_manager, so network_manager is listed twice
# to resolve circular dependencies (config_manager uses network_manager symbols)
target_link_libraries(test_http_authentication
    unity
    http_server  # HTTP server library with authentication functions
    config_manager
    state_machine
    log_manager
    network_manager  # Listed after config_manager to resolve symbols
    timer_subsystems
    pico_stdlib
    hardware_spi
    hardware_gpio
    utils
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_http_authentication 1)
pico_enable_stdio_uart(test_http_authentication 1)

# Set UF2 family for RP2350
pico_set_uf2_family(test_http_authentication "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_http_authentication)

# Create test executable for HTTP server connection pool (ADR-018 Bug 4 / Bug 5)
add_executable(test_http_server
    test_http_server.c
)

# Same link set as test_http_authentication: http_server pulls in
# config_manager and network_manager, which reference each other.
target_link_libraries(test_http_server
    unity
    http_server
    config_manager
    state_machine
    log_manager
    network_manager
    timer_subsystems
    pico_stdlib
    hardware_spi
    hardware_gpio
    utils
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_http_server 1)
pico_enable_stdio_uart(test_http_server 1)

# Set UF2 family for RP2350
pico_set_uf2_family(test_http_server "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_http_server)

# Test executable for runtime network state display on the device page
# (arc42 Chapter 8, "Configured vs. Runtime Network State")
add_executable(test_device_page_network_status
    test_device_page_network_status.c
)

target_link_libraries(test_device_page_network_status
    unity
    http_server
    http_pages
    config_manager
    state_machine
    log_manager
    network_manager
    timer_subsystems
    pico_stdlib
    hardware_spi
    hardware_gpio
    utils
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_device_page_network_status 1)
pico_enable_stdio_uart(test_device_page_network_status 1)

# Set UF2 family for RP2350
pico_set_uf2_family(test_device_page_network_status "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_device_page_network_status)
