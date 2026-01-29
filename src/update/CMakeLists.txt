# Update module CMakeLists.txt
# Documentation Reference: ADR-017 - Update Module Architecture

add_library(update_manager
    update_manager.c
)

target_include_directories(update_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/update
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/log
)

target_link_libraries(update_manager
    pico_stdlib
    pico_bootrom
    pico_flash
    pico_sync
    hardware_flash
    hardware_sync
    hardware_watchdog
    log_manager
    boot_uf2_headers
)
