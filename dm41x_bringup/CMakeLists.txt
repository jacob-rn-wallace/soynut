cmake_minimum_required(VERSION 3.13)

# Standalone from ../firmware and ../lcd_bringup on purpose - no Nut CPU
# core, no ROM, no key bridge. Sole purpose: get anything showing on the
# physical Sharp Memory LCD over SPI, with correct polarity, and prove
# out the periodic-refresh idiom the real dm41x/ target will need later.
# See ../CLAUDE.md's "Sharp Memory LCD bring-up" section and main.c.

if (NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)
endif ()

set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(dm41x_bringup C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# third_party/pico_sharpmem_display/ is vendored, unmodified, LGPL-2.1 -
# see third_party/pico_sharpmem_display/README-VENDORED.md and
# ../CLAUDE.md's "Sharp Memory LCD bring-up" section. Hard rule: never
# edit anything under third_party/, same as ../emu41gcc/.
set(SHARPDISP_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/pico_sharpmem_display)

add_executable(dm41x_bringup
        main.c
        ${SHARPDISP_DIR}/src/sharpdisp.c
        ${SHARPDISP_DIR}/src/bitmap.c
)

target_include_directories(dm41x_bringup PRIVATE
        ${SHARPDISP_DIR}/include
)

# Power of 10, Rule 10: strict warnings for our own file only - scoped
# away from the vendored SHARPDISP_DIR sources the same way
# ../firmware/CMakeLists.txt scopes them away from emu41gcc/*.c (not ours
# to fix, and no reason to hold third-party code to a bar that turns an
# upstream warning into a build break we can't resolve without editing
# vendored source). set_property/APPEND rather than
# target_compile_options() - see ../firmware/CMakeLists.txt's identical
# note about vendored Pico SDK sources added to the same target.
#
# -UNDEBUG is for Rule 5: the Pico SDK's default build type is Release,
# which sets -DNDEBUG, and <assert.h> compiles every assert() to nothing
# under NDEBUG - see ../DEVIATIONS.md's "-UNDEBUG" implementation note.
set_property(SOURCE main.c APPEND PROPERTY COMPILE_OPTIONS
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG
)

target_link_libraries(dm41x_bringup
        pico_stdlib
        hardware_gpio
        hardware_spi
)

pico_enable_stdio_usb(dm41x_bringup 1)
pico_enable_stdio_uart(dm41x_bringup 0)

pico_add_extra_outputs(dm41x_bringup)
