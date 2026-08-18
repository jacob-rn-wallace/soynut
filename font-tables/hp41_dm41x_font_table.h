/**
 * @file hp41_dm41x_font_table.h
 * @brief Compile-time segment/pixel-mapping table for the DM41X-style
 *        400x240 Sharp Memory LCD display: a full-alphabet, true-to-
 *        original-size 12-column grid shared by both of its view modes.
 *
 * The arrays declared here are defined in the generated
 * hp41_dm41x_font_table.c (by gen_dm41x_segment_table.py, from the
 * Magellan project's original vector geometry - see that script for the
 * exact provenance chain and CLAUDE.md's "Sharp Memory LCD bring-up" /
 * "DM41X table" sections for context). This header itself is
 * hand-maintained, not generated - update it alongside the script if the
 * table shapes or grid geometry ever change.
 *
 * Two view modes share this exact same per-cell geometry (see the
 * Magellan plan file's "DM41X-style Sharp Memory LCD backend" for the
 * full picture):
 *   - **Classic-line view** (1 row): reuses the live lcd_a/b/c/lcd_ann
 *     buffer unchanged (same decode hp41_display_bridge.c already does
 *     for the 144x32 display) - this is the only place letters/symbols
 *     appear, so this table covers the FULL character set, unlike
 *     hp41_display_tables.h's 128-code table's predecessor design.
 *   - **Stack view** (4 rows: T/Z/Y/X): each row driven by a real,
 *     FIX/SCI/ENG-mode-aware formatter (hp41_register_format.h) that
 *     emits the same kind of per-cell field sequence the classic line
 *     already uses - so both views walk cells and plot through this one
 *     table identically, no separate rendering path per view.
 *
 * Geometry is deliberately NOT Elite Mode's always-14-fixed-column
 * convention (sign/mantissa/exponent-sign/exponent-digits each getting
 * their own column) - it's a 12-column, VARIABLE-WIDTH layout matching
 * how the real HP-41 hardware actually lays out a number (decimal point
 * and exponent separator ride in the gap after a digit's cell, not their
 * own dedicated column - see DECIMAL_POINT/COMMA below), sized to match
 * the real-world character size already visible on the *existing*
 * soynut NHD14432 device:
 *   - NHD14432: 0.42mm/px dot pitch, 12px-wide HP-41 character cell ->
 *     5.04mm real-world cell pitch.
 *   - Sharp LS027B7DH01 (this table's panel): 0.147mm/px dot pitch ->
 *     same real-world pitch is ~34.3px/column here.
 *   - 12 columns at ~34px/column is a ~2.7% trim of DISP_WIDTH_PX (411px
 *     wanted vs 400px available) - imperceptible. 14 columns wouldn't
 *     fit at all (480px).
 */
#pragma once

#include <stdint.h>

/** @name Panel geometry
 *
 * The Sharp Memory LCD (LS027B7DH01) this table targets - see
 * dm41x_bringup/main.c, which brings this exact panel up over SPI.
 * @{
 */
#define HP41_DM41X_DISP_WIDTH_PX  400
#define HP41_DM41X_DISP_HEIGHT_PX 240
/** @} */

/** @name Grid geometry
 *
 * 12 columns - the real HP-41's own physical character-cell count, not
 * Elite Mode's 14 (see this file's header comment for why). Row geometry
 * is per-view:
 *   - Stack view: 4 rows (T/Z/Y/X, top to bottom - see
 *     hp41_elite_display_bridge.h's HP41_ELITE_REG_* order), using
 *     GRID_Y0/ROW_PITCH_PX below.
 *   - Classic-line view: 1 row, vertically centered independently via
 *     GRID_Y0_CLASSIC (not tied to the 4-row Stack view's own margins).
 *
 * Fit is exact by construction - verified by gen_dm41x_segment_table.py's
 * own check_geometry_matches_header() at generation time, and again by a
 * build-time assert() in dm41x_display_bridge.c, mirroring
 * hp41_elite_display_bridge.c's own elite_framebuffer_init() fit-check:
 *   2*GRID_X0 + (NUM_COLS-1)*COL_PITCH_PX + CELL_WIDTH_PX == DISP_WIDTH_PX
 *   2*GRID_Y0 + (NUM_ROWS-1)*ROW_PITCH_PX + CELL_HEIGHT_PX == DISP_HEIGHT_PX
 * @{
 */
#define HP41_DM41X_NUM_COLS 12
#define HP41_DM41X_NUM_ROWS 4
#define HP41_DM41X_CELL_WIDTH_PX  29
#define HP41_DM41X_CELL_HEIGHT_PX 39
#define HP41_DM41X_COL_PITCH_PX 33
#define HP41_DM41X_ROW_PITCH_PX 47
#define HP41_DM41X_GRID_X0 4
#define HP41_DM41X_GRID_Y0 30
/** Classic-line view's single-row vertical offset - independently
 *  centered, not derived from the 4-row Stack view's own GRID_Y0. */
#define HP41_DM41X_GRID_Y0_CLASSIC 100
/** @} */

/** @name Segment/mark indices
 *
 * Bits 0-13 of hp41_dm41x_char_segments' mask follow Magellan's own
 * 'a'..'n' segment order (data/segments.py in the Magellan repo) - bit i
 * lights segment "abcdefghijklmn"[i]. Deliberately NOT the same bit
 * order as hp41_display_tables.h's SEGMENT_BIT_ORDER: that table exists
 * to stay compatible with the *existing* 144x32 display path, but this
 * one is a from-scratch table for a new display with no bit-level
 * compatibility requirement, so reusing Magellan's own canonical order
 * directly avoids an unnecessary id-mapping table.
 *
 * Indices 14/15 are punctuation pseudo-segments, same idiom as
 * HP41_SEG_DOT_BOTTOM/etc. in hp41_display_tables.h: not part of any
 * character's bitmask, plotted by whichever code (dm41x_display_bridge.c
 * or hp41_register_format.c) decided a cell needs one, mirroring
 * hp41_elite_display_bridge.c's plot_elite_numeric_punctuation() in
 * spirit but riding in the real gap-after-a-cell position, not a hand-
 * picked single pixel. DOT is the decimal point; SEP is the mantissa/
 * exponent separator mark - both rasterized from Magellan's actual
 * DECIMAL_POINT/COMMA polygons, which this table's generous cell pitch
 * has room for (unlike Elite Mode's cramped 6px pitch).
 * @{
 */
#define HP41_DM41X_SEG_DOT 14
#define HP41_DM41X_SEG_SEP 15
#define HP41_DM41X_NUM_SEGMENTS 16
/** @} */

/** One pixel offset: (x, y) local to a character cell. May legitimately
 *  fall outside 0..CELL_WIDTH_PX-1/0..CELL_HEIGHT_PX-1 for the two
 *  punctuation marks, which straddle the gap after their cell by design
 *  (see Magellan's segments.py) - COL_PITCH_PX/ROW_PITCH_PX leave enough
 *  margin past CELL_WIDTH_PX/CELL_HEIGHT_PX to hold this safely clear of
 *  the next column/row. */
typedef struct {
    uint8_t x;
    uint8_t y;
} hp41_dm41x_pixel_t;

/**
 * @brief Per-character 14-bit segment mask, for the full character set
 *        this display can render.
 *
 * hp41_dm41x_char_segments[c]: bit i set means segment i (this header's
 * own 'a'..'n' order above) is lit for ASCII character c. Populated for
 * every character Magellan's data/charset_41.py defines (letters,
 * digits, punctuation, Greek/math symbols) - unlike this table's earlier
 * digit-only revision, the classic-line view needs the full set, and
 * Stack view's rows are just more instances of the same table (see this
 * file's header comment).
 */
extern const uint16_t hp41_dm41x_char_segments[128];

/**
 * @brief All 16 segments'/marks' pixel offsets, flattened.
 *
 * Look up index i's (0-15, see HP41_DM41X_SEG_DOT/_SEP above) pixels via
 * hp41_dm41x_segment_pixels[hp41_dm41x_segment_pixel_offset[i] + k] for
 * k in [0, hp41_dm41x_segment_pixel_count[i]).
 */
extern const hp41_dm41x_pixel_t hp41_dm41x_segment_pixels[];
/** Per-segment start index into hp41_dm41x_segment_pixels[]. */
extern const uint16_t hp41_dm41x_segment_pixel_offset[HP41_DM41X_NUM_SEGMENTS];
/** Per-segment pixel count, same indexing as hp41_dm41x_segment_pixel_offset. */
extern const uint16_t hp41_dm41x_segment_pixel_count[HP41_DM41X_NUM_SEGMENTS];
