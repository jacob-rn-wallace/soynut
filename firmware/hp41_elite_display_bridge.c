/**
 * @file hp41_elite_display_bridge.c
 * @brief Implements Elite User Mode's register decode and 3x5-pixel
 *        rendering - see hp41_elite_display_bridge.h and CLAUDE.md's
 *        "Elite User Mode" section for the full design.
 */

#include "hp41_elite_display_bridge.h"

#include <assert.h>
#include <string.h>

#include "st7920.h"
#include "hp41_elite_font_table.h"

#define GLOBAL extern
#include "nutcpu.h"

/* lcd_ann is a plain global in emu41gcc/display.c with no header
 * exposing it - same pattern hp41_display_bridge.c already uses. */
extern int lcd_ann;

#include "hp41_display_tables.h" /* hp41_annunciator_bits/_pixels/_offset/_count - reused, see below */

/** Elite Mode's 24-column x 4-row grid geometry (see CLAUDE.md, derived from the user's own mockup). */
enum {
    HP41_ELITE_NUM_COLS = 24,
    HP41_ELITE_NUM_ROWS = 4,
    HP41_ELITE_COL_PITCH_PX = 6,
    HP41_ELITE_ROW_PITCH_PX = 6,
    HP41_ELITE_GRID_X0 = 1,
    HP41_ELITE_GRID_Y0 = 1,
    HP41_ELITE_ANNUNCIATOR_Y_OFFSET = 5,
};

/** Numeric row column assignments (see CLAUDE.md's punctuation-pixel semantics table). */
enum {
    HP41_ELITE_COL_MANTISSA_SIGN = 0,
    HP41_ELITE_COL_MANTISSA_FIRST = 1,  /* digits 1-10 occupy columns 1-10 */
    HP41_ELITE_COL_EXPONENT_SIGN = 11,
    HP41_ELITE_COL_EXPONENT_TENS = 12,
    HP41_ELITE_COL_EXPONENT_UNITS = 13,
};

/** espaceRAM register index holding the most-recently-typed ALPHA text (see CLAUDE.md). */
#define HP41_ELITE_ALPHA_ECHO_REG 5
/** How many trailing characters of ALPHA-mode entry the echo register reliably holds. */
#define HP41_ELITE_ALPHA_ECHO_CHARS 7

/**
 * @brief Set one pixel in a 1bpp, row-major, MSB-first framebuffer.
 *
 * Duplicated from hp41_display_bridge.c's own set_px() rather than
 * shared via a new header - it's four lines, and this project doesn't
 * currently have a shared low-level pixel-plot header to put it in.
 *
 * @param fb Framebuffer to modify, at least LCD_FB_SIZE bytes.
 * @param x  Absolute column, 0 to LCD_WIDTH_PX-1.
 * @param y  Absolute row, 0 to LCD_HEIGHT_PX-1.
 */
static inline void set_px(uint8_t *fb, int x, int y)
{
    assert(fb != NULL);
    assert(x >= 0 && x < LCD_WIDTH_PX && y >= 0 && y < LCD_HEIGHT_PX);
    fb[y * LCD_BYTES_PER_ROW + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

/**
 * @brief Plot one 3x5 glyph at a given grid cell.
 *
 * @param fb   Framebuffer to modify, at least LCD_FB_SIZE bytes.
 * @param col  Grid column, 0 to HP41_ELITE_NUM_COLS-1.
 * @param row  Grid row, 0 to HP41_ELITE_NUM_ROWS-1.
 * @param code HP-41 display code (0-127) to look up in hp41_elite_glyph_rows.
 */
static void plot_elite_glyph(uint8_t *fb, int col, int row, int code)
{
    assert(fb != NULL);
    assert(col >= 0 && col < HP41_ELITE_NUM_COLS);
    assert(row >= 0 && row < HP41_ELITE_NUM_ROWS);
    assert(code >= 0 && code < 128);

    int x0 = HP41_ELITE_GRID_X0 + col * HP41_ELITE_COL_PITCH_PX;
    int y0 = HP41_ELITE_GRID_Y0 + row * HP41_ELITE_ROW_PITCH_PX;
    for (int r = 0; r < HP41_ELITE_GLYPH_HEIGHT_PX; r++) {
        uint8_t bits = hp41_elite_glyph_rows[code][r];
        for (int c = 0; c < HP41_ELITE_GLYPH_WIDTH_PX; c++) {
            if (bits & (1u << (2 - c)))
                set_px(fb, x0 + c, y0 + r);
        }
    }
}

/**
 * @brief Plot the decimal-point / mantissa-exponent-separator marks for one row.
 *
 * See CLAUDE.md's punctuation-pixel semantics: a decimal point always
 * follows the leading mantissa digit (column 1), and a separator always
 * follows the last mantissa digit (column 10) - fixed positions, since
 * this project's numbers are always rendered as D.DDDDDDDDD, never a
 * variable-width format.
 *
 * @param fb  Framebuffer to modify, at least LCD_FB_SIZE bytes.
 * @param row Grid row, 0 to HP41_ELITE_NUM_ROWS-1.
 */
static void plot_elite_numeric_punctuation(uint8_t *fb, int row)
{
    assert(fb != NULL);
    assert(row >= 0 && row < HP41_ELITE_NUM_ROWS);

    int row_y0 = HP41_ELITE_GRID_Y0 + row * HP41_ELITE_ROW_PITCH_PX;
    int decimal_point_x = HP41_ELITE_GRID_X0 + 4 + HP41_ELITE_COL_MANTISSA_FIRST * HP41_ELITE_COL_PITCH_PX;
    int separator_x = HP41_ELITE_GRID_X0 + 4 + 10 * HP41_ELITE_COL_PITCH_PX;

    set_px(fb, decimal_point_x, row_y0 + 5); /* dot_bottom, after mantissa digit 1 */
    set_px(fb, separator_x, row_y0 + 4);     /* comma, after mantissa digit 10 */
}

/**
 * @brief Plot the annunciator row, reusing normal mode's table with a fixed y-offset.
 *
 * Confirmed empirically against the user's own mockup: the same 12
 * annunciators, at the same x-positions as normal mode, just shifted
 * +5px down (see CLAUDE.md) - no separate table needed.
 *
 * @param fb Framebuffer to modify, at least LCD_FB_SIZE bytes.
 */
static void plot_elite_annunciators(uint8_t *fb)
{
    assert(fb != NULL);
    for (int a = 0; a < HP41_NUM_ANNUNCIATORS; a++) {
        if (!(lcd_ann & hp41_annunciator_bits[a]))
            continue;
        uint8_t off = hp41_annunciator_pixel_offset[a];
        uint8_t cnt = hp41_annunciator_pixel_count[a];
        for (uint8_t k = 0; k < cnt; k++) {
            hp41_pixel_t p = hp41_annunciator_pixels[off + k];
            set_px(fb, p.x, p.y + HP41_ELITE_ANNUNCIATOR_Y_OFFSET);
        }
    }
}

/**
 * @brief Decode one stack register (T/Z/Y/X) directly from espaceRAM.
 *
 * See hp41_elite_display_bridge.h for the public contract; this is the
 * implementation. Nibble layout and packing confirmed directly from
 * emu41gcc/nutcpu.c's recallData()/storeData() and exec2()'s
 * field-selector table - see CLAUDE.md.
 */
void hp41_elite_decode_register(int stack_index, hp41_elite_number_t *out)
{
    assert(stack_index >= HP41_ELITE_REG_T && stack_index <= HP41_ELITE_REG_X);
    assert(out != NULL);

    int base = stack_index * 8;
    /* Byte 0 is a write-protect flag; documented as essentially always 0
     * for the stack registers - advisory check only, not a behavior gate. */
    assert(espaceRAM[base] == 0);

    uint8_t nibble[14];
    for (int i = 0; i < 7; i++) {
        uint8_t b = espaceRAM[base + 1 + i];
        nibble[2 * i] = b & 0x0F;
        nibble[2 * i + 1] = (b >> 4) & 0x0F;
    }
    for (int i = 0; i < 14; i++)
        assert(nibble[i] <= 9);

    out->exponent_negative = (nibble[0] == 9);
    out->exponent_tens = nibble[2];
    out->exponent_units = nibble[1];
    out->mantissa_negative = (nibble[13] == 9);
    for (int k = 1; k <= 10; k++)
        out->mantissa_digits[k - 1] = nibble[13 - k];

    assert(out->exponent_tens <= 9 && out->exponent_units <= 9);
}

/**
 * @brief Plot one fully-formatted stack register into a grid row.
 *
 * @param fb  Framebuffer to modify, at least LCD_FB_SIZE bytes.
 * @param row Grid row, 0 to HP41_ELITE_NUM_ROWS-1.
 * @param n   Decoded register to render.
 */
static void plot_elite_number_row(uint8_t *fb, int row, const hp41_elite_number_t *n)
{
    assert(fb != NULL);
    assert(n != NULL);

    plot_elite_glyph(fb, HP41_ELITE_COL_MANTISSA_SIGN, row, n->mantissa_negative ? '-' : ' ');
    for (int d = 0; d < 10; d++)
        plot_elite_glyph(fb, HP41_ELITE_COL_MANTISSA_FIRST + d, row, '0' + n->mantissa_digits[d]);
    plot_elite_numeric_punctuation(fb, row);
    plot_elite_glyph(fb, HP41_ELITE_COL_EXPONENT_SIGN, row, n->exponent_negative ? '-' : ' ');
    plot_elite_glyph(fb, HP41_ELITE_COL_EXPONENT_TENS, row, '0' + n->exponent_tens);
    plot_elite_glyph(fb, HP41_ELITE_COL_EXPONENT_UNITS, row, '0' + n->exponent_units);
}

/**
 * @brief Plot the most recently typed ALPHA-mode text into a grid row.
 *
 * Reads espaceRAM register 5 directly (see CLAUDE.md for how this was
 * confirmed empirically): 7 bytes, plain 8-bit ASCII, most-recently-
 * typed character first. Reversed here into typing order and trimmed
 * of trailing (chronologically-oldest) NUL bytes, then plotted
 * left-aligned - only the most recent HP41_ELITE_ALPHA_ECHO_CHARS
 * characters are ever available this way, not the full 24-character
 * ALPHA register.
 *
 * @param fb  Framebuffer to modify, at least LCD_FB_SIZE bytes.
 * @param row Grid row, 0 to HP41_ELITE_NUM_ROWS-1.
 */
static void plot_elite_alpha_row(uint8_t *fb, int row)
{
    assert(fb != NULL);
    assert(row >= 0 && row < HP41_ELITE_NUM_ROWS);

    int base = HP41_ELITE_ALPHA_ECHO_REG * 8;
    uint8_t recent[HP41_ELITE_ALPHA_ECHO_CHARS];
    for (int i = 0; i < HP41_ELITE_ALPHA_ECHO_CHARS; i++)
        recent[i] = espaceRAM[base + 1 + i]; /* byte0 is the write-protect flag */

    /* recent[0] is most-recently-typed; find how many trailing (oldest)
     * slots are still unused (0x00), then plot the used ones in typing
     * order (oldest-of-the-recent-window first). */
    int used = HP41_ELITE_ALPHA_ECHO_CHARS;
    while (used > 0 && recent[used - 1] == 0)
        used--;
    assert(used >= 0 && used <= HP41_ELITE_ALPHA_ECHO_CHARS);

    for (int i = 0; i < used; i++) {
        uint8_t ch = recent[used - 1 - i]; /* oldest-of-the-window first */
        int code = (ch < 128) ? ch : ' ';
        plot_elite_glyph(fb, i, row, code);
    }
}

/**
 * @brief Shared setup for both elite framebuffer entry points.
 *
 * @param fb Output buffer, at least LCD_FB_SIZE bytes; fully overwritten.
 */
static void elite_framebuffer_init(uint8_t *fb)
{
    assert(fb != NULL);
    /* Rightmost pixel used is the last column's glyph, 3px wide, at
     * column index (NUM_COLS-1) - not NUM_COLS*PITCH, which would
     * overcount by one full pitch (a real off-by-one caught by this
     * project's own native tests, not just reasoned about). */
    assert(HP41_ELITE_GRID_X0 + (HP41_ELITE_NUM_COLS - 1) * HP41_ELITE_COL_PITCH_PX
           + HP41_ELITE_GLYPH_WIDTH_PX <= LCD_WIDTH_PX);
    memset(fb, 0, LCD_FB_SIZE);
}

void hp41_elite_display_compute_framebuffer(uint8_t *fb)
{
    elite_framebuffer_init(fb);
    for (int row = 0; row < HP41_ELITE_NUM_ROWS; row++) {
        hp41_elite_number_t n;
        hp41_elite_decode_register(row, &n);
        plot_elite_number_row(fb, row, &n);
    }
    plot_elite_annunciators(fb);
}

void hp41_elite_display_compute_framebuffer_alpha(uint8_t *fb)
{
    elite_framebuffer_init(fb);
    for (int row = 0; row < HP41_ELITE_NUM_ROWS - 1; row++) {
        hp41_elite_number_t n;
        hp41_elite_decode_register(row, &n);
        plot_elite_number_row(fb, row, &n);
    }
    plot_elite_alpha_row(fb, HP41_ELITE_NUM_ROWS - 1);
    plot_elite_annunciators(fb);
}
