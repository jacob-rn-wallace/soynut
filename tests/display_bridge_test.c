/**
 * @file display_bridge_test.c
 * @brief Native (host) test for the display bridge
 *        (firmware/hp41_display_bridge.c).
 *
 * Boots the real HP-41 ROM exactly like tests/nut_smoke_test.c, then
 * calls hp41_display_compute_framebuffer() (the hardware-free half of
 * the bridge - hp41_display_render() itself also calls
 * st7920_draw_frame(), which is Pico GPIO code and can't run/link here)
 * and dumps the resulting 144x32 1bpp framebuffer as ASCII art. This is
 * the concrete check that the font table + pixel map + segment decode +
 * framebuffer packing are all wired correctly, without needing real
 * hardware: if they are, the cold-start "MEMORY LOST" message from
 * tests/nut_smoke_test.c should be visually legible below as blocky
 * 14-segment characters.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_display_bridge.h"
#include "hp41_display_tables.h" /* hp41_annunciator_bits/pixel_count, for the annunciator check below */
#include "nut_rom.h"
#include "st7920.h" /* LCD_WIDTH_PX/LCD_HEIGHT_PX/LCD_FB_SIZE/LCD_BYTES_PER_ROW only - st7920_draw_frame() itself is never called/linked here */

/* lcd_ann is a plain global in emu41gcc/display.c (no header exposes it -
 * same situation as lcd_a/b/c, see hp41_display_bridge.c) - poked
 * directly here to exercise the annunciator render path, which the
 * "MEMORY LOST" boot screen never touches (lcd_ann==0 at that point).
 */
extern int lcd_ann;

#define BATCH_SIZE 1000
#define MAX_INSTR 2000000
#define MAX_BATCHES ((MAX_INSTR / BATCH_SIZE) + 1) /* see nut_smoke_test.c's run_until_settled() */

/**
 * @brief No-op stand-in for firmware/st7920.c's real GPIO driver.
 *
 * hp41_display_bridge.c's hp41_display_render() calls st7920_draw_frame()
 * (real GPIO/ST7920 code, firmware/st7920.c - Pico-only, doesn't build
 * on the host). This test only calls hp41_display_compute_framebuffer(),
 * never hp41_display_render(), but the linker still needs the symbol
 * resolved since it's referenced from the same translation unit.
 *
 * @param fb Ignored.
 */
void st7920_draw_frame(const uint8_t *fb) { (void)fb; }

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
 * @brief Dump a framebuffer as ASCII art ('#'/'.') to stdout.
 * @param fb Framebuffer, at least LCD_FB_SIZE bytes.
 */
static void print_framebuffer(const uint8_t *fb)
{
    assert(fb != NULL);
    assert(LCD_WIDTH_PX % 8 == 0); /* get_px()'s byte/bit split relies on this */
    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        for (int x = 0; x < LCD_WIDTH_PX; x++)
            putchar(get_px(fb, x, y) ? '#' : '.');
        putchar('\n');
    }
}

/**
 * @brief Run the ROM in fixed-size batches until it stops advancing or a cap is hit.
 *
 * See nut_smoke_test.c's run_until_settled() for the Rule 2 rationale
 * (a fixed batch-count cap, not an open-ended "while (ret == 0)").
 *
 * @return executeNUT()'s last status code.
 */
static int run_until_settled(void)
{
    int ret = 0;
    int batch;
    for (batch = 0; batch < MAX_BATCHES; batch++) {
        ret = executeNUT(BATCH_SIZE);
        if (ret != 0 || cptinstr >= MAX_INSTR)
            break;
    }
    assert(batch <= MAX_BATCHES);
    assert(ret >= 0 && ret <= 3);
    return ret;
}

/**
 * @brief Verify each annunciator bit lights exactly its documented pixel count.
 *
 * Lights each annunciator bit one at a time and confirms it adds
 * exactly its documented pixel count, then all 12 at once (confirming
 * no overlap).
 *
 * @param fb        Scratch framebuffer, at least LCD_FB_SIZE bytes;
 *                  overwritten repeatedly.
 * @param chars_lit Baseline lit-pixel count from character cells alone
 *                  (subtracted out before comparing against each
 *                  annunciator's expected count).
 * @return Number of mismatches found (0 = all pass).
 */
static int check_annunciators(uint8_t *fb, int chars_lit)
{
    assert(fb != NULL);
    assert(chars_lit >= 0);
    int failures = 0;
    int total_expected = 0;

    for (int i = 0; i < HP41_NUM_ANNUNCIATORS; i++) {
        lcd_ann = hp41_annunciator_bits[i];
        hp41_display_compute_framebuffer(fb);
        const int got = count_lit(fb) - chars_lit;
        const int want = hp41_annunciator_pixel_count[i];
        total_expected += want;
        printf("annunciator[%2d] bit=0x%03X: got %d px, want %d px%s\n",
               i, hp41_annunciator_bits[i], got, want,
               got == want ? "" : "  <-- MISMATCH");
        if (got != want) failures++;
    }

    lcd_ann = 0xFFF; /* all 12 annunciators on at once */
    hp41_display_compute_framebuffer(fb);
    const int all_got = count_lit(fb) - chars_lit;
    printf("all annunciators at once: got %d px, want %d px%s\n",
           all_got, total_expected,
           all_got == total_expected ? "" : "  <-- MISMATCH (overlap or gap)");
    if (all_got != total_expected) failures++;

    return failures;
}

/**
 * @brief Boot the ROM, render the cold-start screen, and verify pixel counts.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    uint8_t fb[LCD_FB_SIZE];

    nut_boot();
    assert(regPC == 0);
    const int ret = run_until_settled();
    assert(ret >= 0 && ret <= 3);
    printf("executeNUT stopped: ret=%d, instructions=%d\n", ret, cptinstr);

    hp41_display_compute_framebuffer(fb);
    print_framebuffer(fb);
    const int chars_lit = count_lit(fb);
    printf("total lit pixels (chars only, lcd_ann==0 at coldstart): %d\n", chars_lit);

    const int failures = check_annunciators(fb, chars_lit);

    if (failures) {
        printf("FAIL: annunciator rendering doesn't match the pixel map\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
