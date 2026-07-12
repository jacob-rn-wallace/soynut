cmake_minimum_required(VERSION 3.13)

# Default to the pico-sdk checkout that lives alongside this project unless
# the caller already pointed PICO_SDK_PATH somewhere else.
if (NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)
endif ()

# Pico 2 = RP2350. Override with -DPICO_BOARD=... on the command line if
# you're bringing this up on a different board first. Must be set before
# project() — the SDK resolves the board/platform during that call.
set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(soynut C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Nut CPU core (emu41gcc, vendored unmodified in ../emu41gcc) plus the
# compat shims in emu41gcc_compat/ that let its DOS-era C build cleanly
# here without editing a single line of the vendored source - see
# emu41gcc_compat/*.h for what each shim is standing in for.
set(EMU41GCC_DIR ${CMAKE_CURRENT_LIST_DIR}/../emu41gcc)
set(ROMS_DIR ${CMAKE_CURRENT_LIST_DIR}/../roms)
set(FONT_TABLES_DIR ${CMAKE_CURRENT_LIST_DIR}/../font-tables)

add_executable(soynut
        main.c
        st7920.c
        bitmaps.c
        hp41_display_bridge.c
        hp41_key_bridge.c
        hp41_key_hold_bridge.c
        hp41_arduino_bridge.c
        ${EMU41GCC_DIR}/nutcpu.c
        ${EMU41GCC_DIR}/display.c
        emu41gcc_compat/nut_stubs.c
        emu41gcc_compat/nut_globals.c
        emu41gcc_compat/nut_rom.c
        ${ROMS_DIR}/rom_images.c
        ${FONT_TABLES_DIR}/hp41_display_tables.c
)

target_include_directories(soynut PRIVATE
        emu41gcc_compat
        ${EMU41GCC_DIR}
        ${FONT_TABLES_DIR}
)

# nutcpu.h defines tabpage[16]/tabbank[16][4] as plain (non-extern) globals,
# relying on old-style "common symbol" merging across every .c file that
# includes it (nutcpu.c, display.c, nut_globals.c) - legal pre-C23 and how
# every DOS-era compiler behaved, but GCC has defaulted to -fno-common
# since GCC 10, which turns each into a strong definition and makes the
# link fail with "multiple definition of `tabpage'". -fcommon restores the
# old merging behavior, again without touching the vendored header.
set_source_files_properties(
        ${EMU41GCC_DIR}/display.c
        emu41gcc_compat/nut_globals.c
        emu41gcc_compat/nut_rom.c
        hp41_key_bridge.c
        hp41_key_hold_bridge.c
        main.c
        PROPERTIES COMPILE_OPTIONS "-fcommon"
)

# nutcpu.c needs both: -fcommon for the same tabpage/tabbank reason above,
# and a forced include of nut_stubs.h for the HP82143 printer entry points
# (print_char, etc.) that upstream calls from execp() but never declared
# in any header.
set_source_files_properties(${EMU41GCC_DIR}/nutcpu.c PROPERTIES
        COMPILE_OPTIONS "-fcommon;-include${CMAKE_CURRENT_LIST_DIR}/emu41gcc_compat/nut_stubs.h"
)

# Power of 10, Rule 10: compile our own code with the most pedantic
# warnings available and treat them as build failures. Deliberately
# scoped away from emu41gcc/*.c (vendored, unmodifiable - see
# DEVIATIONS.md and CLAUDE.md's "The Nut CPU core") and the generated
# data tables (roms/rom_images.c, font-tables/hp41_display_tables.c),
# neither of which we can or need to hold to this bar. set_property's
# APPEND is required here, not set_source_files_properties, since that
# would silently clobber the -fcommon options already set above for
# several of these same files.
set(SOYNUT_OWN_SOURCES
        main.c
        st7920.c
        bitmaps.c
        hp41_display_bridge.c
        hp41_key_bridge.c
        hp41_key_hold_bridge.c
        hp41_arduino_bridge.c
        emu41gcc_compat/nut_stubs.c
        emu41gcc_compat/nut_globals.c
        emu41gcc_compat/nut_rom.c
)
set_property(SOURCE ${SOYNUT_OWN_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
        -Wall -Wextra -Wpedantic -Werror
)

target_link_libraries(soynut
        pico_stdlib
        hardware_gpio
        hardware_uart
)

# USB (native, TinyUSB CDC) for keypress input - separate from the
# hardware UART peripheral used below for the Arduino display bridge
# link, so both coexist without conflict.
pico_enable_stdio_usb(soynut 1)
pico_enable_stdio_uart(soynut 0)

pico_add_extra_outputs(soynut)
