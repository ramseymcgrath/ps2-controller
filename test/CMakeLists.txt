cmake_minimum_required(VERSION 3.13)
project(ps2_controller_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

# Include dirs for the pure modules under test.
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps2_device
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/status_led
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matrix
)

add_library(unity STATIC unity/unity.c)

# One executable per test file. More are added in later tasks.
add_executable(test_smoke test_smoke.c)
target_link_libraries(test_smoke unity)
add_test(NAME test_smoke COMMAND test_smoke)

add_executable(test_ds2_protocol test_ds2_protocol.c ../src/ps2_device/ds2_protocol.c)
target_link_libraries(test_ds2_protocol unity)
add_test(NAME test_ds2_protocol COMMAND test_ds2_protocol)

add_executable(test_gamepad_map test_gamepad_map.c ../src/input/gamepad_map.c)
target_link_libraries(test_gamepad_map unity)
add_test(NAME test_gamepad_map COMMAND test_gamepad_map)

add_executable(test_port_router test_port_router.c ../src/input/port_router.c)
target_link_libraries(test_port_router unity)
add_test(NAME test_port_router COMMAND test_port_router)

add_executable(test_matrix_render test_matrix_render.c ../src/matrix/matrix_render.c)
target_link_libraries(test_matrix_render unity)
add_test(NAME test_matrix_render COMMAND test_matrix_render)
