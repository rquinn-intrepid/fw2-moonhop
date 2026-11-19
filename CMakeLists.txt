cmake_minimum_required(VERSION 3.13)

# FreeWili 2 board (RP2350B) from the pinned BSP. Never pass -DPICO_BOARD on
# the command line (wilibsp/AGENTS.md, invariant 1).
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/boards")

# Settings the wilibsp top-level CMakeLists normally provides to bsp/.
option(FW2_AGENTIO "Build the agentio remote-control/capture harness" ON)
set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/third_party/Pico-PIO-USB")

include(pico_sdk_import.cmake)
project(fw2_moonhop C CXX ASM)
set(CMAKE_C_STANDARD 11)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
pico_sdk_init()

add_subdirectory(wilibsp/bsp)

add_executable(moonhop src/main.c src/mh_game.c)
target_link_libraries(moonhop PRIVATE freewili2_bsp)
fw2_display_app(moonhop
    POWER_ZONES DISPLAY SENSORS AUDIO RGB_LEDS
    VERSION 002
    DESCRIPTION "Vector moon landing game: tilt or turn to rotate, gray thrust, red emergency burst"
    REPOSITORY "https://github.com/rquinn-intrepid/fw2-moonhop")
