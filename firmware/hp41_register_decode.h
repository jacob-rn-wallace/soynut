/**
 * @file hp41_register_decode.h
 * @brief Decodes one HP-41 stack register (T/Z/Y/X) directly from
 *        emu41gcc's espaceRAM into sign/digit fields ready to plot.
 *
 * Extracted from hp41_elite_display_bridge.c/.h (Phase 3 of the
 * Magellan/QUAD plan - see
 * /Users/jake/.claude/plans/gentle-mapping-dewdrop.md): this is pure
 * logic with no display-specific code, and both the dormant 144x32
 * Elite Mode display and the new QUAD-style display need it - pulling
 * it into its own hardware/display-agnostic module avoids compiling the
 * entire Elite Mode file (its 144x32-specific pixel-plotting, its own
 * annunciator-pixel-table usage) into the new quad/ firmware target
 * just to reuse this one function, and avoids duplicating it.
 *
 * See CLAUDE.md's "Elite User Mode" section for the full empirical
 * derivation this is built on (how T/Z/Y/X and the packed-BCD register
 * format were confirmed against emu41gcc/nutcpu.c's recallData()/
 * storeData() and exec2()'s field-selector table).
 */
#ifndef SOYNUT_HP41_REGISTER_DECODE_H
#define SOYNUT_HP41_REGISTER_DECODE_H

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

#endif // SOYNUT_HP41_REGISTER_DECODE_H
