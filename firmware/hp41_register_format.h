/**
 * @file hp41_register_format.h
 * @brief Formats a decoded HP-41 register for display, honoring the
 *        calculator's actual current FIX/SCI/ENG display mode - real,
 *        mode-aware formatting, not Elite Mode's always-full-mantissa-
 *        plus-exponent shortcut.
 *
 * Phase 3a of the Magellan/DM41X plan
 * (/Users/jake/.claude/plans/gentle-mapping-dewdrop.md's Context section
 * has the full reasoning for why this exists as new code rather than
 * reusing ROM microcode): the real HP-41 only ever formats one register
 * (X) for display, and does so entirely inside ROM microcode - there is
 * no callable C formatter anywhere in `emu41gcc` to reuse (confirmed:
 * see hp41_register_decode.h's own provenance comment). But the
 * HP-41C/CV Owner's Handbook publicly documents the FIX/SCI/ENG display
 * rules, and `tools/flag_array_trace.c` empirically located where the
 * calculator's *current* mode/digit-count actually live in `espaceRAM`
 * (see CLAUDE.md's "Display-format state" section) - so this module
 * reimplements the documented rules directly, driven by that state,
 * rather than re-entering ROM execution mid-flight.
 *
 * Output is a sequence of per-cell fields (digit/sign/blank, each
 * optionally followed by a decimal-point or exponent-separator gap-mark)
 * in the real HP-41's actual 12-column, variable-width layout - not
 * Elite Mode's always-14-fixed-column convention (see
 * font-tables/hp41_dm41x_font_table.h for why). This is deliberately
 * the same shape hp41_display_bridge.c's own per-cell decode already
 * produces for the classic single-line display, so both of the DM41X-
 * style display's view modes (Stack and classic-line) can walk a field
 * sequence and plot through the same table identically.
 */
#ifndef SOYNUT_HP41_REGISTER_FORMAT_H
#define SOYNUT_HP41_REGISTER_FORMAT_H

#include <stdint.h>

#include "hp41_register_decode.h"

/** Real HP-41 physical character-cell count - see
 *  font-tables/hp41_dm41x_font_table.h's HP41_DM41X_NUM_COLS derivation. */
#define HP41_FORMAT_MAX_FIELDS 12

/** Current calculator display-format mode - mirrors the empirically-confirmed
 *  espaceRAM[114] bit-7/bit-6 encoding (see CLAUDE.md's "Display-format state"
 *  section): FIX = bit7 set, ENG = bit6 set, SCI = neither. */
typedef enum {
    HP41_DISPLAY_MODE_FIX,
    HP41_DISPLAY_MODE_SCI,
    HP41_DISPLAY_MODE_ENG,
} hp41_display_mode_t;

/** Current calculator display-format state: mode + digit count (0-9). */
typedef struct {
    hp41_display_mode_t mode;
    uint8_t digit_count; /**< 0-9, from espaceRAM[115] (see CLAUDE.md). */
} hp41_display_format_t;

/**
 * @brief Read the calculator's current display-format state from espaceRAM.
 *
 * Pure logic, no hardware access - safe to call/test on a host build,
 * same as hp41_elite_decode_register().
 *
 * @param out Decoded format state, fully overwritten.
 */
void hp41_read_display_format(hp41_display_format_t *out);

/** What one formatted display cell shows. */
typedef enum {
    HP41_FIELD_DIGIT, /**< A digit 0-9 - see hp41_display_field_t.digit. */
    HP41_FIELD_MINUS, /**< A minus sign. */
} hp41_field_kind_t;

/** One formatted cell, plus any gap-mark riding in the space after it
 *  (matching the real hardware's decimal-point/separator convention -
 *  see font-tables/hp41_dm41x_font_table.h's HP41_DM41X_SEG_DOT/_SEP). */
typedef struct {
    hp41_field_kind_t kind;
    uint8_t digit; /**< 0-9, valid only when kind == HP41_FIELD_DIGIT. */
    uint8_t decimal_point_after; /**< 1 if a decimal point gap-mark follows this cell. */
    uint8_t separator_after; /**< 1 if a mantissa/exponent separator gap-mark follows this cell. */
} hp41_display_field_t;

/** A formatted number, ready to plot cell-by-cell, left to right. */
typedef struct {
    hp41_display_field_t fields[HP41_FORMAT_MAX_FIELDS];
    int num_fields; /**< 0 to HP41_FORMAT_MAX_FIELDS. */
} hp41_formatted_number_t;

/**
 * @brief Format one decoded register per the given display format.
 *
 * Implements the HP-41C/CV Owner's Handbook's documented FIX/SCI/ENG
 * rules: FIX n shows n digits after the decimal point, auto-falling back
 * to scientific notation if the value doesn't fit; SCI n shows 1 leading
 * mantissa digit + n fractional digits + a signed 2-digit exponent; ENG
 * n is the same as SCI but the exponent is constrained to a multiple of
 * 3, shifting the mantissa's decimal point accordingly. Rounds (with
 * carry propagation and, if needed, exponent renormalization) rather
 * than truncating.
 *
 * If the naturally-formatted result would need more than
 * HP41_FORMAT_MAX_FIELDS real cells (only possible at the largest digit
 * counts combined with a negative sign and/or negative exponent - the
 * real hardware faces this identical 12-cell physical limit), trailing
 * mantissa fractional digits are dropped (with re-rounding at the new
 * boundary) until it fits - a deliberate, documented compression rule,
 * not silent truncation.
 *
 * @param n   Decoded register (see hp41_register_decode.h).
 * @param fmt Current display-format state (see hp41_read_display_format()).
 * @param out Formatted field sequence, fully overwritten.
 */
void hp41_format_number(const hp41_elite_number_t *n, const hp41_display_format_t *fmt,
                         hp41_formatted_number_t *out);

#endif // SOYNUT_HP41_REGISTER_FORMAT_H
