cmake_minimum_required(VERSION 3.13...3.27)

set(PROJECT_NAME a2picoFN)
set(VERSION "2026-11-21" CACHE STRING "Firmware release date/version")
set(NET_HOSTNAME "a2picofn" CACHE STRING "DHCP hostname")

if(NOT DEFINED PICO_BOARD OR "${PICO_BOARD}" STREQUAL "")
    set(PICO_BOARD "a2pico2" CACHE STRING "Target board (a2pico2 or a2pico2litew)" FORCE)
endif()

# Validate supported PICO_BOARD
if(NOT PICO_BOARD STREQUAL "a2pico2" AND NOT PICO_BOARD STREQUAL "a2pico2litew")
    message(FATAL_ERROR "Unsupported or missing PICO_BOARD '${PICO_BOARD}'. Supported boards: a2pico2, a2pico2litew")
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

add_subdirectory(fn_core)

execute_process(
    COMMAND git describe --tags
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
message(STATUS "GIT_TAG is ${GIT_TAG}")

# =============================================================================
# 6502 Expansion ROM Pipeline (6502/FIRMWARE.S -> firmware.rom)
# =============================================================================
find_program(CL65_BIN cl65 PATHS /Users/oshuno/bin REQUIRED)

set_source_files_properties(incbin.S OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/firmware.rom)
add_custom_command(
    COMMAND ${CL65_BIN} -t apple2 -C apple2-asm.cfg
            ${CMAKE_CURRENT_SOURCE_DIR}/6502/FIRMWARE.S
            -o ${CMAKE_CURRENT_BINARY_DIR}/firmware.rom
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/6502/FIRMWARE.S
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/firmware.rom
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/6502/SSC.CN00.S
        ${CMAKE_CURRENT_SOURCE_DIR}/6502/SSC.CF00.S
        ${CMAKE_CURRENT_SOURCE_DIR}/6502/SMARTPORT.S
        ${CMAKE_CURRENT_SOURCE_DIR}/6502/CONNECT.S
        ${CMAKE_CURRENT_SOURCE_DIR}/6502/CONFIG.S
    VERBATIM
)
add_custom_target(firmware_rom DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/firmware.rom)

# =============================================================================
# Firmware Variant Target: a2picoFN-Wifi-<VERSION> (Default Wi-Fi Variant)
# =============================================================================
set(WIFI_TARGET_NAME "${PROJECT_NAME}-Wifi-${VERSION}")

add_executable(${WIFI_TARGET_NAME}
    main.c
    board.c
    incbin.S
    infra/net/net_mgr.c
    infra/wifi/wifi_mgr.c
)
add_dependencies(${WIFI_TARGET_NAME} firmware_rom)

target_compile_definitions(${WIFI_TARGET_NAME} PRIVATE
    GIT_TAG="${GIT_TAG}"
    A2PICO_NET_BACKEND_WIFI=1
    A2PICO_NET_HOSTNAME="${NET_HOSTNAME}"
    PICO_CYW43_SUPPORTED=1
    CYW43_LWIP=1
    CYW43_USE_SPI=1
    USE_SDIOIT=0
    CYW43_WL_GPIO_LED_PIN=0
    CYW43_PIO_CLOCK_DIV=3
)

target_include_directories(${WIFI_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/net
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/wifi
)

target_link_libraries(${WIFI_TARGET_NAME} PRIVATE
    a2pico_fn_core
    a2pico
    pico_multicore
    pico_stdlib
    pico_unique_id
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_flash
    hardware_sync
)

if(DEFINED WIFI_SSID)
    target_compile_definitions(${WIFI_TARGET_NAME} PRIVATE WIFI_SSID="${WIFI_SSID}")
endif()
if(DEFINED WIFI_PASSWORD)
    target_compile_definitions(${WIFI_TARGET_NAME} PRIVATE WIFI_PASSWORD="${WIFI_PASSWORD}")
endif()
if(DEFINED FUJINET_PC_PORT)
    target_compile_definitions(${WIFI_TARGET_NAME} PRIVATE FUJINET_PC_PORT=${FUJINET_PC_PORT})
endif()

pico_enable_stdio_usb(${WIFI_TARGET_NAME} 0)
pico_enable_stdio_uart(${WIFI_TARGET_NAME} 0)
pico_add_extra_outputs(${WIFI_TARGET_NAME})

# =============================================================================
# Firmware Variant Target: a2picoFN-Ether-<VERSION> (W5500 Ethernet Variant)
# =============================================================================
set(ETHER_TARGET_NAME "${PROJECT_NAME}-Ether-${VERSION}")

add_executable(${ETHER_TARGET_NAME}
    main.c
    board.c
    incbin.S
    infra/net/net_mgr.c
    infra/ether/w5500_drv.c
    infra/ether/ether_netif.c
)
add_dependencies(${ETHER_TARGET_NAME} firmware_rom)

target_compile_definitions(${ETHER_TARGET_NAME} PRIVATE
    GIT_TAG="${GIT_TAG}"
    A2PICO_NET_BACKEND_ETHER=1
    A2PICO_NET_HOSTNAME="${NET_HOSTNAME}"
)

target_include_directories(${ETHER_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/net
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/ether
)

target_link_libraries(${ETHER_TARGET_NAME} PRIVATE
    a2pico_fn_core
    a2pico
    pico_multicore
    pico_stdlib
    pico_unique_id
    pico_lwip_nosys
    hardware_spi
    hardware_flash
    hardware_sync
)

if(DEFINED FUJINET_PC_PORT)
    target_compile_definitions(${ETHER_TARGET_NAME} PRIVATE FUJINET_PC_PORT=${FUJINET_PC_PORT})
endif()

pico_enable_stdio_usb(${ETHER_TARGET_NAME} 0)
pico_enable_stdio_uart(${ETHER_TARGET_NAME} 0)
pico_add_extra_outputs(${ETHER_TARGET_NAME})

# =============================================================================
# Firmware Variant Target: a2picoFN-UsbEcm-<VERSION> (USB CDC-ECM Virtual Ethernet)
# =============================================================================
set(USB_ECM_TARGET_NAME "${PROJECT_NAME}-UsbEcm-${VERSION}")

add_executable(${USB_ECM_TARGET_NAME}
    main.c
    board.c
    incbin.S
    infra/net/net_mgr.c
    infra/usb_ecm/usb_ecm_netif.c
)
add_dependencies(${USB_ECM_TARGET_NAME} firmware_rom)

target_compile_definitions(${USB_ECM_TARGET_NAME} PRIVATE
    GIT_TAG="${GIT_TAG}"
    A2PICO_NET_BACKEND_USB_ECM=1
    A2PICO_NET_HOSTNAME="${NET_HOSTNAME}"
)

target_include_directories(${USB_ECM_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/usb_ecm
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/net
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PICO_SDK_PATH}/lib/tinyusb/lib/networking
)

target_link_libraries(${USB_ECM_TARGET_NAME} PRIVATE
    a2pico_fn_core
    a2pico
    pico_multicore
    pico_stdlib
    pico_unique_id
    tinyusb_device
    pico_lwip_nosys
    hardware_flash
    hardware_sync
)

if(DEFINED FUJINET_PC_PORT)
    target_compile_definitions(${USB_ECM_TARGET_NAME} PRIVATE FUJINET_PC_PORT=${FUJINET_PC_PORT})
endif()

pico_enable_stdio_usb(${USB_ECM_TARGET_NAME} 0)
pico_enable_stdio_uart(${USB_ECM_TARGET_NAME} 0)
pico_add_extra_outputs(${USB_ECM_TARGET_NAME})
