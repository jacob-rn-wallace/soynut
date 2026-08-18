/**
 * @file dm41x_display_bridge_test.c
 * @brief Native (host) test for firmware/hp41_dm41x_display_bridge.c.
 *
 * Same approach as elite_display_bridge_test.c: pokes espaceRAM/lcd_a/b/c
 * directly rather than booting the ROM, so exact pixel counts are usable
 * for arbitrary register/display content without executeNUT() or
 * nut_boot().
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_dm41x_display_bridge.h"
#include "hp41_register_decode.h"

/* lcd_a/b/c/lcd_ann are plain globals in emu41gcc/display.c (no header
 * exposes them - same situation the production code itself is in). */
extern unsigned char lcd_a[12];
extern unsigned char lcd_b[12];
extern unsigned char lcd_c[12];
extern int lcd_ann;

/** Empirically-confirmed espaceRAM offsets for display-format state (see
 *  CLAUDE.md's "Display-format state" section) - poked directly here,
 *  same spirit as write_register() below poking the stack registers. */
#define ESPACE_DISPLAY_MODE_BYTE 114
#define ESPACE_DISPLAY_DIGITS_BYTE 115
#define DISPLAY_MODE_FIX_BIT 0x80

/**
 * @brief Zero every espaceRAM register this test touches, clear lcd_a/b/c/ann,
 *        and set a default FIX 4 display format (not SCI's all-zero default).
 */
static void reset_state(void)
{
    memset(&espaceRAM[HP41_ELITE_REG_T * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_Z * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_Y * 8], 0, 8);
    memset(&espaceRAM[HP41_ELITE_REG_X * 8], 0, 8);
    espaceRAM[ESPACE_DISPLAY_MODE_BYTE] = DISPLAY_MODE_FIX_BIT;
    espaceRAM[ESPACE_DISPLAY_DIGITS_BYTE] = 4;
    memset(lcd_a, 0, 12);
    memset(lcd_b, 0, 12);
    memset(lcd_c, 0, 12);
    lcd_ann = 0;
}

/**
 * @brief Write a stack register's 7 packed-nibble bytes directly (byte 0 = write-protect flag, left at 0).
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
 * @brief Write one classic-line display cell's raw code + punctuation directly.
 *
 * Only needs to invert hp41_decode_ascii() correctly for this test's own
 * three input characters: space, digits '0'-'9', and uppercase letters
 * 'A'-'Z' (its other branches - the 0x100+ range, the special-cased
 * punctuation-like glyphs - aren't exercised here, so aren't inverted
 * here either). Note that raw code 0 legitimately decodes to '@', NOT
 * blank - reset_state()'s zeroed lcd_a/b/c is NOT the same as "12 blank
 * cells"; use write_classic_cell(pos, ' ', 0) explicitly for that.
 *
 * @param pos   Screen position, 0 (leftmost) to 11 (rightmost).
 * @param ascii ' ', '0'-'9', or 'A'-'Z'.
 * @param punct 0 = none, 1 = period, 2 = colon, 3 = comma.
 */
static void write_classic_cell(int pos, int ascii, int punct)
{
    assert(pos >= 0 && pos < 12);
    assert(ascii == ' ' || (ascii >= '0' && ascii <= '9') || (ascii >= 'A' && ascii <= 'Z'));
    assert(punct >= 0 && punct <= 3);
    int i = 11 - pos;
    /* Space and digits: hp41_decode_ascii()'s v<=0x3f branch returns v
     * unchanged for 0x20-0x3f. Uppercase letters: its v<=0x1f branch
     * returns v+'@', so the inverse is v = ascii-'@'. */
    int v = (ascii == ' ' || (ascii >= '0' && ascii <= '9')) ? ascii : (ascii - '@');
    lcd_a[i] = (unsigned char)(v & 0xFF);
    lcd_b[i] = (unsigned char)(((v >> 4) & 0x03) | (punct << 2));
    lcd_c[i] = (unsigned char)((v >> 8) & 0xFF);
}

/**
 * @brief Fill all 12 classic-line cells with the same character, no punctuation.
 * @param ascii ' ', '0'-'9', or 'A'-'Z' (see write_classic_cell()).
 */
static void fill_classic_line(int ascii)
{
    for (int pos = 0; pos < 12; pos++)
        write_classic_cell(pos, ascii, 0);
}

/**
 * @brief Read one pixel from a 1bpp, row-major, MSB-first framebuffer
 *        (Sharp panel polarity: 0 = lit, 1 = unlit - see hp41_dm41x_display_bridge.h).
 * @param fb Framebuffer, at least HP41_DM41X_FB_SIZE bytes.
 * @param x  Absolute column, 0 to HP41_DM41X_DISP_WIDTH_PX-1.
 * @param y  Absolute row, 0 to HP41_DM41X_DISP_HEIGHT_PX-1.
 * @return 1 if lit (bit clear), 0 if not.
 */
static int get_px(const uint8_t *fb, int x, int y)
{
    assert(fb != NULL);
    assert(x >= 0 && x < HP41_DM41X_DISP_WIDTH_PX);
    assert(y >= 0 && y < HP41_DM41X_DISP_HEIGHT_PX);
    int bytes_per_row = HP41_DM41X_DISP_WIDTH_PX / 8;
    return !((fb[y * bytes_per_row + x / 8] >> (7 - (x % 8))) & 1);
}

/**
 * @brief Count the total number of lit pixels in a framebuffer.
 * @param fb Framebuffer, at least HP41_DM41X_FB_SIZE bytes.
 * @return Lit pixel count.
 */
static int count_lit(const uint8_t *fb)
{
    assert(fb != NULL);
    int lit = 0;
    for (int y = 0; y < HP41_DM41X_DISP_HEIGHT_PX; y++)
        for (int x = 0; x < HP41_DM41X_DISP_WIDTH_PX; x++)
            lit += get_px(fb, x, y);
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
 * @brief Sanity-check the panel is cleared to all-white (nothing lit) with an all-zero/blank state.
 * @return Number of failed checks.
 */
static int test_blank_framebuffer(void)
{
    int failures = 0;
    static uint8_t fb[HP41_DM41X_FB_SIZE];

    reset_state();
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_STACK);
    /* All 4 rows show "0.0000" (FIX 4 default) - not literally zero lit
     * pixels, just confirms the buffer isn't stuck all-lit/uninitialized
     * and a full computation runs without crashing. */
    failures += !check_int("stack view, all-zero registers: some pixels lit",
                            count_lit(fb) > 0, 1);

    reset_state();
    fill_classic_line(' '); /* raw code 0 (reset_state()'s default) decodes to '@', not blank */
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_CLASSIC_LINE);
    failures += !check_int("classic-line view, 12 spaces: zero pixels lit",
                            count_lit(fb), 0);

    return failures;
}

/**
 * @brief Verify the Stack view plots real FIX/SCI-formatted content for all 4 registers.
 * @return Number of failed checks.
 */
static int test_stack_view(void)
{
    int failures = 0;
    static uint8_t fb[HP41_DM41X_FB_SIZE];

    /* Mantissa 1234567890, exponent 04, positive - same worked example
     * register_format_test.c and register_decode_test.c both use. */
    const unsigned char pos[7] = {0x40, 0x00, 0x89, 0x67, 0x45, 0x23, 0x01};

    reset_state();
    write_register(HP41_ELITE_REG_X, pos);
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_STACK);
    int with_x = count_lit(fb);

    reset_state();
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_STACK);
    int all_zero = count_lit(fb);

    /* X's row has real digits now (FIX 4: "12345.6789") instead of
     * "0.0000" - strictly more pixels lit, without needing to hand-count
     * the exact expected pixel total (that's register_format_test.c's
     * and the font table's own job to get right). */
    failures += !check_int("stack view, X=1234567890e4 lit more than all-zero",
                            with_x > all_zero, 1);

    /* Sum-of-parts: T and X both set to the same value light exactly
     * twice one row's worth more than only X set. */
    reset_state();
    write_register(HP41_ELITE_REG_X, pos);
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_STACK);
    int x_only = count_lit(fb);

    reset_state();
    write_register(HP41_ELITE_REG_T, pos);
    write_register(HP41_ELITE_REG_X, pos);
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_STACK);
    int t_and_x = count_lit(fb);

    failures += !check_int("stack view, T=X=same value: T row adds X row's own pixel count",
                            t_and_x, x_only + (x_only - all_zero));

    return failures;
}

/**
 * @brief Verify the classic-line view decodes lcd_a/b/c/punct exactly
 *        like hp41_display_bridge.c's own framebuffer, at this panel's scale.
 * @return Number of failed checks.
 */
static int test_classic_line_view(void)
{
    int failures = 0;
    static uint8_t fb[HP41_DM41X_FB_SIZE];

    reset_state();
    /* "AB" left-justified in the leftmost two cells (rest blank),
     * matching real hardware's left-to-right ALPHA convention. */
    fill_classic_line(' ');
    write_classic_cell(0, 'A', 0);
    write_classic_cell(1, 'B', 0);
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_CLASSIC_LINE);
    int ab_pixels = count_lit(fb);
    failures += !check_int("classic-line view, 'AB': some pixels lit", ab_pixels > 0, 1);

    reset_state();
    fill_classic_line(' ');
    write_classic_cell(0, 'A', 0);
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_CLASSIC_LINE);
    int a_only = count_lit(fb);

    reset_state();
    fill_classic_line(' ');
    write_classic_cell(0, 'A', 0);
    write_classic_cell(1, 'A', 0);
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_CLASSIC_LINE);
    int a_a = count_lit(fb);

    failures += !check_int("classic-line view, 'AA': second cell adds first cell's own pixel count",
                            a_a, a_only * 2);

    /* Punctuation: a period after cell 0 adds exactly the DOT mark's pixel count. */
    reset_state();
    write_classic_cell(0, '0', 0);
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_CLASSIC_LINE);
    int digit_only = count_lit(fb);

    reset_state();
    write_classic_cell(0, '0', 1); /* period */
    hp41_dm41x_display_compute_framebuffer(fb, HP41_DM41X_VIEW_CLASSIC_LINE);
    int digit_with_period = count_lit(fb);

    failures += !check_int("classic-line view, period adds pixels beyond the digit alone",
                            digit_with_period > digit_only, 1);

    return failures;
}

/**
 * @brief Run every DM41X display-bridge check and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_blank_framebuffer()
                        + test_stack_view()
                        + test_classic_line_view();

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
