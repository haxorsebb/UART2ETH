# HTTP Pages Module Build Configuration
# Documentation Reference: ADR-018 - HTTP Server Modularization

add_library(http_pages INTERFACE)

target_sources(http_pages INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/page_device.c
    ${CMAKE_CURRENT_LIST_DIR}/page_config.c
    ${CMAKE_CURRENT_LIST_DIR}/page_update.c
    ${CMAKE_CURRENT_LIST_DIR}/page_styles.c
)

# Conditional factory page compilation
if(FACTORY_INTERNAL_VERSION)
    target_sources(http_pages INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/page_factory.c
    )
endif()

target_include_directories(http_pages INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)
