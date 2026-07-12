cmake_minimum_required(VERSION 3.13)

# Standalone from ../firmware on purpose - no Nut CPU core, no ROM, no key
# bridge, no Arduino bridge. Sole purpose: get anything showing on the
# physical LCD over the direct 3-wire serial link. See main.c.

if (NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)
endif ()

set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(lcd_bringup C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(lcd_bringup
        main.c
        st7920.c
)

# Power of 10, Rule 10: strict warnings for our own two files. Scoped
# with set_property/COMPILE_OPTIONS rather than target_compile_options -
# some Pico SDK "libraries" (e.g. pico_bootrom) add their sources
# directly to this target under the hood, and target_compile_options
# would apply to those vendored sources too (confirmed: it fails on
# pico-sdk's own bootrom.c under -Wpedantic, which isn't ours to fix).
set_property(SOURCE main.c st7920.c APPEND PROPERTY COMPILE_OPTIONS
        -Wall -Wextra -Wpedantic -Werror
)

target_link_libraries(lcd_bringup
        pico_stdlib
        hardware_gpio
)

pico_enable_stdio_usb(lcd_bringup 1)
pico_enable_stdio_uart(lcd_bringup 0)

pico_add_extra_outputs(lcd_bringup)
