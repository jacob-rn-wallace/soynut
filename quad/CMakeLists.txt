cmake_minimum_required(VERSION 3.13)

# Phase 4 of the Magellan/QUAD plan
# (/Users/jake/.claude/plans/gentle-mapping-dewdrop.md) - the real
# firmware target for the QUAD-style 400x240 Sharp Memory LCD display,
# sibling to firmware/ (the 144x32 NHD14432 target) and quad_bringup/
# (the standalone display-only sandbox this target's SPI driver is
# vendored from). See CLAUDE.md's "Sharp Memory LCD bring-up" and "QUAD
# display bridge" sections for the full picture.

if (NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)
endif ()

# Pico 2 = RP2350. Must be set before project() - see firmware/CMakeLists.txt's
# identical note.
set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(quad C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# This target's sources live in four sibling directories/submodules,
# none of them copied: firmware/ (the shared Nut-CPU-emulator bridge/
# persist/key-bridge code, unchanged from the 144x32 target's own list,
# minus the display bridges that are specific to that target, plus the
# HP-IL controller/video-bridge/render files - see CLAUDE.md's "HP-IL
# video interface" section), emu41gcc/ (vendored Nut CPU core, same
# submodule firmware/ already uses), quad_bringup/'s vendored
# third_party/pico_sharpmem_display/ (the Sharp LCD SPI driver, already
# proven on real hardware there - see that directory's own
# README-VENDORED.md; hard "never edit" rule, same as emu41gcc/), and
# ilvideo-native/ (a separate repo, git submodule, same
# --recurse-submodules requirement as emu41gcc/ - see its own README
# and CLAUDE.md's "HP-IL video interface" section for the full
# provenance).
set(FIRMWARE_DIR ${CMAKE_CURRENT_LIST_DIR}/../firmware)
set(EMU41GCC_DIR ${CMAKE_CURRENT_LIST_DIR}/../emu41gcc)
set(ROMS_DIR ${CMAKE_CURRENT_LIST_DIR}/../roms)
set(FONT_TABLES_DIR ${CMAKE_CURRENT_LIST_DIR}/../font-tables)
set(SHARPDISP_DIR ${CMAKE_CURRENT_LIST_DIR}/../quad_bringup/third_party/pico_sharpmem_display)

# ilvideo-native (git submodule, ../ilvideo-native - a separate repo,
# see its own README) provides the HP-IL video interface's device +
# terminal logic (core/) and its shared bitmap font (font/) - this
# target's third view, alongside Stack and Classic-line above. See
# CLAUDE.md's "HP-IL video interface" section.
set(ILVIDEO_NATIVE_DIR ${CMAKE_CURRENT_LIST_DIR}/../ilvideo-native)

add_executable(quad
        main.c
        ${FIRMWARE_DIR}/hp41_quad_display_bridge.c
        ${FIRMWARE_DIR}/hp41_register_format.c
        ${FIRMWARE_DIR}/hp41_register_decode.c
        ${FIRMWARE_DIR}/hp41_ascii_decode.c
        ${FIRMWARE_DIR}/hp41_key_bridge.c
        ${FIRMWARE_DIR}/hp41_key_hold_bridge.c
        ${FIRMWARE_DIR}/hp41_persist_state.c
        ${FIRMWARE_DIR}/hp41_persist_flash.c
        ${FIRMWARE_DIR}/hp41_hpil_controller.c
        ${FIRMWARE_DIR}/hp41_hpil_video_bridge.c
        ${FIRMWARE_DIR}/hp41_hpil_video_render.c
        ${SHARPDISP_DIR}/src/sharpdisp.c
        ${SHARPDISP_DIR}/src/bitmap.c
        ${EMU41GCC_DIR}/nutcpu.c
        ${EMU41GCC_DIR}/display.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_stubs.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_globals.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_rom.c
        ${ROMS_DIR}/rom_images.c
        ${FONT_TABLES_DIR}/hp41_quad_font_table.c
        ${ILVIDEO_NATIVE_DIR}/core/hpil_device.c
        ${ILVIDEO_NATIVE_DIR}/core/ilvideo_term.c
        ${ILVIDEO_NATIVE_DIR}/core/ilvideo_device.c
        ${ILVIDEO_NATIVE_DIR}/font/ilvideo_font.c
)

target_include_directories(quad PRIVATE
        ${FIRMWARE_DIR}
        ${FIRMWARE_DIR}/emu41gcc_compat
        ${EMU41GCC_DIR}
        ${FONT_TABLES_DIR}
        ${SHARPDISP_DIR}/include
        ${ILVIDEO_NATIVE_DIR}/core
        ${ILVIDEO_NATIVE_DIR}/font
)

# The HP-IL module ROM (roms/HPIL.MOD -> roms/rom_images_hpil.c via
# roms/mod_to_c.py) is genuinely optional, unlike the base OS ROM above
# - "bring your own" per roms/README.md's "HP-IL module" section, and
# this target still builds and runs fine without it (just with nothing
# ever driving the HP-IL video interface's screen, since the base OS
# alone never touches HP-IL opcodes - see CLAUDE.md's "HP-IL video
# interface" section, "What's unconfirmed"). Wired in conditionally
# (CMake-time file-existence check, not a C preprocessor #ifdef in the
# shared logic - main.c's own use of HPIL_MODULE_AVAILABLE below is the
# one narrow, justified exception, same spirit as this project's other
# documented Rule 8 exceptions) so a build without roms/HPIL.MOD present
# doesn't break.
set(HPIL_MODULE_ROM ${ROMS_DIR}/rom_images_hpil.c)
if (EXISTS ${HPIL_MODULE_ROM})
    message(STATUS "quad: HP-IL module ROM found (${HPIL_MODULE_ROM}) - wiring it in")
    target_sources(quad PRIVATE
            ${FIRMWARE_DIR}/emu41gcc_compat/nut_rom_hpil.c
            ${HPIL_MODULE_ROM}
    )
    target_compile_definitions(quad PRIVATE HPIL_MODULE_AVAILABLE=1)
    set_source_files_properties(${FIRMWARE_DIR}/emu41gcc_compat/nut_rom_hpil.c PROPERTIES
            COMPILE_OPTIONS "-fcommon"
    )
    set_property(SOURCE ${FIRMWARE_DIR}/emu41gcc_compat/nut_rom_hpil.c APPEND PROPERTY COMPILE_OPTIONS
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
    )
else ()
    message(STATUS "quad: HP-IL module ROM not found (${HPIL_MODULE_ROM}) - building without it; see roms/README.md")
endif ()

# Same tabpage[]/tabbank[] common-symbol reasoning as
# firmware/CMakeLists.txt's identical block - see that file's own
# comment for the full explanation. Scoped to exactly the files that
# actually include nutcpu.h (confirmed by grep, not copied blindly from
# firmware/'s own list - this target doesn't compile
# hp41_elite_display_bridge.c/hp41_display_bridge.c/hp41_arduino_bridge.c
# at all, and hp41_quad_display_bridge.c/hp41_ascii_decode.c don't
# include nutcpu.h either).
set_source_files_properties(
        ${EMU41GCC_DIR}/display.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_globals.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_rom.c
        ${FIRMWARE_DIR}/hp41_register_decode.c
        ${FIRMWARE_DIR}/hp41_register_format.c
        ${FIRMWARE_DIR}/hp41_key_bridge.c
        ${FIRMWARE_DIR}/hp41_key_hold_bridge.c
        ${FIRMWARE_DIR}/hp41_persist_state.c
        main.c
        PROPERTIES COMPILE_OPTIONS "-fcommon"
)

set_source_files_properties(${EMU41GCC_DIR}/nutcpu.c PROPERTIES
        COMPILE_OPTIONS "-fcommon;-include${FIRMWARE_DIR}/emu41gcc_compat/nut_stubs.h"
)

# Power of 10, Rule 10: strict warnings for this project's own sources -
# see firmware/CMakeLists.txt's identical note for why set_property/
# APPEND rather than set_source_files_properties/target_compile_options.
# Deliberately excludes the vendored third_party/pico_sharpmem_display/
# sources (hard "never edit" black box - see that directory's own
# README-VENDORED.md and CLAUDE.md's "Sharp Memory LCD bring-up"
# section) the same way firmware/'s own list excludes emu41gcc/*.c.
set(QUAD_OWN_SOURCES
        main.c
        ${FIRMWARE_DIR}/hp41_quad_display_bridge.c
        ${FIRMWARE_DIR}/hp41_register_format.c
        ${FIRMWARE_DIR}/hp41_register_decode.c
        ${FIRMWARE_DIR}/hp41_ascii_decode.c
        ${FIRMWARE_DIR}/hp41_key_bridge.c
        ${FIRMWARE_DIR}/hp41_key_hold_bridge.c
        ${FIRMWARE_DIR}/hp41_persist_state.c
        ${FIRMWARE_DIR}/hp41_persist_flash.c
        ${FIRMWARE_DIR}/hp41_hpil_controller.c
        ${FIRMWARE_DIR}/hp41_hpil_video_bridge.c
        ${FIRMWARE_DIR}/hp41_hpil_video_render.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_stubs.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_globals.c
        ${FIRMWARE_DIR}/emu41gcc_compat/nut_rom.c
)
set_property(SOURCE ${QUAD_OWN_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG
)

target_link_libraries(quad
        pico_stdlib
        hardware_gpio
        hardware_uart
        hardware_flash
        hardware_spi
)

# USB (native, TinyUSB CDC) for keypress input - same as firmware/'s own
# choice, see that file's identical comment.
pico_enable_stdio_usb(quad 1)
pico_enable_stdio_uart(quad 0)

pico_add_extra_outputs(quad)
