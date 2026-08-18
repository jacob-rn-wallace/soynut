/**
 * @file register_decode_test.c
 * @brief Native (host) test for hp41_register_decode.c.
 *
 * Extracted from elite_display_bridge_test.c (Phase 3 of the Magellan/
 * QUAD plan - see CLAUDE.md's "Elite User Mode" section for the
 * register-format provenance this is built on) alongside the production
 * code's own extraction into hp41_register_decode.c/h - this test now
 * covers exactly that module, independent of either display path that
 * consumes it.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_register_decode.h"

/**
 * @brief Zero every espaceRAM register this test touches (T,Z,Y,X).
 */
static void reset_registers(void)
{
    memset(&espaceRAM[HP41_ELITE_REG_T * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_Z * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_Y * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_X * 8], 0, 8);
}

/**
 * @brief Write a stack register's 7 packed-nibble bytes directly (byte 0 = write-protect flag, left at 0).
 *
 * @param stack_index One of the HP41_ELITE_REG_* values.
 * @param packed       Exactly 7 bytes, matching hp41_elite_decode_register()'s documented packing.
 */
static void write_register(int stack_index, const unsigned char packed[7])
{
    assert(stack_index >= HP41_ELITE_REG_T && stack_index <= HP41_ELITE_REG_X);
    assert(packed != NULL);
    int base = stack_index * 8;
    espaceRAM[base] = 0;
    memcpy(&espaceRAM[base + 1], packed, 7);
}

/**
 * @brief Print a pass/fail line comparing an actual value against an expected one.
 * @param label Human-readable name for this check.
 * @param got   Actual value.
 * @param want  Expected value.
 * @return 1 on match, 0 on mismatch.
 */
static int check_int(const char *label, int got, int want)
{
    assert(label != NULL);
    int ok = (got == want);
    printf("%-52s got=%-4d want=%-4d %s\n", label, got, want, ok ? "OK" : "MISMATCH");
    return ok;
}

/**
 * @brief Verify hp41_elite_decode_register() against hand-computed nibble packings.
 * @return Number of failed checks (0 = all pass).
 */
static int test_decode_register(void)
{
    int failures = 0;
    hp41_elite_number_t n;

    reset_registers();
    hp41_elite_decode_register(HP41_ELITE_REG_T, &n);
    failures += !check_int("all-zero: mantissa_negative", n.mantissa_negative, false);
    failures += !check_int("all-zero: exponent_negative", n.exponent_negative, false);
    failures += !check_int("all-zero: exponent_tens", n.exponent_tens, 0);
    failures += !check_int("all-zero: exponent_units", n.exponent_units, 0);
    for (int i = 0; i < 10; i++)
        failures += !check_int("all-zero: mantissa digit", n.mantissa_digits[i], 0);

    /* Mantissa 1234567890 (digit[0]=1 leading .. digit[9]=0 trailing),
     * exponent 04, both positive - see nibble derivation in the commit
     * that added this test / CLAUDE.md's worked example. */
    const unsigned char pos[7] = {0x40, 0x00, 0x89, 0x67, 0x45, 0x23, 0x01};
    write_register(HP41_ELITE_REG_X, pos);
    hp41_elite_decode_register(HP41_ELITE_REG_X, &n);
    failures += !check_int("positive: mantissa_negative", n.mantissa_negative, false);
    failures += !check_int("positive: exponent_negative", n.exponent_negative, false);
    failures += !check_int("positive: exponent_tens", n.exponent_tens, 0);
    failures += !check_int("positive: exponent_units", n.exponent_units, 4);
    const int want_digits[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    for (int i = 0; i < 10; i++)
        failures += !check_int("positive: mantissa digit", n.mantissa_digits[i], want_digits[i]);

    /* Same digits, both signs negative. */
    const unsigned char neg[7] = {0x49, 0x00, 0x89, 0x67, 0x45, 0x23, 0x91};
    write_register(HP41_ELITE_REG_X, neg);
    hp41_elite_decode_register(HP41_ELITE_REG_X, &n);
    failures += !check_int("negative: mantissa_negative", n.mantissa_negative, true);
    failures += !check_int("negative: exponent_negative", n.exponent_negative, true);
    for (int i = 0; i < 10; i++)
        failures += !check_int("negative: mantissa digit", n.mantissa_digits[i], want_digits[i]);

    return failures;
}

/**
 * @brief Run every register-decode check and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_decode_register();

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
