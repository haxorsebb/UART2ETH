# Update module CMakeLists.txt
# Documentation Reference:
# - ADR-017 - Update Module Architecture
# - ADR-019 - Network Configuration Changes Take Effect at Boot (deferred_reboot)

add_library(update_manager
    update_manager.c
    deferred_reboot.c
)

target_include_directories(update_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/update
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/log
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/state_machine
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
    state_machine       # deferred_reboot posts MAIN_EVENT_REBOOT_REQUESTED
    timer_subsystems    # deferred_reboot uses CORE1_TIMER_REBOOT_GRACE
    boot_uf2_headers
)
