/**
 * @file hp41_quad_display_bridge.c
 * @brief Implements hp41_quad_display_compute_framebuffer() - see
 *        hp41_quad_display_bridge.h for the public contract.
 */

#include "hp41_quad_display_bridge.h"

#include <assert.h>
#include <string.h>

#include "hp41_ascii_decode.h"
#include "hp41_register_decode.h"
#include "hp41_register_format.h"

/** The real HP-41's own physical character-cell count - lcd_a/b/c below
 *  are sized to this, matching hp41_display_bridge.c's own HP41_NUM_CELLS. */
#define HP41_QUAD_NUM_CLASSIC_CELLS 12

/* lcd_a/b/c/lcd_ann are plain globals in emu41gcc/display.c with no
 * header exposing them - same pattern hp41_display_bridge.c already
 * uses for the classic-line view's exact same underlying state. */
extern unsigned char lcd_a[HP41_QUAD_NUM_CLASSIC_CELLS];
extern unsigned char lcd_b[HP41_QUAD_NUM_CLASSIC_CELLS];
extern unsigned char lcd_c[HP41_QUAD_NUM_CLASSIC_CELLS];
extern int lcd_ann;

/**
 * @brief Set one pixel in the Sharp panel's framebuffer, dark-on-white polarity.
 *
 * The opposite convention from hp41_display_bridge.c's set_px(): this
 * panel is cleared to 0xFF (white), and a lit (visible, dark) segment
 * clears its bit rather than setting it - confirmed on real hardware in
 * quad_bringup/ (see CLAUDE.md's "Sharp Memory LCD bring-up" section).
 *
 * @param fb Framebuffer to modify, at least HP41_QUAD_FB_SIZE bytes.
 * @param x  Absolute column, 0 to HP41_QUAD_DISP_WIDTH_PX-1.
 * @param y  Absolute row, 0 to HP41_QUAD_DISP_HEIGHT_PX-1.
 */
static inline void set_px(uint8_t *fb, int x, int y)
{
    assert(fb != NULL);
    assert(x >= 0 && x < HP41_QUAD_DISP_WIDTH_PX);
    assert(y >= 0 && y < HP41_QUAD_DISP_HEIGHT_PX);
    int bytes_per_row = HP41_QUAD_DISP_WIDTH_PX / 8;
    fb[y * bytes_per_row + x / 8] &= (uint8_t)~(0x80 >> (x % 8));
}

/**
 * @brief Plot one named segment/mark's pixels into a character cell.
 *
 * @param fb        Framebuffer to modify, at least HP41_QUAD_FB_SIZE bytes.
 * @param cell_x0   Absolute x offset of this cell's top-left corner.
 * @param cell_y0   Absolute y offset of this cell's top-left corner.
 * @param seg_index Segment/mark index (0-13 = SEGMENT_ORDER, 14-16 =
 *                  HP41_QUAD_SEG_DOT/_SEP/_COLON).
 */
static void plot_segment(uint8_t *fb, int cell_x0, int cell_y0, int seg_index)
{
    assert(fb != NULL);
    assert(seg_index >= 0 && seg_index < HP41_QUAD_NUM_SEGMENTS);
    uint16_t off = hp41_quad_segment_pixel_offset[seg_index];
    uint16_t cnt = hp41_quad_segment_pixel_count[seg_index];
    for (uint16_t k = 0; k < cnt; k++) {
        hp41_quad_pixel_t p = hp41_quad_segment_pixels[off + k];
        set_px(fb, cell_x0 + p.x, cell_y0 + p.y);
    }
}

/** How far right of a cell's own boundary to shift a punctuation mark
 *  before plotting it (see plot_mark()) - half the inter-cell gap,
 *  centering the mark in the gap instead of leaving it glued to the
 *  boundary. Magellan's DECIMAL_POINT/COMMA/COLON polygons are all
 *  centered exactly at x=CELL_WIDTH (the cell's own right edge) by
 *  design - which is also where a digit's own rightmost segments
 *  (b/c) already reach, confirmed on real hardware: at this table's
 *  first geometry, the mark and the preceding digit's own segments
 *  directly overlapped (both reaching pixel x=26), making the mark
 *  unreadable as a distinct decimal point rather than noise on the
 *  digit itself. Widening the gap alone didn't fix this - the overlap
 *  is with the digit the mark trails, which sits at a fixed offset
 *  from the cell boundary regardless of how much *further* gap space
 *  exists past it - so the mark itself has to move, not just the gap. */
#define HP41_QUAD_MARK_X_OFFSET ((HP41_QUAD_COL_PITCH_PX - HP41_QUAD_CELL_WIDTH_PX) / 2)

/**
 * @brief Plot one punctuation mark (DOT/SEP/COLON), shifted into the
 *        middle of the gap after its cell - see HP41_QUAD_MARK_X_OFFSET.
 *
 * @param fb        Framebuffer to modify, at least HP41_QUAD_FB_SIZE bytes.
 * @param cell_x0   Absolute x offset of the preceding cell's top-left corner.
 * @param cell_y0   Absolute y offset of the preceding cell's top-left corner.
 * @param seg_index One of HP41_QUAD_SEG_DOT/_SEP/_COLON.
 */
static void plot_mark(uint8_t *fb, int cell_x0, int cell_y0, int seg_index)
{
    assert(fb != NULL);
    assert(seg_index == HP41_QUAD_SEG_DOT || seg_index == HP41_QUAD_SEG_SEP
           || seg_index == HP41_QUAD_SEG_COLON);
    plot_segment(fb, cell_x0 + HP41_QUAD_MARK_X_OFFSET, cell_y0, seg_index);
}

/**
 * @brief Plot one ASCII character's lit segments into a character cell.
 *
 * @param fb      Framebuffer to modify, at least HP41_QUAD_FB_SIZE bytes.
 * @param ascii   Character code, 0-127 (only entries hp41_quad_char_segments
 *                actually populates render as anything - see that table's
 *                own header comment for which those are).
 * @param cell_x0 Absolute x offset of this cell's top-left corner.
 * @param cell_y0 Absolute y offset of this cell's top-left corner.
 */
static void plot_char(uint8_t *fb, int ascii, int cell_x0, int cell_y0)
{
    assert(fb != NULL);
    assert(ascii >= 0 && ascii < 128);
    uint16_t segbits = hp41_quad_char_segments[ascii];
    for (int b = 0; b < 14; b++)
        if (segbits & (1u << b))
            plot_segment(fb, cell_x0, cell_y0, b);
}

/**
 * @brief Plot one formatted number's field sequence into the Stack
 *        view's grid, left-justified starting at column 0.
 *
 * @param fb  Framebuffer to modify, at least HP41_QUAD_FB_SIZE bytes.
 * @param f   Formatted field sequence (see hp41_register_format.h).
 * @param row Grid row, 0 to HP41_QUAD_NUM_ROWS-1.
 */
static void plot_formatted_row(uint8_t *fb, const hp41_formatted_number_t *f, int row)
{
    assert(fb != NULL);
    assert(f != NULL);
    assert(row >= 0 && row < HP41_QUAD_NUM_ROWS);
    assert(f->num_fields <= HP41_QUAD_NUM_COLS);

    int row_y0 = HP41_QUAD_GRID_Y0 + row * HP41_QUAD_ROW_PITCH_PX;
    for (int col = 0; col < f->num_fields; col++) {
        int cell_x0 = HP41_QUAD_GRID_X0 + col * HP41_QUAD_COL_PITCH_PX;
        const hp41_display_field_t *field = &f->fields[col];
        int ascii = (field->kind == HP41_FIELD_MINUS) ? '-' : '0' + field->digit;
        plot_char(fb, ascii, cell_x0, row_y0);
        if (field->decimal_point_after)
            plot_mark(fb, cell_x0, row_y0, HP41_QUAD_SEG_DOT);
        if (field->separator_after)
            plot_mark(fb, cell_x0, row_y0, HP41_QUAD_SEG_SEP);
    }
}

/**
 * @brief Render the Stack view: T, Z, Y, X, each real-formatted.
 * @param fb Framebuffer to modify, at least HP41_QUAD_FB_SIZE bytes.
 */
static void render_stack_view(uint8_t *fb)
{
    assert(fb != NULL);
    hp41_display_format_t fmt;
    hp41_read_display_format(&fmt);

    static const int rows[4] = {
        HP41_ELITE_REG_T, HP41_ELITE_REG_Z, HP41_ELITE_REG_Y, HP41_ELITE_REG_X,
    };
    for (int row = 0; row < 4; row++) {
        hp41_elite_number_t n;
        hp41_elite_decode_register(rows[row], &n);
        hp41_formatted_number_t f;
        hp41_format_number(&n, &fmt, &f);
        plot_formatted_row(fb, &f, row);
    }
}

/**
 * @brief Render the classic-line view: the current single-line 12-cell
 *        display content, decoded exactly like hp41_display_bridge.c's
 *        own hp41_display_compute_framebuffer(), plotted at this
 *        panel's own scale.
 * @param fb Framebuffer to modify, at least HP41_QUAD_FB_SIZE bytes.
 */
static void render_classic_line_view(uint8_t *fb)
{
    assert(fb != NULL);

    for (int pos = 0; pos < HP41_QUAD_NUM_CLASSIC_CELLS; pos++) {
        /* lcd_*[11] is the leftmost screen position, [0] the rightmost -
         * matches hp41_display_bridge.c's own identical convention. */
        int i = (HP41_QUAD_NUM_CLASSIC_CELLS - 1) - pos;
        int v = (lcd_c[i] << 8) | ((lcd_b[i] & 3) << 4) | lcd_a[i];
        int ascii = hp41_decode_ascii(v) & 0x7f;
        int punct = lcd_b[i] >> 2;
        int cell_x0 = HP41_QUAD_GRID_X0 + pos * HP41_QUAD_COL_PITCH_PX;

        plot_char(fb, ascii, cell_x0, HP41_QUAD_GRID_Y0_CLASSIC);

        switch (punct) {
            case 1: /* period */
                plot_mark(fb, cell_x0, HP41_QUAD_GRID_Y0_CLASSIC, HP41_QUAD_SEG_DOT);
                break;
            case 2: /* colon */
                plot_mark(fb, cell_x0, HP41_QUAD_GRID_Y0_CLASSIC, HP41_QUAD_SEG_COLON);
                break;
            case 3: /* comma */
                plot_mark(fb, cell_x0, HP41_QUAD_GRID_Y0_CLASSIC, HP41_QUAD_SEG_SEP);
                break;
            default:
                break;
        }
    }

    /* Annunciators (BAT/USER/G/RAD/SHIFT/PRGM/ALPHA/etc, driven by
     * lcd_ann) are not yet rendered in this view - they're small
     * dedicated icon/text glyphs, not 14-segment shapes, so they need
     * new icon artwork at this panel's scale (a Magellan-side task, not
     * just more soynut pixel-table generation) rather than reusing
     * anything this table already has. Deliberately deferred, not
     * silently dropped - see CLAUDE.md's "QUAD display bridge" section. */
    (void)lcd_ann;
}

void hp41_quad_display_compute_framebuffer(uint8_t *fb, hp41_quad_view_t view)
{
    assert(fb != NULL);
    memset(fb, 0xFF, HP41_QUAD_FB_SIZE);

    switch (view) {
        case HP41_QUAD_VIEW_STACK:
            render_stack_view(fb);
            break;
        case HP41_QUAD_VIEW_CLASSIC_LINE:
            render_classic_line_view(fb);
            break;
        default:
            assert(0 && "unknown hp41_quad_view_t");
            break;
    }
}
