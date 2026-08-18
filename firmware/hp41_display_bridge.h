/**
 * @file hp41_display_bridge.h
 * @brief Renders emu41gcc's Nut CPU LCD shift registers into a real
 *        ST7920 framebuffer, and optionally pushes it to hardware.
 */
#ifndef SOYNUT_HP41_DISPLAY_BRIDGE_H
#define SOYNUT_HP41_DISPLAY_BRIDGE_H

#include <stdint.h>

/**
 * @brief Decode one HP-41 raw display register code to an ASCII character.
 *
 * Re-derived from emu41gcc/display.c's static alpha41() - see
 * hp41_display_bridge.c's own implementation comment for why. No longer
 * `static`: hp41_dm41x_display_bridge.c's classic-line view (Phase 3b of
 * the Magellan/DM41X plan) needs the exact same decode, and duplicating
 * this specific function a third time (tools/powoff_trace.c already
 * duplicates it once, for a cross-build-system reason that doesn't apply
 * here - both callers live in firmware/'s own build) isn't worth it.
 *
 * @param v Raw HP-41 display code for one cell:
 *          (lcd_c[i]<<8) | ((lcd_b[i]&3)<<4) | lcd_a[i].
 * @return The decoded ASCII character code (0-127).
 */
int hp41_decode_ascii(int v);

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
