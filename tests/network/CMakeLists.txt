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
pico_enable_stdio_uart(test_enc28j60_driver 0)  # Disable UART0 to avoid SPI0 conflict

# Set UF2 family for RP2350
pico_set_uf2_family(test_enc28j60_driver "rp2350-arm-s")

# Create UF2 output
pico_add_extra_outputs(test_enc28j60_driver)