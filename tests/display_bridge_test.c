/* Native (host) test for the display bridge (firmware/hp41_display_bridge.c).
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
 * Build (from repo root):
 *   cc -std=gnu11 -fcommon \
 *      -Iemu41gcc -Ifirmware/emu41gcc_compat -Ifirmware -Ifont-tables \
 *      -include firmware/emu41gcc_compat/nut_stubs.h \
 *      -o tests/build/display_bridge_test tests/display_bridge_test.c \
 *      emu41gcc/nutcpu.c emu41gcc/display.c \
 *      firmware/emu41gcc_compat/nut_stubs.c \
 *      firmware/emu41gcc_compat/nut_globals.c \
 *      firmware/emu41gcc_compat/nut_rom.c \
 *      firmware/hp41_display_bridge.c \
 *      roms/rom_images.c font-tables/hp41_display_tables.c
 *   ./tests/build/display_bridge_test
 */

#include <stdio.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "nut_rom.h"
#include "hp41_display_bridge.h"
#include "hp41_display_tables.h" /* hp41_annunciator_bits/pixel_count, for the annunciator check below */
#include "st7920.h" /* LCD_WIDTH_PX/LCD_HEIGHT_PX/LCD_FB_SIZE/LCD_BYTES_PER_ROW only - st7920_draw_frame() itself is never called/linked here */

/* lcd_ann is a plain global in emu41gcc/display.c (no header exposes it -
 * same situation as lcd_a/b/c, see hp41_display_bridge.c) - poked
 * directly here to exercise the annunciator render path, which the
 * "MEMORY LOST" boot screen never touches (lcd_ann==0 at that point).
 */
extern int lcd_ann;

static int get_px(const uint8_t *fb, int x, int y)
{
    return (fb[y * LCD_BYTES_PER_ROW + x / 8] >> (7 - (x % 8))) & 1;
}

static int count_lit(const uint8_t *fb)
{
    int lit = 0;
    for (int y = 0; y < LCD_HEIGHT_PX; y++)
        for (int x = 0; x < LCD_WIDTH_PX; x++)
            lit += get_px(fb, x, y);
    return lit;
}

/* hp41_display_bridge.c's hp41_display_render() calls st7920_draw_frame()
 * (real GPIO/ST7920 code, firmware/st7920.c - Pico-only, doesn't build
 * on the host). This test only calls hp41_display_compute_framebuffer(),
 * never hp41_display_render(), but the linker still needs the symbol
 * resolved since it's referenced from the same translation unit.
 */
void st7920_draw_frame(const uint8_t *fb) { (void)fb; }

int main(void)
{
    uint8_t fb[LCD_FB_SIZE];
    int ret;
    const int max_instr = 2000000;

    nut_boot();
    do {
        ret = executeNUT(1000);
    } while (ret == 0 && cptinstr < max_instr);

    printf("executeNUT stopped: ret=%d, instructions=%d\n", ret, cptinstr);

    hp41_display_compute_framebuffer(fb);

    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        for (int x = 0; x < LCD_WIDTH_PX; x++)
            putchar(get_px(fb, x, y) ? '#' : '.');
        putchar('\n');
    }
    int chars_lit = count_lit(fb);
    printf("total lit pixels (chars only, lcd_ann==0 at coldstart): %d\n", chars_lit);

    /* Annunciator check: the boot screen above never lights lcd_ann, so
     * exercise that path directly. Each bit, one at a time, should add
     * exactly hp41_annunciator_pixel_count[i] pixels; all 12 at once
     * should add exactly their sum (300, per the extraction - see
     * font-tables/hp41_annunciator_pixel_map.json) with none overlapping
     * each other or the character cells.
     */
    int fail = 0;
    int total_expected = 0;
    for (int i = 0; i < HP41_NUM_ANNUNCIATORS; i++) {
        lcd_ann = hp41_annunciator_bits[i];
        hp41_display_compute_framebuffer(fb);
        int got = count_lit(fb) - chars_lit;
        int want = hp41_annunciator_pixel_count[i];
        total_expected += want;
        printf("annunciator[%2d] bit=0x%03X: got %d px, want %d px%s\n",
               i, hp41_annunciator_bits[i], got, want,
               got == want ? "" : "  <-- MISMATCH");
        if (got != want) fail = 1;
    }

    lcd_ann = 0xFFF; /* all 12 annunciators on at once */
    hp41_display_compute_framebuffer(fb);
    int all_got = count_lit(fb) - chars_lit;
    printf("all annunciators at once: got %d px, want %d px%s\n",
           all_got, total_expected,
           all_got == total_expected ? "" : "  <-- MISMATCH (overlap or gap)");
    if (all_got != total_expected) fail = 1;

    if (fail) {
        printf("FAIL: annunciator rendering doesn't match the pixel map\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
