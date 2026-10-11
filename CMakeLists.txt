cmake_minimum_required(VERSION 3.13...3.27)

set(PROJECT_NAME a2picoFN)
set(VERSION "2026-10-11" CACHE STRING "Firmware release date/version")

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
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

set_source_files_properties(incbin.S OBJECT_DEPENDS firmware.rom)
add_custom_command(
        COMMAND cl65 -t apple2 -C apple2-asm.cfg
                        ${CMAKE_CURRENT_SOURCE_DIR}/6502/SSC.S
                     -o ${CMAKE_CURRENT_BINARY_DIR}/firmware.rom
        MAIN_DEPENDENCY 6502/SSC.S
        OUTPUT firmware.rom
        DEPENDS 6502/SSC.CN00.S 6502/SSC.C800.S 6502/SSC.HILEV.S 6502/SSC.TERM.S
                6502/SSC.CORE.S 6502/SSC.UTIL.S 6502/SSC.CMD.S   6502/SSC.CF00.S
                6502/SMARTPORT.S
        VERBATIM
        )

target_sources(${TARGET_NAME} PRIVATE
        board.c
        gen.c
        incbin.S
        main.c
        slip.c
        sp.c
        transport_cdc.c
        usb_descriptors.c
        )

target_link_libraries(${TARGET_NAME} PRIVATE
        a2pico
        pico_multicore
        pico_stdlib
        pico_unique_id
        tinyusb_device
        )

if(PICO_BOARD STREQUAL "a2pico2litew")
    target_link_libraries(${TARGET_NAME} PRIVATE pico_cyw43_arch_poll)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        PICO_CYW43_SUPPORTED=1
        CYW43_LWIP=0
        CYW43_USE_SPI=1
        USE_SDIOIT=0
        CYW43_WL_GPIO_LED_PIN=0
    )
endif()
