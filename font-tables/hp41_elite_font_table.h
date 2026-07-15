/**
 * @file hp41_elite_font_table.h
 * @brief Compile-time 3x5 pixel bitmap font for Elite User Mode.
 *
 * The array declared here is defined in the generated
 * hp41_elite_font_table.c (by gen_elite_font_table.py, from
 * hp41_elite_font_table.json - see that script and CLAUDE.md's "Elite
 * User Mode" section for what the data means and its known legibility
 * limits at this resolution). This header itself is hand-maintained,
 * not generated - update it alongside the script if the table shape
 * ever changes.
 */
#pragma once

#include <stdint.h>

#define HP41_ELITE_GLYPH_WIDTH_PX  3
#define HP41_ELITE_GLYPH_HEIGHT_PX 5

/**
 * @brief Per-code 3x5 pixel bitmap, indexed by HP-41 display code (0-127).
 *
 * hp41_elite_glyph_rows[code][r]: row r (0=top..4=bottom) of code's
 * glyph, packed 3 bits wide (bit 2 = leftmost pixel, bit 0 =
 * rightmost). A code with no defined glyph is all-zero (blank),
 * matching hp41_char_segments' convention for unpopulated codes.
 */
extern const uint8_t hp41_elite_glyph_rows[128][HP41_ELITE_GLYPH_HEIGHT_PX];
