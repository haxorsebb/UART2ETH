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
    pico_stdlib
    pico_multicore        # Required for dual-core operation
    hardware_spi
    hardware_gpio
    log_manager
    state_machine
    config_manager
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
    pico_stdlib
    hardware_spi
    hardware_gpio
    log_manager
    state_machine
    config_manager
)

# Enable dual stdio for test output
pico_enable_stdio_usb(test_dhcp_debug 1)
pico_enable_stdio_uart(test_dhcp_debug 0)  

# Set UF2 family for RP2350
pico_set_uf2_family(test_dhcp_debug "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_dhcp_debug)
