/**
 * @file elite_display_bridge_test.c
 * @brief Native (host) test for Elite User Mode
 *        (firmware/hp41_elite_display_bridge.c).
 *
 * Unlike display_bridge_test.c, this doesn't boot the ROM at all - the
 * inputs (stack registers, the ALPHA-entry echo register) are plain
 * espaceRAM bytes, so a test can set them directly to a known value the
 * same way a live ROM computation would have, without needing
 * executeNUT() or nut_boot() at all. This makes the same exact-pixel-
 * count technique display_bridge_test.c uses for the fixed "MEMORY
 * LOST" string just as usable here for arbitrary register content.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_elite_display_bridge.h"
#include "hp41_display_tables.h" /* hp41_annunciator_bits/_pixels/_offset/_count, for the annunciator check below */
#include "st7920.h" /* LCD_WIDTH_PX/LCD_HEIGHT_PX/LCD_FB_SIZE/LCD_BYTES_PER_ROW only */

/* lcd_ann is a plain global in emu41gcc/display.c (no header exposes it -
 * same situation hp41_elite_display_bridge.c itself is already in). */
extern int lcd_ann;

/** Hand-verified (see the plan's own arithmetic, cross-checked with a
 *  standalone Python re-implementation of the exact same font-popcount/
 *  layout algorithm before writing these in) expected lit-pixel counts
 *  for specific known register values, at font-tables/hp41_elite_font_table.json's
 *  current glyph set. If the font glyphs are ever redrawn, these must
 *  be recomputed. */
#define ROW_PIXELS_ALL_ZERO 146
#define ROW_PIXELS_POSITIVE_1234567890_E04 129
#define ROW_PIXELS_NEGATIVE_1234567890_E04 135
#define ALPHA_ROW_PIXELS_ABC 27

/**
 * @brief Zero every espaceRAM register this test touches (T,Z,Y,X, and the ALPHA echo register).
 */
static void reset_registers(void)
{
    memset(&espaceRAM[HP41_ELITE_REG_T * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_Z * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_Y * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_X * 8], 0, 8);
    memset(&espaceRAM[5 * 8], 0, 8); /* HP41_ELITE_ALPHA_ECHO_REG, see hp41_elite_display_bridge.c */
    lcd_ann = 0;
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
 * @brief Read one pixel from a 1bpp, row-major, MSB-first framebuffer.
 * @param fb Framebuffer, at least LCD_FB_SIZE bytes.
 * @param x  Absolute column, 0 to LCD_WIDTH_PX-1.
 * @param y  Absolute row, 0 to LCD_HEIGHT_PX-1.
 * @return 1 if lit, 0 if not.
 */
static int get_px(const uint8_t *fb, int x, int y)
{
    assert(fb != NULL);
    assert(x >= 0 && x < LCD_WIDTH_PX && y >= 0 && y < LCD_HEIGHT_PX);
    return (fb[y * LCD_BYTES_PER_ROW + x / 8] >> (7 - (x % 8))) & 1;
}

/**
 * @brief Count the total number of lit pixels in a framebuffer.
 * @param fb Framebuffer, at least LCD_FB_SIZE bytes.
 * @return Lit pixel count, 0 to LCD_WIDTH_PX*LCD_HEIGHT_PX.
 */
static int count_lit(const uint8_t *fb)
{
    assert(fb != NULL);
    int lit = 0;
    for (int y = 0; y < LCD_HEIGHT_PX; y++)
        for (int x = 0; x < LCD_WIDTH_PX; x++)
            lit += get_px(fb, x, y);
    assert(lit >= 0 && lit <= LCD_WIDTH_PX * LCD_HEIGHT_PX);
    return lit;
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
 * @brief Verify the 4-row numeric grid's exact lit-pixel counts.
 * @return Number of failed checks (0 = all pass).
 */
static int test_numeric_framebuffer(void)
{
    int failures = 0;
    uint8_t fb[LCD_FB_SIZE];
    const unsigned char pos[7] = {0x40, 0x00, 0x89, 0x67, 0x45, 0x23, 0x01};
    const unsigned char neg[7] = {0x49, 0x00, 0x89, 0x67, 0x45, 0x23, 0x91};

    reset_registers();
    hp41_elite_display_compute_framebuffer(fb);
    failures += !check_int("all 4 registers zero: total lit px",
                            count_lit(fb), ROW_PIXELS_ALL_ZERO * 4);

    reset_registers();
    write_register(HP41_ELITE_REG_X, pos);
    hp41_elite_display_compute_framebuffer(fb);
    failures += !check_int("X positive, T/Z/Y zero: total lit px",
                            count_lit(fb), ROW_PIXELS_ALL_ZERO * 3 + ROW_PIXELS_POSITIVE_1234567890_E04);

    /* Sum-of-parts: all 4 rows independently non-overlapping. */
    reset_registers();
    write_register(HP41_ELITE_REG_T, pos);
    write_register(HP41_ELITE_REG_Z, neg);
    write_register(HP41_ELITE_REG_Y, pos);
    write_register(HP41_ELITE_REG_X, neg);
    hp41_elite_display_compute_framebuffer(fb);
    failures += !check_int("T=pos,Z=neg,Y=pos,X=neg: total lit px", count_lit(fb),
                            ROW_PIXELS_POSITIVE_1234567890_E04 * 2 + ROW_PIXELS_NEGATIVE_1234567890_E04 * 2);

    return failures;
}

/**
 * @brief Verify the annunciator row is the normal-mode table shifted exactly +5px down.
 * @return Number of failed checks (0 = all pass).
 */
static int test_annunciator_row_offset(void)
{
    int failures = 0;
    uint8_t fb[LCD_FB_SIZE];

    reset_registers();
    hp41_elite_display_compute_framebuffer(fb);
    const int baseline = count_lit(fb); /* all-zero registers, no annunciators */

    for (int a = 0; a < HP41_NUM_ANNUNCIATORS; a++) {
        reset_registers();
        lcd_ann = hp41_annunciator_bits[a];
        hp41_elite_display_compute_framebuffer(fb);
        const int got = count_lit(fb) - baseline;
        const int want = hp41_annunciator_pixel_count[a];
        failures += !check_int("annunciator pixel count matches normal-mode table", got, want);

        /* Every lit pixel for this annunciator must be exactly the
         * normal-mode table's pixel, shifted +5 in y - re-derived from
         * the existing generated table, not a second hand-typed
         * coordinate list, so this can't silently drift from it. */
        uint8_t off = hp41_annunciator_pixel_offset[a];
        uint8_t cnt = hp41_annunciator_pixel_count[a];
        int all_at_expected_offset = 1;
        for (uint8_t k = 0; k < cnt; k++) {
            hp41_pixel_t p = hp41_annunciator_pixels[off + k];
            if (!get_px(fb, p.x, p.y + 5))
                all_at_expected_offset = 0;
        }
        failures += !check_int("annunciator pixels all at normal-mode position +5y",
                                all_at_expected_offset, 1);
    }

    return failures;
}

/**
 * @brief Verify the ALPHA row (bottom row, alpha variant) renders the echo register correctly.
 * @return Number of failed checks (0 = all pass).
 */
static int test_alpha_row(void)
{
    int failures = 0;
    uint8_t fb[LCD_FB_SIZE];

    /* Register 5 (see hp41_elite_display_bridge.c's HP41_ELITE_ALPHA_ECHO_REG),
     * most-recently-typed character first: typed "ABC", so byte0='C',
     * byte1='B', byte2='A', rest 0 (unused/oldest slots). */
    reset_registers();
    const unsigned char abc[7] = {'C', 'B', 'A', 0, 0, 0, 0};
    write_register(HP41_ELITE_REG_T, (const unsigned char[7]){0x40, 0x00, 0x89, 0x67, 0x45, 0x23, 0x01});
    espaceRAM[5 * 8] = 0;
    memcpy(&espaceRAM[5 * 8 + 1], abc, 7);

    hp41_elite_display_compute_framebuffer_alpha(fb);
    /* Row 0 (T) still numeric, rows 1-2 (Z,Y) all-zero, row 3 is the
     * alpha echo instead of X. */
    failures += !check_int("alpha variant: total lit px",
                            count_lit(fb),
                            ROW_PIXELS_POSITIVE_1234567890_E04 + ROW_PIXELS_ALL_ZERO * 2 + ALPHA_ROW_PIXELS_ABC);

    return failures;
}

/**
 * @brief Run every Elite User Mode display-bridge check and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_decode_register()
                        + test_numeric_framebuffer()
                        + test_annunciator_row_offset()
                        + test_alpha_row();

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
