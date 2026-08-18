/**
 * @file register_format_test.c
 * @brief Native (host) test for firmware/hp41_register_format.c.
 *
 * Expected values below were derived by hand from the HP-41C/CV Owner's
 * Handbook's documented FIX/SCI/ENG rules (see hp41_register_format.h's
 * own provenance comment), then cross-checked against the actual
 * implementation's output during development - same "compute
 * independently, then compare" spirit as
 * elite_display_bridge_test.c's own hand-verified pixel counts.
 *
 * Renders each hp41_formatted_number_t to a compact string ("-1.2346|04"
 * - '.' = decimal point gap-mark, '|' = exponent-separator gap-mark) so
 * a whole formatted number can be checked in one string comparison,
 * rather than one assertion per field.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hp41_register_format.h"

/**
 * @brief Render a formatted number to a compact string for comparison.
 * @param f   Formatted number to render.
 * @param out Output buffer, at least 24 bytes (num_fields <= 12, plus
 *            up to 2 gap-marks per field and a NUL).
 */
static void render(const hp41_formatted_number_t *f, char *out)
{
    assert(f != NULL);
    assert(out != NULL);
    char *p = out;
    for (int i = 0; i < f->num_fields; i++) {
        const hp41_display_field_t *c = &f->fields[i];
        *p++ = (c->kind == HP41_FIELD_MINUS) ? '-' : (char)('0' + c->digit);
        if (c->decimal_point_after)
            *p++ = '.';
        if (c->separator_after)
            *p++ = '|';
    }
    *p = '\0';
}

/**
 * @brief Set a decoded register's fields directly from plain arguments.
 * @param n    Register to fill in.
 * @param neg  Mantissa sign.
 * @param digits Exactly 10 mantissa digits, leading first.
 * @param eneg Exponent sign.
 * @param et   Exponent tens digit.
 * @param eu   Exponent units digit.
 */
static void set_register(hp41_elite_number_t *n, int neg, const int digits[10],
                          int eneg, int et, int eu)
{
    assert(n != NULL);
    assert(digits != NULL);
    n->mantissa_negative = neg;
    for (int i = 0; i < 10; i++)
        n->mantissa_digits[i] = (uint8_t)digits[i];
    n->exponent_negative = eneg;
    n->exponent_tens = (uint8_t)et;
    n->exponent_units = (uint8_t)eu;
}

/**
 * @brief Print a pass/fail line comparing a rendered string against the expected one.
 * @param label Human-readable name for this check.
 * @param got   Actual rendered string.
 * @param want  Expected rendered string.
 * @return 1 on match, 0 on mismatch.
 */
static int check_str(const char *label, const char *got, const char *want)
{
    assert(label != NULL);
    assert(got != NULL);
    assert(want != NULL);
    int ok = strcmp(got, want) == 0;
    printf("%-42s got=%-16s want=%-16s %s\n", label, got, want, ok ? "OK" : "MISMATCH");
    return ok;
}

/**
 * @brief Format one case and check it against an expected rendered string.
 * @param label   Human-readable name for this check.
 * @param n       Decoded register.
 * @param mode    Display mode.
 * @param digits  Digit-count setting (0-9).
 * @param want    Expected rendered string (see render()).
 * @return 1 on match, 0 on mismatch.
 */
static int check_format(const char *label, const hp41_elite_number_t *n,
                         hp41_display_mode_t mode, int digits, const char *want)
{
    hp41_display_format_t fmt = {.mode = mode, .digit_count = (uint8_t)digits};
    hp41_formatted_number_t f;
    hp41_format_number(n, &fmt, &f);
    char got[24];
    render(&f, got);
    return check_str(label, got, want);
}

/**
 * @brief SCI-mode checks: digit count sweep, sign/exponent-sign, overflow trim.
 * @return Number of failed checks.
 */
static int test_sci(void)
{
    int failures = 0;
    const int digits[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0}; /* 1.234567890 */
    hp41_elite_number_t n;

    set_register(&n, 0, digits, 0, 0, 4); /* +1.234567890e4 */
    failures += !check_format("SCI 0", &n, HP41_DISPLAY_MODE_SCI, 0, "1|04");
    failures += !check_format("SCI 4 (rounds 5->6)", &n, HP41_DISPLAY_MODE_SCI, 4, "1.2346|04");
    failures += !check_format("SCI 9 (full precision)", &n, HP41_DISPLAY_MODE_SCI, 9, "1.234567890|04");

    set_register(&n, 1, digits, 1, 0, 4); /* -1.234567890e-4 */
    failures += !check_format("SCI 4, negative mantissa+exponent", &n, HP41_DISPLAY_MODE_SCI, 4,
                               "-1.2346|-04");
    /* 12 real cells max: -(1) + 1 + frac + -(1) + 2 <= 12 => frac <= 7,
     * so SCI 9's 9 requested fractional digits get trimmed to 7. */
    failures += !check_format("SCI 9, negative+negative (overflow trim)", &n, HP41_DISPLAY_MODE_SCI, 9,
                               "-1.2345678-04");

    return failures;
}

/**
 * @brief ENG-mode checks: exponent forced to a multiple of 3, mantissa point shifts.
 * @return Number of failed checks.
 */
static int test_eng(void)
{
    int failures = 0;
    const int digits[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0}; /* value ~1.23456789e4 = 12345.6789 */
    hp41_elite_number_t n;
    set_register(&n, 0, digits, 0, 0, 4);

    /* exponent 4 -> nearest-lower multiple of 3 is 3, shift=1 -> 2 leading digits. */
    failures += !check_format("ENG 0", &n, HP41_DISPLAY_MODE_ENG, 0, "12|03");
    failures += !check_format("ENG 3", &n, HP41_DISPLAY_MODE_ENG, 3, "12.346|03");

    return failures;
}

/**
 * @brief FIX-mode checks: digit count sweep, both auto-SCI-fallback directions,
 *        and a rounding cascade that carries past the leading digit.
 * @return Number of failed checks.
 */
static int test_fix(void)
{
    int failures = 0;
    const int digits[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    hp41_elite_number_t n;

    /* value 12345.6789 */
    set_register(&n, 0, digits, 0, 0, 4);
    failures += !check_format("FIX 0 (too big for FIX 0 - falls back to SCI)",
                               &n, HP41_DISPLAY_MODE_FIX, 0, "1|04");
    failures += !check_format("FIX 1 (rounds 6789->7)", &n, HP41_DISPLAY_MODE_FIX, 1, "12345.7");
    failures += !check_format("FIX 4 (exact)", &n, HP41_DISPLAY_MODE_FIX, 4, "12345.6789");
    failures += !check_format("FIX 8 (too big - falls back to SCI)",
                               &n, HP41_DISPLAY_MODE_FIX, 8, "1.23456789|04");

    /* value 0.000123456789 */
    set_register(&n, 0, digits, 1, 0, 4);
    failures += !check_format("FIX 3 (too small - falls back to SCI)",
                               &n, HP41_DISPLAY_MODE_FIX, 3, "1.235|-04");
    failures += !check_format("FIX 4 (first real digit just fits)",
                               &n, HP41_DISPLAY_MODE_FIX, 4, "0.0001");
    failures += !check_format("FIX 9", &n, HP41_DISPLAY_MODE_FIX, 9, "0.000123456");

    /* 9.999999999, FIX 2: rounds all the way up, carrying past the
     * leading digit - "10.00", not "10.10" (a real bug this test would
     * have caught: the fractional digits must re-index after the carry,
     * not restart from the mantissa's own beginning). */
    const int nines[10] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    set_register(&n, 0, nines, 0, 0, 0);
    failures += !check_format("FIX 2, carry cascades past leading digit",
                               &n, HP41_DISPLAY_MODE_FIX, 2, "10.00");

    return failures;
}

/**
 * @brief Zero is exactly representable in every mode - never falls back.
 * @return Number of failed checks.
 */
static int test_zero(void)
{
    int failures = 0;
    const int zeros[10] = {0};
    hp41_elite_number_t n;
    set_register(&n, 0, zeros, 0, 0, 0);

    failures += !check_format("zero, FIX 4", &n, HP41_DISPLAY_MODE_FIX, 4, "0.0000");
    failures += !check_format("zero, SCI 3", &n, HP41_DISPLAY_MODE_SCI, 3, "0.000|00");

    return failures;
}

/**
 * @brief Run every register-format check and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_sci() + test_eng() + test_fix() + test_zero();

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
