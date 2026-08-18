/**
 * @file hp41_register_format.c
 * @brief Implements hp41_read_display_format()/hp41_format_number() -
 *        see hp41_register_format.h for the public contract and
 *        provenance.
 */

#include "hp41_register_format.h"

#include <assert.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

/** Empirically-confirmed espaceRAM offsets - see CLAUDE.md's
 *  "Display-format state" section for how these were found and their
 *  documented-flag-semantics cross-check. */
#define ESPACE_DISPLAY_MODE_BYTE 114
#define ESPACE_DISPLAY_DIGITS_BYTE 115
#define DISPLAY_MODE_FIX_BIT 0x80
#define DISPLAY_MODE_ENG_BIT 0x40

void hp41_read_display_format(hp41_display_format_t *out)
{
    assert(out != NULL);

    uint8_t mode_byte = espaceRAM[ESPACE_DISPLAY_MODE_BYTE];
    if (mode_byte & DISPLAY_MODE_FIX_BIT)
        out->mode = HP41_DISPLAY_MODE_FIX;
    else if (mode_byte & DISPLAY_MODE_ENG_BIT)
        out->mode = HP41_DISPLAY_MODE_ENG;
    else
        out->mode = HP41_DISPLAY_MODE_SCI;

    out->digit_count = espaceRAM[ESPACE_DISPLAY_DIGITS_BYTE] & 0x0F;
    assert(out->digit_count <= 9);
}

/**
 * @brief Round a 10-digit mantissa to `keep` significant digits.
 *
 * @param full  Source mantissa, exactly 10 digits, MSB (leading) first.
 * @param keep  How many leading digits to keep, 0 to 10.
 * @param out   Output buffer, at least `keep` digits (0 if keep == 0).
 * @return 1 if rounding carried past the leading digit (out[] was
 *         shifted right and out[0] set to 1 - caller must increment the
 *         display exponent by 1), 0 otherwise.
 */
static int round_mantissa(const uint8_t full[10], int keep, uint8_t *out)
{
    assert(full != NULL);
    assert(keep >= 0 && keep <= 10);
    assert(out != NULL || keep == 0);

    for (int i = 0; i < keep; i++)
        out[i] = full[i];
    if (keep >= 10 || keep < 1)
        return 0; /* nothing left to round against */

    int round_up = full[keep] >= 5;
    if (!round_up)
        return 0;

    int i = keep - 1;
    while (i >= 0) {
        out[i]++;
        if (out[i] < 10)
            return 0;
        out[i] = 0;
        i--;
    }
    /* Carried past the leading digit: 999...->1000... - shift right,
     * dropping the new last digit (still only `keep` digits shown),
     * leading digit becomes 1. */
    for (int j = keep - 1; j > 0; j--)
        out[j] = out[j - 1];
    out[0] = 1;
    return 1;
}

/**
 * @brief Compute the register's true internal exponent as a signed int.
 * @param n Decoded register.
 * @return exponent_tens*10 + exponent_units, negated if exponent_negative.
 */
static int true_exponent(const hp41_elite_number_t *n)
{
    assert(n != NULL);
    int e = n->exponent_tens * 10 + n->exponent_units;
    return n->exponent_negative ? -e : e;
}

/**
 * @brief Whether every mantissa digit is zero (a true zero value).
 * @param n Decoded register.
 * @return 1 if the mantissa is all zeros, 0 otherwise.
 */
static int is_zero(const hp41_elite_number_t *n)
{
    assert(n != NULL);
    for (int i = 0; i < 10; i++)
        if (n->mantissa_digits[i] != 0)
            return 0;
    return 1;
}

/**
 * @brief Append one digit field to a formatted-number field list.
 * @param out   Field list being built; num_fields incremented by 1.
 * @param digit 0-9.
 */
static void push_digit(hp41_formatted_number_t *out, uint8_t digit)
{
    assert(out != NULL);
    assert(digit <= 9);
    assert(out->num_fields < HP41_FORMAT_MAX_FIELDS);
    hp41_display_field_t *f = &out->fields[out->num_fields++];
    f->kind = HP41_FIELD_DIGIT;
    f->digit = digit;
    f->decimal_point_after = 0;
    f->separator_after = 0;
}

/**
 * @brief Append a minus-sign field to a formatted-number field list.
 * @param out Field list being built; num_fields incremented by 1.
 */
static void push_minus(hp41_formatted_number_t *out)
{
    assert(out != NULL);
    assert(out->num_fields < HP41_FORMAT_MAX_FIELDS);
    hp41_display_field_t *f = &out->fields[out->num_fields++];
    f->kind = HP41_FIELD_MINUS;
    f->digit = 0;
    f->decimal_point_after = 0;
    f->separator_after = 0;
}

/**
 * @brief Append the signed 2-digit exponent (separator already placed
 *        by the caller on the last mantissa field), dropping mantissa
 *        fields from the back if the exponent doesn't fit.
 *
 * @param out          Field list being built.
 * @param exp_negative Whether the display exponent is negative.
 * @param exp_value    0-99 (already reduced to 2 digits by the caller).
 */
static void push_exponent(hp41_formatted_number_t *out, int exp_negative, int exp_value)
{
    assert(out != NULL);
    assert(exp_value >= 0 && exp_value <= 99);

    int needed = (exp_negative ? 1 : 0) + 2;
    while (out->num_fields + needed > HP41_FORMAT_MAX_FIELDS) {
        assert(out->num_fields > 0);
        out->num_fields--; /* drop the least-significant mantissa digit */
    }
    if (exp_negative)
        push_minus(out);
    push_digit(out, (uint8_t)(exp_value / 10));
    push_digit(out, (uint8_t)(exp_value % 10));
}

/**
 * @brief Format a register in SCI-style layout: 1 leading digit, `frac`
 *        fractional digits, then a signed 2-digit exponent.
 *
 * Shared by HP41_DISPLAY_MODE_SCI and (with a pre-shifted mantissa/
 * exponent) HP41_DISPLAY_MODE_ENG - see hp41_format_number().
 *
 * @param n           Decoded register.
 * @param int_digits  Leading (pre-decimal-point) digit count, 1-3 (3 for
 *                    ENG's largest shift; always 1 for plain SCI).
 * @param frac        Fractional digit count after the point, clamped by
 *                    the caller so int_digits+frac <= 10.
 * @param disp_exp    Display exponent (already shifted for ENG).
 * @param out         Output field list, fully overwritten.
 */
static void format_sci_style(const hp41_elite_number_t *n, int int_digits, int frac,
                              int disp_exp, hp41_formatted_number_t *out)
{
    assert(n != NULL);
    assert(int_digits >= 1 && int_digits <= 3);
    assert(frac >= 0);
    assert(int_digits + frac <= 10);
    assert(out != NULL);

    memset(out, 0, sizeof(*out));

    uint8_t rounded[10] = {0};
    int carried = round_mantissa(n->mantissa_digits, int_digits + frac, rounded);
    if (carried)
        disp_exp++;

    if (n->mantissa_negative)
        push_minus(out);
    for (int i = 0; i < int_digits; i++)
        push_digit(out, rounded[i]);
    if (frac > 0)
        out->fields[out->num_fields - 1].decimal_point_after = 1;
    for (int i = 0; i < frac; i++)
        push_digit(out, rounded[int_digits + i]);
    out->fields[out->num_fields - 1].separator_after = 1;

    push_exponent(out, disp_exp < 0, disp_exp < 0 ? -disp_exp : disp_exp);
}

/**
 * @brief Format a register in FIX-style layout: integer part, decimal
 *        point, `frac` fractional digits - no exponent shown.
 *
 * @param n         Decoded register.
 * @param exp       True internal exponent (see true_exponent()).
 * @param frac      Fractional digit count after the point (the FIX n setting).
 * @param out       Output field list, fully overwritten.
 */
static void format_fix_style(const hp41_elite_number_t *n, int exp, int frac,
                              hp41_formatted_number_t *out)
{
    assert(n != NULL);
    assert(frac >= 0 && frac <= 9);
    assert(out != NULL);

    memset(out, 0, sizeof(*out));

    /* point_offset is the mathematical digit-string offset of the
     * decimal point (may be <= 0, meaning the point sits at or before
     * the mantissa's own leading digit) - NOT the same as the *display*
     * integer-digit count, which is clamped to a minimum of 1 (a literal
     * "0") when point_offset <= 0. Keeping these separate is what makes
     * a single index formula work for both the "big number" and "small
     * number" cases below, rather than two different ones. */
    int point_offset = exp + 1;
    int int_digits = (point_offset > 0) ? point_offset : 1;
    int leading_zeros = (point_offset > 0) ? 0 : -point_offset;

    /* How many real mantissa digits are actually visible: the window
     * [0, int_digits+frac) shifted left by leading_zeros, clamped to
     * the 10 digits we actually have. */
    int window = int_digits + frac - leading_zeros;
    if (window < 0)
        window = 0;
    if (window > 10)
        window = 10;

    uint8_t rounded[10] = {0};
    int carried = round_mantissa(n->mantissa_digits, window, rounded);
    if (carried) {
        /* Carried past the mantissa's own leading digit - e.g. FIX 2 on
         * 9.996 rounds to "10.00": the decimal point's true offset (and
         * so the display integer-digit count) shifts right by one.
         * rounded[] is already shifted to match (see round_mantissa()). */
        point_offset++;
        int_digits = (point_offset > 0) ? point_offset : 1;
    }

    if (n->mantissa_negative)
        push_minus(out);

    for (int i = 0; i < int_digits; i++) {
        uint8_t digit = (point_offset > 0 && i < window) ? rounded[i] : 0;
        push_digit(out, digit);
    }
    if (frac > 0)
        out->fields[out->num_fields - 1].decimal_point_after = 1;

    for (int i = 0; i < frac; i++) {
        int src = point_offset + i;
        uint8_t digit = (src >= 0 && src < window) ? rounded[src] : 0;
        push_digit(out, digit);
    }
}

void hp41_format_number(const hp41_elite_number_t *n, const hp41_display_format_t *fmt,
                         hp41_formatted_number_t *out)
{
    assert(n != NULL);
    assert(fmt != NULL);
    assert(out != NULL);
    assert(fmt->digit_count <= 9);

    if (is_zero(n)) {
        /* Zero is exactly representable in every mode - always show it
         * plainly rather than running it through the general FIX/SCI/ENG
         * math (which assumes a normalized nonzero mantissa). */
        memset(out, 0, sizeof(*out));
        push_digit(out, 0);
        if (fmt->mode == HP41_DISPLAY_MODE_FIX) {
            out->fields[out->num_fields - 1].decimal_point_after = (fmt->digit_count > 0);
            for (int i = 0; i < fmt->digit_count; i++)
                push_digit(out, 0);
        } else {
            out->fields[out->num_fields - 1].decimal_point_after = 1;
            for (int i = 0; i < fmt->digit_count; i++)
                push_digit(out, 0);
            out->fields[out->num_fields - 1].separator_after = 1;
            push_digit(out, 0);
            push_digit(out, 0);
        }
        return;
    }

    int exp = true_exponent(n);
    int n_digit_count = fmt->digit_count;

    if (fmt->mode == HP41_DISPLAY_MODE_SCI) {
        format_sci_style(n, 1, n_digit_count, exp, out);
        return;
    }

    if (fmt->mode == HP41_DISPLAY_MODE_ENG) {
        int shift = ((exp % 3) + 3) % 3; /* 0, 1, or 2 extra leading digits */
        int int_digits = 1 + shift;
        int frac = n_digit_count;
        if (int_digits + frac > 10)
            frac = 10 - int_digits;
        format_sci_style(n, int_digits, frac, exp - shift, out);
        return;
    }

    assert(fmt->mode == HP41_DISPLAY_MODE_FIX);
    int int_digits = (exp >= 0) ? exp + 1 : 1;
    int sign_cols = n->mantissa_negative ? 1 : 0;
    int leading_zeros = (exp >= 0) ? 0 : (-exp - 1);
    int too_big = (sign_cols + int_digits + n_digit_count) > HP41_FORMAT_MAX_FIELDS;
    int too_small = leading_zeros >= n_digit_count; /* no real digit would show */
    if (too_big || too_small) {
        format_sci_style(n, 1, n_digit_count, exp, out);
        return;
    }
    format_fix_style(n, exp, n_digit_count, out);
}
