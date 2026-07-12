/* Bridges emu41gcc's Nut CPU display registers (lcd_a/b/c/lcd_ann, in
 * emu41gcc/display.c) to the real ST7920 panel, using the compile-time
 * segment/pixel/annunciator tables generated from hp41_font_table.json,
 * hp41_pixel_segment_map.json, and hp41_annunciator_pixel_map.json (see
 * font-tables/gen_display_tables.py).
 */

#include "hp41_display_bridge.h"

#include <string.h>

#include "st7920.h"
#include "hp41_display_tables.h"

/* lcd_a/b/c/lcd_ann are plain globals in emu41gcc/display.c with no
 * header exposing them (display.h only declares functions) - declared
 * extern here directly, same pattern used elsewhere in this project for
 * vendored globals that have no header of their own (e.g. rom_images.c's
 * ROM arrays in firmware/emu41gcc_compat/nut_rom.c).
 */
extern unsigned char lcd_a[HP41_NUM_CELLS];
extern unsigned char lcd_b[HP41_NUM_CELLS];
extern unsigned char lcd_c[HP41_NUM_CELLS];
extern int lcd_ann;

/* Same raw-code-to-ASCII decode as emu41gcc/display.c's static
 * alpha41() - reimplemented here (rather than exposing that static
 * function, which would mean touching the vendored file) because this
 * exact decode is what's already validated correct: it's what produced
 * "MEMORY LOST" via display_to_buf() in the first Nut CPU boot test
 * (see CLAUDE.md, tests/nut_smoke_test.c). v is the raw HP-41 display
 * code: (lcd_c[i]<<8) | ((lcd_b[i]&3)<<4) | lcd_a[i].
 */
static int hp41_decode_ascii(int v)
{
    v &= 0x13f;
    if (v <= 0x1f)
        return v + '@';
    if (v <= 0x3f) {
        if (v == 0x2c) return '<';  /* backward flying goose */
        if (v == 0x2e) return '>';  /* flying goose */
        if (v == 0x3a) return '*';  /* starburst */
        return v;
    }
    if (v <= 0x105)
        return v - 0xa0;
    if (v <= 0x11f) {
        switch (v) {
            case 0x106: return '~';  /* top bar */
            case 0x107: return '\''; /* append */
            case 0x10c: return 'u';  /* micro */
            case 0x10d: return '#';  /* different sign */
            case 0x10e: return 's';  /* sigma */
            case 0x10f: return 'a';  /* angle */
            default:    return 'x';  /* non-displayable */
        }
    }
    return v - 0x120 + 'a' - 1;
}

static inline void set_px(uint8_t *fb, int x, int y)
{
    fb[y * LCD_BYTES_PER_ROW + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

static void plot_segment(uint8_t *fb, int cell_x0, int seg_index)
{
    uint8_t off = hp41_segment_pixel_offset[seg_index];
    uint8_t cnt = hp41_segment_pixel_count[seg_index];
    for (uint8_t k = 0; k < cnt; k++) {
        hp41_pixel_t p = hp41_segment_pixels[off + k];
        set_px(fb, cell_x0 + p.x, p.y);
    }
}

static void plot_annunciator(uint8_t *fb, int ann_index)
{
    uint8_t off = hp41_annunciator_pixel_offset[ann_index];
    uint8_t cnt = hp41_annunciator_pixel_count[ann_index];
    for (uint8_t k = 0; k < cnt; k++) {
        hp41_pixel_t p = hp41_annunciator_pixels[off + k];
        set_px(fb, p.x, p.y); /* absolute, not per-cell */
    }
}

void hp41_display_compute_framebuffer(uint8_t *fb)
{
    memset(fb, 0, LCD_FB_SIZE);

    for (int pos = 0; pos < HP41_NUM_CELLS; pos++) {
        /* lcd_*[11] is the leftmost screen position, [0] the rightmost -
         * matches display.c's display_to_buf(), which walks i from
         * DSIZE-1 down to 0 to build its left-to-right string. */
        int i = (HP41_NUM_CELLS - 1) - pos;
        int v = (lcd_c[i] << 8) | ((lcd_b[i] & 3) << 4) | lcd_a[i];
        int ascii = hp41_decode_ascii(v) & 0x7f;
        int punct = lcd_b[i] >> 2;
        int cell_x0 = pos * HP41_CELL_WIDTH_PX;

        uint16_t segbits = hp41_char_segments[ascii];
        for (int b = 0; b < 14; b++) {
            if (segbits & (1u << b))
                plot_segment(fb, cell_x0, b);
        }

        switch (punct) {
            case 1: /* period */
                plot_segment(fb, cell_x0, HP41_SEG_DOT_BOTTOM);
                break;
            case 2: /* colon */
                plot_segment(fb, cell_x0, HP41_SEG_DOT_TOP);
                plot_segment(fb, cell_x0, HP41_SEG_DOT_BOTTOM);
                break;
            case 3: /* comma */
                plot_segment(fb, cell_x0, HP41_SEG_DOT_BOTTOM);
                plot_segment(fb, cell_x0, HP41_SEG_COMMA_TAIL);
                break;
            default:
                break;
        }
    }

    for (int a = 0; a < HP41_NUM_ANNUNCIATORS; a++) {
        if (lcd_ann & hp41_annunciator_bits[a])
            plot_annunciator(fb, a);
    }
}

void hp41_display_render(void)
{
    static uint8_t framebuf[LCD_FB_SIZE];
    hp41_display_compute_framebuffer(framebuf);
    st7920_draw_frame(framebuf);
}
