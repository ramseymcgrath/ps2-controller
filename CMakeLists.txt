cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pimoroni_pico_plus2_w_rp2350 CACHE STRING "Board type")

# BluePad32 + BTstack must be located before pico_sdk_init().
# BluePad32 is vendored under external/. We use BluePad32's OWN bundled btstack
# (external/bluepad32/external/btstack) rather than the one shipped in the Pico
# SDK: BluePad32 4.2.0 calls the pre-v1.8 btstack HID-client API (hids_client_*),
# whereas the SDK's btstack is v1.8+ (renamed to hids_host_*). Pico SDK >=2.1's
# pico_btstack honors PICO_BTSTACK_PATH, so we override without patching the SDK.
set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/external/bluepad32)
set(BTSTACK_ROOT ${BLUEPAD32_ROOT}/external/btstack)
set(PICO_BTSTACK_PATH ${BTSTACK_ROOT})

include(pico_sdk_import.cmake)

project(ps2_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ps2_controller
    src/main.c
    src/input/bluepad32_platform.c
    src/input/gamepad_map.c
    src/input/port_router.c
    src/input/shared_input.c
    src/ps2_device/ps2_transport.c
    src/ps2_device/ds2_protocol.c
    src/ps2_device/ps2_device.c
    src/status_led/status_indicator.c
    src/matrix/matrix_driver.c
)

# Generate psxSPI.pio.h (PIO program + c-sdk helpers) for the transport.
pico_generate_pio_header(ps2_controller
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ps2_device/psxSPI.pio)

target_include_directories(ps2_controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ps2_device
    ${CMAKE_CURRENT_SOURCE_DIR}/src/status_led
    ${CMAKE_CURRENT_SOURCE_DIR}/src/matrix
    ${BLUEPAD32_ROOT}/src/components/bluepad32/include
)

# btstack_config.h / sdkconfig.h (in src/) must be findable by libbluepad32.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include)
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include)
include_directories(${BTSTACK_ROOT}/src)

target_link_libraries(ps2_controller PUBLIC
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_cyw43
    pico_multicore
    hardware_pio
    hardware_timer
    hardware_clocks
    hardware_i2c
    hardware_dma
    bluepad32
)

add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)

pico_enable_stdio_usb(ps2_controller 1)
pico_enable_stdio_uart(ps2_controller 0)
pico_add_extra_outputs(ps2_controller)
