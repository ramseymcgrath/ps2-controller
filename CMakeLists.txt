cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)

project(ps2_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ps2_controller
    src/main.c
)

target_link_libraries(ps2_controller PUBLIC
    pico_stdlib
)

pico_enable_stdio_usb(ps2_controller 1)
pico_enable_stdio_uart(ps2_controller 0)
pico_add_extra_outputs(ps2_controller)
