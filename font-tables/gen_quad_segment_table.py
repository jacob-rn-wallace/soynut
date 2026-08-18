#!/usr/bin/env python3
"""Rasterize Magellan's HP-41 segment geometry into the QUAD pixel table.

Sibling to gen_display_tables.py, but a from-scratch pipeline rather than
a mode of it: that script starts from JSON already keyed by named,
pre-decided pixel positions (hp41_pixel_segment_map.json, hand-derived
from a display-mockup image). This script instead starts from real
*vector* geometry - the Magellan project's data/segments.py (14-segment
"sunburst" polygons in a 1000x1350 reference space, italic slant baked
in) and data/charset_41.py (character -> lit-segment-letters, e.g.
"1" -> "bc") - and rasterizes each polygon at this table's own chosen
cell pixel size via a supersampled Pillow fill. Magellan
(/Users/jake/magellan/hp41-display by default; override with the
MAGELLAN_DIR environment variable if your checkout lives elsewhere) is
this project's own separate, personal-reference repo for exactly this
geometry - see its CLAUDE.md for the full provenance chain back to
Nonpareil's reference material.

The FULL character set from charset_41.py (letters, digits, punctuation,
Greek/math symbols) is pulled in - this table is shared by both of the
QUAD-style display's view modes: a "classic-line" view that reuses the
live lcd_a/b/c/lcd_ann buffer unchanged (the only place letters/symbols
appear - see hp41_display_bridge.c's existing decode) and a 4-row "Stack"
view (T/Z/Y/X) driven by a real FIX/SCI/ENG-aware formatter
(hp41_register_format.h) that emits the same kind of per-cell sequence -
so both views plot through this one table identically. See the Magellan
plan file's "QUAD-style Sharp Memory LCD backend" section for the full
picture; this table's earlier digit-only revision only served a
since-superseded numeric-only design.

Grid geometry is a true-to-original-size 12-column layout (the real
HP-41's own physical character-cell count), not Elite Mode's always-14-
fixed-column convention - see hp41_quad_font_table.h's header comment
for the real-world mm math this is derived from.

Usage (run from font-tables/, or anywhere - paths below are relative to
this script's directory):
    python3 gen_quad_segment_table.py > hp41_quad_font_table.c

Requires Pillow (already a soynut Python dependency - see
tools/hp41_keyboard_gui.py) for the polygon rasterization.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path
from typing import TypedDict

from PIL import Image, ImageDraw

SCRIPT_DIR = Path(__file__).resolve().parent

# Magellan lives in a sibling checkout by convention (e.g. /Users/jake/
# soynut and /Users/jake/magellan side by side) - override with the
# MAGELLAN_DIR env var if your layout differs.
_default_magellan_dir = SCRIPT_DIR / ".." / ".." / "magellan" / "hp41-display"
MAGELLAN_DIR = Path(os.environ.get("MAGELLAN_DIR", str(_default_magellan_dir))).resolve()
sys.path.insert(0, str(MAGELLAN_DIR))

try:
    # Magellan is a separate, personal-reference repo (see module
    # docstring) - mypy has no stubs/source for it to check against, and
    # shouldn't: these two type: ignore comments are the per-callsite
    # exception, not a project-wide mypy config change.
    from data.charset_41 import CHAR_SEGMENTS  # type: ignore[import-not-found]
    from data.segments import (  # type: ignore[import-not-found]
        CELL_HEIGHT,
        CELL_WIDTH,
        COLON,
        COMMA,
        DECIMAL_POINT,
        SEGMENT_ORDER,
        SEGMENTS,
    )
except ImportError:
    print(
        f"error: couldn't import Magellan's data/segments.py or data/charset_41.py "
        f"from {MAGELLAN_DIR} - set the MAGELLAN_DIR environment variable to your "
        f"magellan/hp41-display checkout",
        file=sys.stderr,
    )
    raise

Point = tuple[float, float]

# This table's own grid geometry - must match hp41_quad_font_table.h
# exactly (checked by check_geometry_matches_header() below); duplicated
# here rather than parsed from the header because this script's output
# doesn't otherwise need to touch that hand-maintained file at all.
CELL_WIDTH_PX = 25
CELL_HEIGHT_PX = 34
COL_PITCH_PX = 33
ROW_PITCH_PX = 46
NUM_COLS = 12
NUM_ROWS = 4
GRID_X0 = 6
GRID_Y0 = 34
DISP_WIDTH_PX = 400
DISP_HEIGHT_PX = 240

NUM_ASCII_CODES = 128  # hp41_quad_char_segments[128]'s size, matching hp41_display_tables.h's

# Characters this display ever plots: every plain-ASCII entry from
# Magellan's charset_41.py (see module docstring) - order here becomes
# the "populated codes" summary order, not the table's own index (that's
# always the character's own ASCII code).
#
# Deliberately excludes charset_41.py's Greek/math symbol entries (mu,
# ne, Sigma, angle, pi, alpha, beta, gamma, sigma, lambda, delta) even
# though they're real, correctly-shaped glyphs there: this table is
# indexed by whatever hp41_decode_ascii() (firmware/hp41_display_bridge.c)
# actually returns for the classic-line view to look up, and that
# function's own contract is "0-127" - it already approximates those
# symbols as plain ASCII letters (mu -> 'u', sigma -> 's', angle -> 'a',
# etc., see its switch statement), so entries keyed by the actual Unicode
# symbol would never be reached through the real decode path anyway, and
# ord() of one is nowhere near the 0-127 index range besides.
NEEDED_CHARS = "".join(sorted(ch for ch in CHAR_SEGMENTS if ord(ch) < NUM_ASCII_CODES))

# Segment index order: bits 0-13 of the character mask follow Magellan's
# own SEGMENT_ORDER directly (see hp41_quad_font_table.h for why this
# table doesn't reuse hp41_display_tables.h's different SEGMENT_BIT_ORDER
# convention). Indices 14-16 are the three punctuation pseudo-segments
# (period, comma/separator, colon) - the classic-line view needs all
# three to show anything the original single-line display could
# (hp41_display_bridge.c's own punct switch handles period/colon/comma).
SEG_INDEX_DOT = 14
SEG_INDEX_SEP = 15
SEG_INDEX_COLON = 16
NUM_SEGMENTS = 17

# Supersampling factor for the polygon-fill rasterizer below, and the
# per-pixel coverage fraction (of ss*ss subpixels) needed to call an
# output pixel "on".
SUPERSAMPLE = 4
COVERAGE_THRESHOLD = 0.5

# Rasterization canvas, in Magellan reference units - wider/taller than
# CELL_WIDTH x CELL_HEIGHT specifically to hold the two punctuation
# marks, which Magellan centers ON the cell boundary (x=CELL_WIDTH) by
# design (see segments.py) and which (for the comma mark) has a tail
# reaching below CELL_HEIGHT. Confirmed to comfortably clear the next
# column/row's own cell - see check_geometry_matches_header().
CANVAS_UNITS_W = CELL_WIDTH * 1.15
CANVAS_UNITS_H = CELL_HEIGHT * 1.15


class DisplayTable(TypedDict):
    """compute_table()'s return shape - what emit_c() prints.

    Attributes:
        char_segments: hp41_quad_char_segments[128] - 14-bit segment mask per code.
        populated: Sorted list of the codes that got a real mask.
        flat: Flattened (x, y) pixel offsets across all 17 segments/marks.
        offsets: Per-segment start index into flat, indices 0-16.
        counts: Per-segment pixel count, indices 0-16.
    """

    char_segments: list[int]
    populated: list[int]
    flat: list[Point]
    offsets: list[int]
    counts: list[int]


def check(condition: bool, message: str) -> None:
    """Power of 10 (Python adaptation), Rule 5 assertion helper.

    Never compiled out under `-O`/`-OO`, unlike a bare `assert` - see
    ../DEVIATIONS.md's implementation note (gen_display_tables.py uses
    the same helper for the same reason).

    Args:
        condition: Must be true, or this raises.
        message: Explains what invariant failed.
    """
    if not condition:
        raise AssertionError(message)


def check_geometry_matches_header() -> None:
    """Verify this script's constants agree with hp41_quad_font_table.h.

    That header is hand-maintained (not generated), so nothing enforces
    the two staying in sync except this check - run at import time so a
    drifted header/generator pair fails loudly instead of silently
    emitting a table for the wrong geometry.
    """
    header = (SCRIPT_DIR / "hp41_quad_font_table.h").read_text()
    expected = {
        "HP41_QUAD_DISP_WIDTH_PX": DISP_WIDTH_PX,
        "HP41_QUAD_DISP_HEIGHT_PX": DISP_HEIGHT_PX,
        "HP41_QUAD_NUM_COLS": NUM_COLS,
        "HP41_QUAD_NUM_ROWS": NUM_ROWS,
        "HP41_QUAD_CELL_WIDTH_PX": CELL_WIDTH_PX,
        "HP41_QUAD_CELL_HEIGHT_PX": CELL_HEIGHT_PX,
        "HP41_QUAD_COL_PITCH_PX": COL_PITCH_PX,
        "HP41_QUAD_ROW_PITCH_PX": ROW_PITCH_PX,
        "HP41_QUAD_GRID_X0": GRID_X0,
        "HP41_QUAD_GRID_Y0": GRID_Y0,
    }
    for name, value in expected.items():
        # Whitespace-tolerant: only the token pair actually matters, not
        # how many spaces separate #define/name/value in the header.
        match = re.search(rf"#define\s+{name}\s+(\S+)", header)
        check(match is not None and int(match.group(1)) == value,
              f"hp41_quad_font_table.h's {name} doesn't match this script's "
              f"value of {value} - update whichever one is stale")

    # Same fit-check hp41_quad_font_table.h documents as "verified at
    # generation time" - the last column's/row's own cell (not the next
    # pitch step past it) plus a matching margin on the far side must
    # exactly fill the panel (by this table's own deliberate choice of
    # round pixel constants), not just fit within it, or GRID_X0/GRID_Y0
    # above were computed wrong.
    check(2 * GRID_X0 + (NUM_COLS - 1) * COL_PITCH_PX + CELL_WIDTH_PX == DISP_WIDTH_PX,
          "grid width + symmetric margins doesn't exactly fill DISP_WIDTH_PX")
    check(2 * GRID_Y0 + (NUM_ROWS - 1) * ROW_PITCH_PX + CELL_HEIGHT_PX == DISP_HEIGHT_PX,
          "grid height + symmetric margins doesn't exactly fill DISP_HEIGHT_PX")


def _rasterize(polygons: list[list[Point]]) -> list[Point]:
    """Fill a list of Magellan-space polygons and return their "on" pixels.

    Supersamples by SUPERSAMPLE, box-downsamples (an exact coverage
    average per output pixel, not a resampling approximation), then
    thresholds at COVERAGE_THRESHOLD - a standard, simple way to turn
    vector fill coverage into a clean 1bpp decision per cell.

    Args:
        polygons: One or more polygons, each a list of (x, y) points in
            Magellan's CELL_WIDTH x CELL_HEIGHT reference space.

    Returns:
        (x, y) integer pixel offsets, local to this rasterizer's shared
        canvas (see CANVAS_UNITS_W/_H) - may exceed CELL_WIDTH_PX/
        CELL_HEIGHT_PX for the two punctuation marks, by design.
    """
    check(len(polygons) > 0, "rasterize() needs at least one polygon")
    canvas_px_w = round(CANVAS_UNITS_W * CELL_WIDTH_PX / CELL_WIDTH)
    canvas_px_h = round(CANVAS_UNITS_H * CELL_HEIGHT_PX / CELL_HEIGHT)
    hi_w, hi_h = canvas_px_w * SUPERSAMPLE, canvas_px_h * SUPERSAMPLE
    sx, sy = hi_w / CANVAS_UNITS_W, hi_h / CANVAS_UNITS_H

    img = Image.new("L", (hi_w, hi_h), 0)
    draw = ImageDraw.Draw(img)
    for poly in polygons:
        draw.polygon([(x * sx, y * sy) for x, y in poly], fill=255)
    small = img.resize((canvas_px_w, canvas_px_h), Image.Resampling.BOX)

    threshold_level = round(255 * COVERAGE_THRESHOLD)
    return [
        (px, py)
        for py in range(canvas_px_h)
        for px in range(canvas_px_w)
        if small.getpixel((px, py)) >= threshold_level
    ]


def _compute_char_segments() -> tuple[list[int], list[int]]:
    """Build the 128-entry character mask table for NEEDED_CHARS only.

    Returns:
        (char_segments, populated): char_segments[128], bit i set means
        SEGMENT_ORDER[i] is lit; populated is the sorted list of codes
        that got a real (possibly all-zero, for ' ') mask - every
        plain-ASCII entry in NEEDED_CHARS.
    """
    char_segments = [0] * NUM_ASCII_CODES
    populated = []
    for ch in NEEDED_CHARS:
        code = ord(ch)
        check(0 <= code < NUM_ASCII_CODES, f"{ch!r} (0x{code:02x}) out of ASCII range")
        segs = CHAR_SEGMENTS.get(ch, frozenset())
        if ch != " ":
            check(len(segs) > 0, f"Magellan's charset_41.py has no glyph for {ch!r}")
        mask = 0
        for i, seg_id in enumerate(SEGMENT_ORDER):
            if seg_id in segs:
                mask |= 1 << i
        char_segments[code] = mask
        populated.append(code)
    return char_segments, sorted(populated)


def _flatten_segments() -> tuple[list[Point], list[int], list[int]]:
    """Rasterize all 14 segments plus the 3 punctuation marks, flattened.

    Returns:
        (flat, offsets, counts): flat is every segment's/mark's pixels
        concatenated, in SEGMENT_ORDER then [DOT, SEP, COLON] order
        (indices 0-16, matching hp41_quad_font_table.h's
        HP41_QUAD_SEG_* / bit-order documentation).
    """
    flat: list[Point] = []
    offsets = []
    counts = []
    for seg_id in SEGMENT_ORDER:
        pts = _rasterize([SEGMENTS[seg_id]])
        offsets.append(len(flat))
        counts.append(len(pts))
        flat.extend(pts)
    for mark in (DECIMAL_POINT, COMMA, COLON):
        pts = _rasterize(mark)
        offsets.append(len(flat))
        counts.append(len(pts))
        flat.extend(pts)
    check(len(offsets) == NUM_SEGMENTS,
          f"expected {NUM_SEGMENTS} segments/marks, got {len(offsets)}")
    return flat, offsets, counts


def compute_table() -> DisplayTable:
    """Rasterize Magellan's geometry and build every table emit_c() needs.

    Returns:
        The complete computed table - see DisplayTable's own docstring.
    """
    check_geometry_matches_header()
    char_segments, populated = _compute_char_segments()
    flat, offsets, counts = _flatten_segments()
    return {
        "char_segments": char_segments,
        "populated": populated,
        "flat": flat,
        "offsets": offsets,
        "counts": counts,
    }


def emit_c(d: DisplayTable) -> None:
    """Print hp41_quad_font_table.c's full contents to stdout.

    Args:
        d: Computed by compute_table().
    """
    print("// Auto-generated by gen_quad_segment_table.py from the Magellan")
    print(f"// project's data/segments.py and data/charset_41.py ({MAGELLAN_DIR}).")
    print("// Do not hand-edit - re-run the script instead.")
    print('#include "hp41_quad_font_table.h"')
    print()

    print(f"// Populated codes (all others are zero/blank): {d['populated']}", file=sys.stderr)
    print("// Bit i set -> segment SEGMENT_ORDER[i] (see hp41_quad_font_table.h) is lit.")
    print("// Every plain-ASCII character Magellan's charset_41.py defines is")
    print("// populated (see that header's own comment for why Greek/math")
    print("// symbols aren't).")
    print(f"const uint16_t hp41_quad_char_segments[{NUM_ASCII_CODES}] = {{")
    for i in range(0, NUM_ASCII_CODES, 8):
        row = d["char_segments"][i:i + 8]
        print("  " + ", ".join(f"0x{v:04X}" for v in row) + ",")
    print("};")
    print()

    flat, offsets, counts = d["flat"], d["offsets"], d["counts"]
    print(f"// {len(flat)} total (x,y) pixel offsets across all {NUM_SEGMENTS} "
          f"segments/marks, local to a {CELL_WIDTH_PX}x{CELL_HEIGHT_PX}px cell")
    print("// (indices 14-16's marks legitimately extend past that box - see")
    print("// hp41_quad_font_table.h's hp41_quad_pixel_t doc comment).")
    print(f"const hp41_quad_pixel_t hp41_quad_segment_pixels[{len(flat)}] = {{")
    for i in range(0, len(flat), 8):
        pix_row = flat[i:i + 8]
        print("  " + ", ".join(f"{{{int(x)},{int(y)}}}" for x, y in pix_row) + ",")
    print("};")
    print()

    print("// Index with 0-13 = SEGMENT_ORDER, 14 = HP41_QUAD_SEG_DOT, "
          "15 = HP41_QUAD_SEG_SEP, 16 = HP41_QUAD_SEG_COLON.")
    print(f"const uint16_t hp41_quad_segment_pixel_offset[{NUM_SEGMENTS}] = {{")
    print("  " + ", ".join(str(v) for v in offsets) + ",")
    print("};")
    print(f"const uint16_t hp41_quad_segment_pixel_count[{NUM_SEGMENTS}] = {{")
    print("  " + ", ".join(str(v) for v in counts) + ",")
    print("};")

    print()
    print("// Summary (for reference, not compiled):")
    names = [*SEGMENT_ORDER, "DOT", "SEP", "COLON"]
    for i, name in enumerate(names):
        print(f"//   [{i:2d}] {name:4s} {counts[i]:3d} px @ offset {offsets[i]}")


def main() -> None:
    """Compute the table once, then print the generated C source to stdout."""
    d = compute_table()
    emit_c(d)


if __name__ == "__main__":
    main()
