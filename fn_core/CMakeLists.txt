add_library(a2pico_fn_core INTERFACE)

target_sources(a2pico_fn_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/slip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config_ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport_tcp.c
)

target_include_directories(a2pico_fn_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/net
)
