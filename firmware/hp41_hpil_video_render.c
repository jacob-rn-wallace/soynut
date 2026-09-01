/**
 * @file hp41_hpil_video_render.c
 * @brief Implementation - see hp41_hpil_video_render.h for the full
 *        contract and why this is a separate module from
 *        hp41_quad_display_bridge.c.
 */

#include "hp41_hpil_video_render.h"

#include <assert.h>
#include <string.h>

#include "hp41_hpil_video_bridge.h"
#include "hp41_quad_font_table.h" /* HP41_QUAD_DISP_WIDTH_PX/HEIGHT_PX - the one panel-geometry fact this file needs from the classic-display side */
#include "ilvideo_font.h"
#include "ilvideo_term.h" /* ILVIDEO_COLS/ILVIDEO_VISIBLE_ROWS */

#define DISP_WIDTH_BYTES ((HP41_QUAD_DISP_WIDTH_PX + 7) / 8)

/* Centers the 32x16 grid (12x15 pitch = 384x240) within the 400x240
 * panel: an 8px margin each side horizontally, an exact fit vertically
 * (no margin needed) - see ilvideo-native/font/gen_ascii_font.py's own
 * docstring for the derivation. */
#define GRID_X0_PX ((HP41_QUAD_DISP_WIDTH_PX - ILVIDEO_COLS * ILVIDEO_FONT_COL_PITCH_PX) / 2)
#define GRID_Y0_PX 0

/**
 * @brief Clear (light -> dark) one pixel's bit in the framebuffer.
 * @param fb Framebuffer, DISP_WIDTH_BYTES * HP41_QUAD_DISP_HEIGHT_PX bytes.
 * @param x Absolute panel column, 0..HP41_QUAD_DISP_WIDTH_PX-1.
 * @param y Absolute panel row, 0..HP41_QUAD_DISP_HEIGHT_PX-1.
 */
static void set_pixel_dark(uint8_t *fb, int x, int y) {
    assert(x >= 0 && x < HP41_QUAD_DISP_WIDTH_PX);
    assert(y >= 0 && y < HP41_QUAD_DISP_HEIGHT_PX);
    fb[y * DISP_WIDTH_BYTES + (x / 8)] &= (uint8_t)~(0x80 >> (x % 8));
}

void hp41_hpil_video_render_into(uint8_t *fb) {
    int col;
    int row;

    assert(fb != NULL);
    /* both are compile-time-constant, but genuinely meaningful: they'd
     * catch a future accidental mismatch between this grid's geometry
     * and the panel's, from either side changing independently */
    assert(ILVIDEO_COLS * ILVIDEO_FONT_COL_PITCH_PX <= HP41_QUAD_DISP_WIDTH_PX);
    assert(ILVIDEO_VISIBLE_ROWS * ILVIDEO_FONT_ROW_PITCH_PX <= HP41_QUAD_DISP_HEIGHT_PX);

    memset(fb, 0xFF, (size_t)DISP_WIDTH_BYTES * HP41_QUAD_DISP_HEIGHT_PX);

    for (row = 0; row < ILVIDEO_VISIBLE_ROWS; ++row) {
        for (col = 0; col < ILVIDEO_COLS; ++col) {
            uint8_t ch = hp41_hpil_video_bridge_char_at(col, row);
            const uint8_t *glyph_rows = ilvideo_font_glyph_rows(ch);
            int cell_x0 = GRID_X0_PX + col * ILVIDEO_FONT_COL_PITCH_PX;
            int cell_y0 = GRID_Y0_PX + row * ILVIDEO_FONT_ROW_PITCH_PX;
            /* center the 8x14 glyph within its 12x15 cell */
            int glyph_x0 = cell_x0 + (ILVIDEO_FONT_COL_PITCH_PX - ILVIDEO_FONT_WIDTH_PX) / 2;
            int glyph_y0 = cell_y0 + (ILVIDEO_FONT_ROW_PITCH_PX - ILVIDEO_FONT_HEIGHT_PX) / 2;
            int gy;

            for (gy = 0; gy < ILVIDEO_FONT_HEIGHT_PX; ++gy) {
                uint8_t byte = glyph_rows[gy];
                int gx;

                for (gx = 0; gx < ILVIDEO_FONT_WIDTH_PX; ++gx) {
                    if ((byte & (0x80 >> gx)) != 0) {
                        set_pixel_dark(fb, glyph_x0 + gx, glyph_y0 + gy);
                    }
                }
            }
        }
    }
}
