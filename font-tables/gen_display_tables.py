#!/usr/bin/env python3
"""
Convert hp41_font_table.json (char code -> 14-segment on/off pattern),
hp41_pixel_segment_map.json (segment name -> GDRAM pixel offsets within a
12px-wide character cell), and hp41_annunciator_pixel_map.json
(annunciator name -> absolute GDRAM pixels) into compile-time lookup
tables - either the plain C (.c source, no PROGMEM) used by
firmware/hp41_display_bridge.c on the Pico, or (with --avr) a
self-contained PROGMEM header for the Arduino display bridge's own
on-device segment decode (see "Arduino NHD14432/NHD14432_DisplayBridge/
hp41_display_tables_avr.h"'s own header comment for why that copy
exists). Both outputs are computed from the exact same parsed JSON, so
they can't drift from each other in content - only re-run this script
against both targets if the JSON source ever changes.

Usage (run from font-tables/, or anywhere - paths below are relative to
this script's directory):
    python3 gen_display_tables.py > hp41_display_tables.c
    python3 gen_display_tables.py --avr > hp41_display_tables_avr.h
"""
import json
import sys
from pathlib import Path
from typing import Any, TypedDict

JsonDict = dict[str, Any]
Point = list[int]  # [x, y], as stored in the source JSON


class DisplayTables(TypedDict):
    """compute_tables()'s return shape - both emitters below consume
    exactly this, computed once from the JSON sources.
    """

    cell_width_px: int
    cell_height_px: int
    char_segments: list[int]
    blanked: list[int]
    flat: list[Point]
    offsets: list[int]
    counts: list[int]
    ann_bits: list[int]
    ann_flat: list[Point]
    ann_offsets: list[int]
    ann_counts: list[int]

SCRIPT_DIR = Path(__file__).resolve().parent
FONT_TABLE_PATH = SCRIPT_DIR / "hp41_font_table.json"
PIXEL_MAP_PATH = SCRIPT_DIR / "hp41_pixel_segment_map.json"
ANNUNCIATOR_MAP_PATH = SCRIPT_DIR / "hp41_annunciator_pixel_map.json"

# Must match emu41gcc/display.c's ann_to_buf() bit order (MSB first).
ANNUNCIATOR_ORDER = [
    "BAT", "USER", "G", "RAD", "SHIFT",
    "0", "1", "2", "3", "4", "PRGM", "ALPHA",
]

# Must match hp41_font_table.json's per-character bit string order (also
# echoed as "segment_bit_order_ref" in hp41_pixel_segment_map.json).
SEGMENT_BIT_ORDER = [
    "top", "upper_left_vert", "upper_right_vert", "upper_left_diag",
    "upper_right_diag", "upper_center_vert", "mid_left", "mid_right",
    "lower_left_vert", "lower_right_vert", "lower_left_diag",
    "lower_right_diag", "lower_center_vert", "bottom",
]
PUNCT_SEGMENTS = ["dot_top", "dot_bottom", "comma_tail"]
ALL_SEGMENTS = SEGMENT_BIT_ORDER + PUNCT_SEGMENTS  # 17 total
SEGMENT_BIT_COUNT = len(SEGMENT_BIT_ORDER)  # 14

# Codes 0-31 are flagged unreliable (garbage extraction) in
# hp41_font_table.json / CLAUDE.md. But the "all segments on" bit string
# turns out to be the extraction tool's own failure sentinel, not a real
# character - see _compute_char_segments()'s use of it below.
FAILURE_SENTINEL = "1" * SEGMENT_BIT_COUNT
RELIABLE_CODE_START = 32  # codes below this are known-unreliable extractions


def check(condition: bool, message: str) -> None:
    """Power of 10 (Python adaptation), Rule 5 assertion helper.

    Unlike a bare `assert` statement, this is never compiled out under
    `-O`/`-OO` - see ../DEVIATIONS.md's implementation note on why this
    project uses `check()` instead of `assert` for anything Rule 5
    actually requires.
    """
    if not condition:
        raise AssertionError(message)


def _load_json_sources() -> tuple[JsonDict, JsonDict, JsonDict]:
    """Reads and parses the three JSON source files. Pure I/O + parse,
    no validation - that's compute_tables()'s job once all three are in
    hand and can be cross-checked against each other.
    """
    with FONT_TABLE_PATH.open() as f:
        font_table = json.load(f)
    with PIXEL_MAP_PATH.open() as f:
        pixel_map = json.load(f)
    with ANNUNCIATOR_MAP_PATH.open() as f:
        annunciator_map = json.load(f)
    return font_table, pixel_map, annunciator_map


def _compute_char_segments(font_table: JsonDict) -> tuple[list[int], list[int]]:
    """Returns (char_segments, blanked): char_segments[128] is each
    code's 14-bit segment-on/off mask; blanked lists which codes were
    zeroed out instead (0-31, plus any 32-127 code carrying the source
    table's all-1s extraction-failure sentinel - see CLAUDE.md's "Font /
    display segment tables" section for why that sentinel exists and why
    it's detected by value rather than a hardcoded range).
    """
    char_segments = [0] * 128
    blanked = []
    for code in range(128):
        bits = font_table[str(code)]
        check(len(bits) == SEGMENT_BIT_COUNT,
              f"code {code}: expected {SEGMENT_BIT_COUNT} bits, got {bits!r}")
        if code < RELIABLE_CODE_START or bits == FAILURE_SENTINEL:
            blanked.append(code)
            continue
        mask = 0
        for i, b in enumerate(bits):
            if b == "1":
                mask |= (1 << i)
        char_segments[code] = mask
    return char_segments, blanked


def _flatten_segments(pixel_map: JsonDict) -> tuple[list[Point], list[int], list[int]]:
    """Flattens every named segment's pixel list into one array, with
    per-segment {offset, count} into it, indexed in ALL_SEGMENTS order
    (0-13 = SEGMENT_BIT_ORDER, 14-16 = dot_top/dot_bottom/comma_tail).
    Returns (flat, offsets, counts).
    """
    check(
        pixel_map["segment_bit_order_ref"] == SEGMENT_BIT_ORDER,
        "hp41_pixel_segment_map.json's segment order no longer matches "
        "this script - update SEGMENT_BIT_ORDER",
    )
    segments = pixel_map["segments"]
    check(
        set(segments.keys()) == set(ALL_SEGMENTS),
        f"unexpected segment set in pixel map: {sorted(segments.keys())}",
    )

    flat: list[Point] = []
    offsets = []
    counts = []
    for name in ALL_SEGMENTS:
        pts = segments[name]
        offsets.append(len(flat))
        counts.append(len(pts))
        flat.extend(pts)
    return flat, offsets, counts


def _flatten_annunciators(
    annunciator_map: JsonDict,
) -> tuple[list[int], list[Point], list[int], list[int]]:
    """Absolute (not per-cell) pixels, one static label per bit. Returns
    (ann_bits, ann_flat, ann_offsets, ann_counts).
    """
    ann_data = annunciator_map["annunciators"]
    check(
        set(ann_data.keys()) == set(ANNUNCIATOR_ORDER),
        f"unexpected annunciator set: {sorted(ann_data.keys())}",
    )

    ann_bits = [ann_data[name]["bit"] for name in ANNUNCIATOR_ORDER]
    ann_flat: list[Point] = []
    ann_offsets = []
    ann_counts = []
    for name in ANNUNCIATOR_ORDER:
        pts = ann_data[name]["pixels"]
        ann_offsets.append(len(ann_flat))
        ann_counts.append(len(pts))
        ann_flat.extend(pts)
    return ann_bits, ann_flat, ann_offsets, ann_counts


def compute_tables() -> DisplayTables:
    """Parse the 3 JSON sources and return every derived table both
    emitters need, so the plain-C and AVR/PROGMEM outputs are guaranteed
    to be built from identical data - only how it's *printed* differs.
    """
    font_table, pixel_map, annunciator_map = _load_json_sources()
    char_segments, blanked = _compute_char_segments(font_table)
    flat, offsets, counts = _flatten_segments(pixel_map)
    ann_bits, ann_flat, ann_offsets, ann_counts = _flatten_annunciators(annunciator_map)

    return {
        "cell_width_px": pixel_map["cell_width_px"],
        "cell_height_px": pixel_map["cell_height_px"],
        "char_segments": char_segments,
        "blanked": blanked,
        "flat": flat,
        "offsets": offsets,
        "counts": counts,
        "ann_bits": ann_bits,
        "ann_flat": ann_flat,
        "ann_offsets": ann_offsets,
        "ann_counts": ann_counts,
    }


# --- Shared array-printing helpers (used by both emitters below) -----------
# The plain-C and AVR/PROGMEM outputs render the exact same derived data
# in near-identical shapes, differing only in the trailing PROGMEM
# attribute - factored out here rather than duplicated so the two
# emitters can't silently drift in formatting.

def _print_u16_array(name: str, size: int, values: list[int], *, progmem: bool) -> None:
    suffix = " PROGMEM" if progmem else ""
    print(f"const uint16_t {name}[{size}]{suffix} = {{")
    for i in range(0, len(values), 8):
        row = values[i:i + 8]
        print("  " + ", ".join(f"0x{v:04X}" for v in row) + ",")
    print("};")


def _print_annunciator_bits(name: str, size: int, values: list[int], *, progmem: bool) -> None:
    """Same shape as _print_u16_array(), but annunciator bit masks are
    only ever 3 hex digits wide (12-bit values) and there are few enough
    (12) to print on a single line - a separate function rather than
    extra parameters on _print_u16_array() to keep that one's signature
    small.
    """
    suffix = " PROGMEM" if progmem else ""
    print(f"const uint16_t {name}[{size}]{suffix} = {{")
    print("  " + ", ".join(f"0x{v:03X}" for v in values) + ",")
    print("};")


def _print_pixel_array(name: str, size: int, pts: list[Point], *, progmem: bool) -> None:
    suffix = " PROGMEM" if progmem else ""
    print(f"const hp41_pixel_t {name}[{size}]{suffix} = {{")
    for i in range(0, len(pts), 8):
        row = pts[i:i + 8]
        print("  " + ", ".join(f"{{{x},{y}}}" for x, y in row) + ",")
    print("};")


def _print_u8_array(name: str, size: int, values: list[int], *, progmem: bool) -> None:
    suffix = " PROGMEM" if progmem else ""
    print(f"const uint8_t {name}[{size}]{suffix} = {{")
    print("  " + ", ".join(str(v) for v in values) + ",")
    print("};")


# --- Plain-C emitter (firmware/hp41_display_bridge.c on the Pico) ----------

def _emit_char_segments_c(d: DisplayTables) -> None:
    print(f"// NOTE: {len(d['blanked'])} codes rendered blank (0-31, plus any "
          f"32-127 code carrying the all-1s extraction-failure sentinel):",
          file=sys.stderr)
    print(f"//   {d['blanked']}", file=sys.stderr)

    print("// Bit i set -> segment SEGMENT_BIT_ORDER[i] (see .h) is lit.")
    print("// Codes 0-31, plus any code that carried the source table's")
    print("// all-1s extraction-failure sentinel (see gen_display_tables.py),")
    print("// are zeroed (blank) rather than rendered as a garbled block.")
    _print_u16_array("hp41_char_segments", 128, d["char_segments"], progmem=False)
    print()


def _emit_segment_pixels_c(d: DisplayTables) -> None:
    flat, offsets, counts = d["flat"], d["offsets"], d["counts"]
    print(f"// {len(flat)} total (x,y) pixel offsets across all "
          f"{len(ALL_SEGMENTS)} segments, local to a "
          f'{d["cell_width_px"]}px-wide character cell '
          f'(y absolute 0-{d["cell_height_px"] - 1}).')
    _print_pixel_array("hp41_segment_pixels", len(flat), flat, progmem=False)
    print()

    print("// Index with segment_index (0-13 SEGMENT_BIT_ORDER, "
          "14=dot_top, 15=dot_bottom, 16=comma_tail).")
    _print_u8_array("hp41_segment_pixel_offset", len(ALL_SEGMENTS), offsets, progmem=False)
    _print_u8_array("hp41_segment_pixel_count", len(ALL_SEGMENTS), counts, progmem=False)

    print()
    print("// Summary (for reference, not compiled):")
    for i, name in enumerate(ALL_SEGMENTS):
        print(f"//   [{i:2d}] {name:18s} {counts[i]} px @ offset {offsets[i]}")


def _emit_annunciators_c(d: DisplayTables) -> None:
    ann_bits = d["ann_bits"]
    ann_flat, ann_offsets, ann_counts = d["ann_flat"], d["ann_offsets"], d["ann_counts"]
    n_ann = len(ANNUNCIATOR_ORDER)

    print()
    _print_annunciator_bits("hp41_annunciator_bits", n_ann, ann_bits, progmem=False)
    print()

    print(f"// {len(ann_flat)} total absolute (x,y) pixels across all {n_ann} annunciators.")
    _print_pixel_array("hp41_annunciator_pixels", len(ann_flat), ann_flat, progmem=False)
    print()

    print("// Index with ANNUNCIATOR_ORDER (BAT, USER, G, RAD, SHIFT, "
          "0, 1, 2, 3, 4, PRGM, ALPHA).")
    _print_u8_array("hp41_annunciator_pixel_offset", n_ann, ann_offsets, progmem=False)
    _print_u8_array("hp41_annunciator_pixel_count", n_ann, ann_counts, progmem=False)

    print()
    print("// Summary (for reference, not compiled):")
    for i, name in enumerate(ANNUNCIATOR_ORDER):
        print(f"//   [{i:2d}] {name:6s} bit=0x{ann_bits[i]:03X} "
              f"{ann_counts[i]} px @ offset {ann_offsets[i]}")


def emit_c(d: DisplayTables) -> None:
    """Plain C source (no PROGMEM) for firmware/hp41_display_bridge.c on
    the Pico - unchanged output from before this script was refactored
    to share compute_tables() and the array-printing helpers above
    across both emitters.
    """
    print("// Auto-generated by gen_display_tables.py from "
          "hp41_font_table.json and hp41_pixel_segment_map.json.")
    print("// Do not hand-edit - re-run the script instead.")
    print('#include "hp41_display_tables.h"')
    print()

    _emit_char_segments_c(d)
    _emit_segment_pixels_c(d)
    _emit_annunciators_c(d)


# --- AVR/PROGMEM emitter (Arduino display bridge) --------------------------

def _emit_header_avr(d: DisplayTables) -> None:
    print("// Auto-generated by font-tables/gen_display_tables.py --avr from")
    print("// hp41_font_table.json and hp41_pixel_segment_map.json.")
    print("// Do not hand-edit - re-run the script instead. This is the AVR/")
    print("// PROGMEM twin of firmware/hp41_display_tables.h/.c (the Pico's")
    print("// copy) - same source JSON, same data, different C rendering.")
    print("// See NHD14432_DisplayBridge/CLAUDE.md for why this copy exists.")
    print("#pragma once")
    print("#include <stdint.h>")
    print("#include <avr/pgmspace.h>")
    print()
    print(f"#define HP41_CELL_WIDTH_PX  {d['cell_width_px']}")
    print("#define HP41_NUM_CELLS      12")
    print("#define HP41_NUM_SEGMENTS   17  /* 14 character segments + 3 punctuation */")
    print()
    print("#define HP41_SEG_DOT_TOP     14")
    print("#define HP41_SEG_DOT_BOTTOM  15")
    print("#define HP41_SEG_COMMA_TAIL  16")
    print()
    print("typedef struct {")
    print("    uint8_t x; /* 0..HP41_CELL_WIDTH_PX-1, local to a character cell */")
    print("    uint8_t y; /* absolute row, 0..31 */")
    print("} hp41_pixel_t;")
    print()


def _emit_char_segments_avr(d: DisplayTables) -> None:
    print("// Bit i set -> segment SEGMENT_BIT_ORDER[i] (see gen_display_tables.py) is lit.")
    _print_u16_array("hp41_char_segments", 128, d["char_segments"], progmem=True)
    print()


def _emit_segment_pixels_avr(d: DisplayTables) -> None:
    flat, offsets, counts = d["flat"], d["offsets"], d["counts"]
    print(f"// {len(flat)} total (x,y) pixel offsets across all "
          f"{len(ALL_SEGMENTS)} segments, local to a "
          f'{d["cell_width_px"]}px-wide character cell.')
    _print_pixel_array("hp41_segment_pixels", len(flat), flat, progmem=True)
    print()

    print("// Index with segment_index (0-13 SEGMENT_BIT_ORDER, "
          "14=dot_top, 15=dot_bottom, 16=comma_tail).")
    _print_u8_array("hp41_segment_pixel_offset", len(ALL_SEGMENTS), offsets, progmem=True)
    _print_u8_array("hp41_segment_pixel_count", len(ALL_SEGMENTS), counts, progmem=True)
    print()


def _emit_annunciators_avr(d: DisplayTables) -> None:
    ann_bits = d["ann_bits"]
    ann_flat, ann_offsets, ann_counts = d["ann_flat"], d["ann_offsets"], d["ann_counts"]
    n_ann = len(ANNUNCIATOR_ORDER)

    print("#define HP41_NUM_ANNUNCIATORS 12  /* BAT, USER, G, RAD, SHIFT, 0-4, PRGM, ALPHA */")
    print()
    _print_annunciator_bits("hp41_annunciator_bits", n_ann, ann_bits, progmem=True)
    print()

    print(f"// {len(ann_flat)} total absolute (x,y) pixels across all {n_ann} annunciators.")
    _print_pixel_array("hp41_annunciator_pixels", len(ann_flat), ann_flat, progmem=True)
    print()

    print("// Index with ANNUNCIATOR_ORDER (BAT, USER, G, RAD, SHIFT, "
          "0, 1, 2, 3, 4, PRGM, ALPHA).")
    _print_u8_array("hp41_annunciator_pixel_offset", n_ann, ann_offsets, progmem=True)
    _print_u8_array("hp41_annunciator_pixel_count", n_ann, ann_counts, progmem=True)


def emit_avr_header(d: DisplayTables) -> None:
    """Self-contained PROGMEM header for the Arduino display bridge -
    same data as emit_c(), but as a single .h (matching that sketch's
    existing bitmaps.h convention: one PROGMEM header, no separate .c),
    with avr/pgmspace.h's PROGMEM attribute on every array so the ~1.1KB
    of table data lives in flash only, not copied into the Uno's scarce
    2KB SRAM (the default behavior for `const` arrays on classic AVR
    without PROGMEM - a well-known Arduino gotcha, see
    NHD14432_DisplayBridge/CLAUDE.md).
    """
    _emit_header_avr(d)
    _emit_char_segments_avr(d)
    _emit_segment_pixels_avr(d)
    _emit_annunciators_avr(d)


def main() -> None:
    d = compute_tables()
    if "--avr" in sys.argv:
        emit_avr_header(d)
    else:
        emit_c(d)


if __name__ == "__main__":
    main()
