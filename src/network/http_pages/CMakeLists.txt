# HTTP Pages Module Build Configuration
# Documentation Reference: ADR-018 - HTTP Server Modularization

# Standard http_pages library (production build)
add_library(http_pages INTERFACE)

target_sources(http_pages INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/page_device.c
    ${CMAKE_CURRENT_LIST_DIR}/page_config.c
    ${CMAKE_CURRENT_LIST_DIR}/page_update.c
    ${CMAKE_CURRENT_LIST_DIR}/page_styles.c
)

target_include_directories(http_pages INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)

# Factory internal version (includes page_factory.c)
add_library(http_pages_factory_internal INTERFACE)

target_sources(http_pages_factory_internal INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/page_device.c
    ${CMAKE_CURRENT_LIST_DIR}/page_config.c
    ${CMAKE_CURRENT_LIST_DIR}/page_update.c
    ${CMAKE_CURRENT_LIST_DIR}/page_styles.c
    ${CMAKE_CURRENT_LIST_DIR}/page_factory.c
)

target_include_directories(http_pages_factory_internal INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(http_pages_factory_internal INTERFACE
    FACTORY_INTERNAL_VERSION=1
)
