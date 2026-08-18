/**
 * @file hp41_ascii_decode.h
 * @brief Decodes one raw HP-41 display register code to an ASCII character.
 *
 * Extracted out of hp41_display_bridge.c/.h (Phase 4 of the Magellan/
 * DM41X plan - see /Users/jake/.claude/plans/gentle-mapping-dewdrop.md):
 * the new dm41x/ firmware target's classic-line view
 * (hp41_dm41x_display_bridge.c) needs this exact decode too, but linking
 * the whole of hp41_display_bridge.c to get it would drag in
 * hp41_display_render()'s dependency on st7920.c and the old 144x32
 * font tables - exactly the 144x32-specific coupling this new target is
 * supposed to drop. Same "small, hardware-agnostic module" treatment
 * hp41_register_decode.h/.c already got for the same reason.
 */
#ifndef SOYNUT_HP41_ASCII_DECODE_H
#define SOYNUT_HP41_ASCII_DECODE_H

/**
 * @brief Decode one HP-41 raw display register code to an ASCII character.
 *
 * Re-derived from emu41gcc/display.c's static alpha41() (rather than
 * exposing that static function, which would mean touching the vendored
 * file) because this exact decode is what's already validated correct:
 * it's what produced "MEMORY LOST" via display_to_buf() in the first
 * Nut CPU boot test (see CLAUDE.md, tests/nut_smoke_test.c).
 *
 * @param v Raw HP-41 display code for one cell:
 *          (lcd_c[i]<<8) | ((lcd_b[i]&3)<<4) | lcd_a[i].
 * @return The decoded ASCII character code (0-127).
 */
int hp41_decode_ascii(int v);

#endif // SOYNUT_HP41_ASCII_DECODE_H
