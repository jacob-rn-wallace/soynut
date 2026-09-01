/**
 * @file nut_rom_hpil.h
 * @brief Wires the optional HP82160A HP-IL Module ROM into
 *        tabpage[]/typmod[] at the pages it declares.
 *
 * Source: roms/HPIL.MOD (a real MOD1-format module file - see
 * roms/README.md's "HP-IL module" section), converted by
 * roms/mod_to_c.py into roms/rom_images_hpil.c. Both are gitignored,
 * "bring your own" - same pattern as the base OS ROM
 * (roms/rom_images.c) nut_rom.c/nut_boot() wires in.
 *
 * Deliberately a separate file from nut_rom.c/nut_boot(): those are
 * compiled into every target (firmware/, quad/, tests/, tools/, sim/),
 * so referencing roms/rom_images_hpil.c's arrays from there would force
 * every single one of those targets to also link that file, breaking
 * any target that doesn't have roms/HPIL.MOD available. Only targets
 * that actually want this module (currently: quad/, since that's where
 * the HP-IL video interface actually renders - see CLAUDE.md's "HP-IL
 * video interface" section - and tools/hpil_opcode_trace.c, to trace
 * real ROM behavior with it present) compile+link this file at all.
 */
#pragma once

/**
 * @brief Wire the HP-IL module's two ROM pages into tabpage[]/typmod[]
 *        at their declared pages (6 and 7).
 *
 * Call once, after nut_boot() (nut_boot() doesn't touch pages 6/7
 * itself, so call order relative to it doesn't actually matter, but
 * "after" mirrors how a real module would be plugged in after the
 * calculator's own OS is already resident).
 */
void nut_rom_wire_hpil_module(void);
