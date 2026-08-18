/**
 * @file hp41_display_bridge.h
 * @brief Renders emu41gcc's Nut CPU LCD shift registers into a real
 *        ST7920 framebuffer, and optionally pushes it to hardware.
 */
#ifndef SOYNUT_HP41_DISPLAY_BRIDGE_H
#define SOYNUT_HP41_DISPLAY_BRIDGE_H

#include <stdint.h>

/* hp41_decode_ascii() used to live here - extracted into its own
 * hardware-agnostic module, hp41_ascii_decode.h/.c (Phase 4 of the
 * Magellan/QUAD plan), since the new quad/ firmware target's
 * classic-line view needs the exact same decode without pulling in this
 * file's st7920.h/144x32-font-table dependencies. Re-included here so
 * existing callers of this header don't need to change. */
#include "hp41_ascii_decode.h"

/**
 * @brief Decode the emulator's display state into an ST7920 framebuffer.
 *
 * Renders the Nut CPU's LCD shift registers (lcd_a/b/c/lcd_ann, owned by
 * emu41gcc/display.c) into an ST7920-format framebuffer (see st7920.h:
 * LCD_FB_SIZE bytes, 1bpp, row-major, MSB-first per row). Pure logic, no
 * hardware access - safe to call/test on a host build.
 *
 * @param fb Output buffer, at least LCD_FB_SIZE bytes; fully overwritten.
 */
void hp41_display_compute_framebuffer(uint8_t *fb);

/**
 * @brief Render the current display state straight to the physical LCD.
 *
 * hp41_display_compute_framebuffer() followed by st7920_draw_frame().
 * Call whenever the emulator's `fdsp` flag is set (see nutcpu.h) - the
 * caller is responsible for clearing fdsp afterwards, same as
 * emu41gcc's own reference main loop does (see CLAUDE.md "ROM wiring" /
 * emu41.c's traite_display() callers).
 */
void hp41_display_render(void);

#endif // SOYNUT_HP41_DISPLAY_BRIDGE_H
