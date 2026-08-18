/**
 * @file hp41_elite_display_bridge.h
 * @brief Renders "Elite User Mode": a 4-line x 24-character alternate
 *        view of the HP-41's T/Z/Y/X stack registers (or, on the
 *        bottom row, the most recently typed ALPHA-mode text), using a
 *        tiny 3x5 pixel font instead of the normal 14-segment display.
 *
 * See CLAUDE.md's "Elite User Mode" section for the full design
 * (trigger sequence, register layout, punctuation semantics, and the
 * empirically-confirmed facts this is built on - where T/Z/Y/X and the
 * ALPHA-entry echo actually live in emu41gcc's espaceRAM).
 */
#ifndef SOYNUT_HP41_ELITE_DISPLAY_BRIDGE_H
#define SOYNUT_HP41_ELITE_DISPLAY_BRIDGE_H

#include <stdint.h>

/* HP41_ELITE_REG_*, hp41_elite_number_t, and hp41_elite_decode_register()
 * used to live here - extracted into their own hardware/display-agnostic
 * module (Phase 3 of the Magellan/DM41X plan) since the new DM41X-style
 * display needs the decode without any of this file's 144x32-specific
 * pixel-plotting code. Re-included here so existing callers of this
 * header don't need to change. */
#include "hp41_register_decode.h"

/**
 * @brief Render Elite Mode's 4-row stack grid into an ST7920 framebuffer.
 *
 * Rows top-to-bottom are T, Z, Y, X, each fully formatted as a signed
 * decimal number with exponent. Also plots the annunciator row (same
 * table as normal mode, shifted +5px down - see CLAUDE.md).
 *
 * @param fb Output buffer, at least LCD_FB_SIZE bytes; fully overwritten.
 */
void hp41_elite_display_compute_framebuffer(uint8_t *fb);

/**
 * @brief Render Elite Mode's grid with the ALPHA-entry echo in place of the X row.
 *
 * Same as hp41_elite_display_compute_framebuffer(), except row 3
 * (normally X) instead shows the most recently typed ALPHA-mode text,
 * read directly from espaceRAM register 5 (see CLAUDE.md for how this
 * was confirmed empirically, and its documented limit: only the most
 * recent ~7 characters, not the full 24-character ALPHA register).
 *
 * @param fb Output buffer, at least LCD_FB_SIZE bytes; fully overwritten.
 */
void hp41_elite_display_compute_framebuffer_alpha(uint8_t *fb);

#endif // SOYNUT_HP41_ELITE_DISPLAY_BRIDGE_H
