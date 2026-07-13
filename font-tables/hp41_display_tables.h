/**
 * @file hp41_display_tables.h
 * @brief Compile-time HP-41 character segment / pixel-mapping tables.
 *
 * The arrays declared here are defined in the generated
 * hp41_display_tables.c (by gen_display_tables.py, from
 * hp41_font_table.json, hp41_pixel_segment_map.json, and
 * hp41_annunciator_pixel_map.json - see that script for how, and
 * CLAUDE.md's "Font / display segment table" section for what the data
 * means). This header itself is hand-maintained, not generated - update
 * it alongside the script if the table shapes ever change.
 */
#pragma once

#include <stdint.h>

#define HP41_CELL_WIDTH_PX  12
#define HP41_NUM_CELLS      12
#define HP41_NUM_SEGMENTS   17  /* 14 character segments + 3 punctuation */

/** @name Punctuation pseudo-segment indices
 *
 * Index of the 3 punctuation pseudo-segments within the 17-segment
 * tables below (0-13 are the 14 character segments, in the same bit
 * order as hp41_char_segments' bitmask - see gen_display_tables.py's
 * SEGMENT_BIT_ORDER).
 * @{
 */
#define HP41_SEG_DOT_TOP     14
#define HP41_SEG_DOT_BOTTOM  15
#define HP41_SEG_COMMA_TAIL  16
/** @} */

/** One pixel offset: (x, y) local to a character cell, or absolute for annunciators. */
typedef struct {
    uint8_t x; /* 0..HP41_CELL_WIDTH_PX-1, local to a character cell */
    uint8_t y; /* absolute row, 0..31 */
} hp41_pixel_t;

/**
 * @brief Per-code 14-bit segment-on/off mask.
 *
 * hp41_char_segments[code]: bit i set means segment i (character-segment
 * bit order) is lit for that HP-41 display character code. Only codes
 * 32-127 are populated - see CLAUDE.md, codes 0-31 are known-unreliable
 * in the source extraction and are zeroed (blank) here.
 */
extern const uint16_t hp41_char_segments[128];

/**
 * @brief All segments' pixel offsets, flattened.
 *
 * Look up a segment's pixels via
 * hp41_segment_pixels[hp41_segment_pixel_offset[seg] + k] for
 * k in [0, hp41_segment_pixel_count[seg]).
 */
extern const hp41_pixel_t hp41_segment_pixels[];
/** Per-segment start index into hp41_segment_pixels[], indexed 0-13 = SEGMENT_BIT_ORDER, 14-16 = punctuation. */
extern const uint8_t hp41_segment_pixel_offset[HP41_NUM_SEGMENTS];
/** Per-segment pixel count, same indexing as hp41_segment_pixel_offset. */
extern const uint8_t hp41_segment_pixel_count[HP41_NUM_SEGMENTS];

#define HP41_NUM_ANNUNCIATORS 12  /* BAT, USER, G, RAD, SHIFT, 0-4, PRGM, ALPHA */

/**
 * @brief Per-annunciator lcd_ann bitmask.
 *
 * hp41_annunciator_bits[i]: the lcd_ann bitmask (see
 * emu41gcc/display.c's ann_to_buf()) that lights annunciator i. Pixels
 * are absolute GDRAM coordinates (unlike the character segment table,
 * these are NOT per-cell - each annunciator is a single static label at
 * a fixed position), looked up the same way:
 * hp41_annunciator_pixels[hp41_annunciator_pixel_offset[i] + k] for
 * k in [0, hp41_annunciator_pixel_count[i]).
 */
extern const uint16_t hp41_annunciator_bits[HP41_NUM_ANNUNCIATORS];
/** All annunciators' absolute pixel offsets, flattened. */
extern const hp41_pixel_t hp41_annunciator_pixels[];
/** Per-annunciator start index into hp41_annunciator_pixels[], indexed by ANNUNCIATOR_ORDER. */
extern const uint8_t hp41_annunciator_pixel_offset[HP41_NUM_ANNUNCIATORS];
/** Per-annunciator pixel count, same indexing as hp41_annunciator_pixel_offset. */
extern const uint8_t hp41_annunciator_pixel_count[HP41_NUM_ANNUNCIATORS];
