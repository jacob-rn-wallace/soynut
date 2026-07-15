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

#include <stdbool.h>
#include <stdint.h>

/** Stack register indices into espaceRAM, matching emu41gcc's own status-register layout. */
enum {
    HP41_ELITE_REG_T = 0,
    HP41_ELITE_REG_Z = 1,
    HP41_ELITE_REG_Y = 2,
    HP41_ELITE_REG_X = 3,
};

/** One decoded HP-41 stack register, as sign/digit fields ready to plot - never recombined into a single number. */
typedef struct {
    bool mantissa_negative;
    uint8_t mantissa_digits[10]; /**< [0] = leading/integer digit, [9] = last fractional digit, each 0-9. */
    bool exponent_negative;
    uint8_t exponent_tens; /**< 0-9. */
    uint8_t exponent_units; /**< 0-9. */
} hp41_elite_number_t;

/**
 * @brief Decode one stack register (T/Z/Y/X) directly from espaceRAM.
 *
 * Pure logic, no hardware access - safe to call/test on a host build.
 * See CLAUDE.md for the confirmed register format (14 packed BCD
 * nibbles: exponent sign, 2 exponent digits, 10 mantissa digits,
 * mantissa sign).
 *
 * @param stack_index One of the HP41_ELITE_REG_* values above.
 * @param out         Decoded fields, fully overwritten.
 */
void hp41_elite_decode_register(int stack_index, hp41_elite_number_t *out);

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
