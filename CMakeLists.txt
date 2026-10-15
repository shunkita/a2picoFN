cmake_minimum_required(VERSION 3.13...3.27)

set(PROJECT_NAME a2picoFN)
set(VERSION "2026-10-15" CACHE STRING "Firmware release date/version")

# Validate supported PICO_BOARD and set board suffix
if(PICO_BOARD STREQUAL "a2pico2litew")
    set(BOARD_SUFFIX "A2Pico2LiteW")
elseif(PICO_BOARD STREQUAL "a2pico2-generic")
    set(BOARD_SUFFIX "A2Pico2-generic")
else()
    message(FATAL_ERROR "Unsupported or missing PICO_BOARD '${PICO_BOARD}'. Supported boards: a2pico2litew, a2pico2-generic")
endif()

include(FetchContent)
FetchContent_Declare(a2pico
        GIT_REPOSITORY https://github.com/oliverschmidt/a2pico.git
        GIT_TAG main
        )
FetchContent_MakeAvailable(a2pico)

include(pico_sdk_import.cmake)
project(${PROJECT_NAME} C CXX ASM)
pico_sdk_init()

set(TARGET_NAME "${PROJECT_NAME}_${BOARD_SUFFIX}_${VERSION}")

add_executable(${TARGET_NAME})
pico_add_extra_outputs(${TARGET_NAME})


execute_process(
        COMMAND git describe --tags
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )
message("GIT_TAG is ${GIT_TAG}")
target_compile_definitions(${TARGET_NAME} PRIVATE GIT_TAG="${GIT_TAG}")

# Make sure TinyUSB can find tusb_config.h
target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

set_source_files_properties(incbin.S OBJECT_DEPENDS firmware.rom)
add_custom_command(
        COMMAND cl65 -t apple2 -C apple2-asm.cfg
                        ${CMAKE_CURRENT_SOURCE_DIR}/6502/FIRMWARE.S
                     -o ${CMAKE_CURRENT_BINARY_DIR}/firmware.rom
        MAIN_DEPENDENCY 6502/FIRMWARE.S
        OUTPUT firmware.rom
        DEPENDS 6502/SSC.CN00.S 6502/SSC.CF00.S
                6502/SMARTPORT.S 6502/CONNECT.S 6502/CONFIG.S
        VERBATIM
        )

target_sources(${TARGET_NAME} PRIVATE
        board.c
        gen.c
        incbin.S
        main.c
        slip.c
        sp.c
        transport_wifi.c
        flash_config.c
        config_ui.c
        )

target_link_libraries(${TARGET_NAME} PRIVATE
        a2pico
        pico_multicore
        pico_stdlib
        pico_unique_id
        hardware_flash
        )

pico_enable_stdio_usb(${TARGET_NAME} 0)
pico_enable_stdio_uart(${TARGET_NAME} 0)

if(PICO_BOARD STREQUAL "a2pico2litew")
    target_link_libraries(${TARGET_NAME} PRIVATE pico_cyw43_arch_lwip_poll)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        PICO_CYW43_SUPPORTED=1
        CYW43_LWIP=1
        CYW43_USE_SPI=1
        USE_SDIOIT=0
        CYW43_WL_GPIO_LED_PIN=0
    )
endif()

if(DEFINED WIFI_SSID)
    target_compile_definitions(${TARGET_NAME} PRIVATE WIFI_SSID="${WIFI_SSID}")
endif()
if(DEFINED WIFI_PASSWORD)
    target_compile_definitions(${TARGET_NAME} PRIVATE WIFI_PASSWORD="${WIFI_PASSWORD}")
endif()
if(DEFINED FUJINET_PC_IP)
    target_compile_definitions(${TARGET_NAME} PRIVATE FUJINET_PC_IP="${FUJINET_PC_IP}")
endif()
if(DEFINED FUJINET_PC_PORT)
    target_compile_definitions(${TARGET_NAME} PRIVATE FUJINET_PC_PORT=${FUJINET_PC_PORT})
endif()
