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
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_TABLE_PATH = os.path.join(SCRIPT_DIR, "hp41_font_table.json")
PIXEL_MAP_PATH = os.path.join(SCRIPT_DIR, "hp41_pixel_segment_map.json")
ANNUNCIATOR_MAP_PATH = os.path.join(SCRIPT_DIR, "hp41_annunciator_pixel_map.json")

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


def compute_tables():
    """Parse the 3 JSON sources and return every derived table both
    emitters need, so the plain-C and AVR/PROGMEM outputs are guaranteed
    to be built from identical data - only how it's *printed* differs.
    """
    with open(FONT_TABLE_PATH) as f:
        font_table = json.load(f)
    with open(PIXEL_MAP_PATH) as f:
        pixel_map = json.load(f)
    with open(ANNUNCIATOR_MAP_PATH) as f:
        annunciator_map = json.load(f)

    assert pixel_map["segment_bit_order_ref"] == SEGMENT_BIT_ORDER, \
        "hp41_pixel_segment_map.json's segment order no longer matches " \
        "this script - update SEGMENT_BIT_ORDER"
    segments = pixel_map["segments"]
    assert set(segments.keys()) == set(ALL_SEGMENTS), \
        f"unexpected segment set in pixel map: {sorted(segments.keys())}"

    # Codes 0-31 are flagged unreliable (garbage extraction) in
    # hp41_font_table.json / CLAUDE.md. But the "all segments on" bit
    # string ("11111111111111") turns out to be the extraction tool's
    # failure sentinel, not a real character - and it also shows up
    # scattered through the nominally-validated 32-127 range (all of
    # 102-125, i.e. most of lowercase f-z: never actually extracted).
    # Detect the sentinel by value rather than trusting a hardcoded
    # "0-31 is bad, 32-127 is fine" range, and blank every code that
    # carries it so we render nothing rather than a garbled full block.
    FAILURE_SENTINEL = "1" * 14
    char_segments = [0] * 128
    blanked = []
    for code in range(128):
        bits = font_table[str(code)]
        assert len(bits) == 14, f"code {code}: expected 14 bits, got {bits!r}"
        if code < 32 or bits == FAILURE_SENTINEL:
            blanked.append(code)
            continue
        mask = 0
        for i, b in enumerate(bits):
            if b == "1":
                mask |= (1 << i)
        char_segments[code] = mask

    # Flatten all segment pixels into one array, with per-segment
    # {offset, count} into it, indexed in ALL_SEGMENTS order (0-13 =
    # SEGMENT_BIT_ORDER, 14-16 = dot_top/dot_bottom/comma_tail).
    flat = []
    offsets = []
    counts = []
    for name in ALL_SEGMENTS:
        pts = segments[name]
        offsets.append(len(flat))
        counts.append(len(pts))
        flat.extend(pts)

    # Annunciators: absolute (not per-cell) pixels, one static label per bit.
    ann_data = annunciator_map["annunciators"]
    assert set(ann_data.keys()) == set(ANNUNCIATOR_ORDER), \
        f"unexpected annunciator set: {sorted(ann_data.keys())}"

    ann_bits = [ann_data[name]["bit"] for name in ANNUNCIATOR_ORDER]
    ann_flat = []
    ann_offsets = []
    ann_counts = []
    for name in ANNUNCIATOR_ORDER:
        pts = ann_data[name]["pixels"]
        ann_offsets.append(len(ann_flat))
        ann_counts.append(len(pts))
        ann_flat.extend(pts)

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


def emit_c(d):
    """Plain C source (no PROGMEM) for firmware/hp41_display_bridge.c on
    the Pico - unchanged output from before this script was refactored
    to share compute_tables() across both emitters.
    """
    print(f"// NOTE: {len(d['blanked'])} codes rendered blank (0-31, plus any "
          f"32-127 code carrying the all-1s extraction-failure sentinel):",
          file=sys.stderr)
    print(f"//   {d['blanked']}", file=sys.stderr)

    print("// Auto-generated by gen_display_tables.py from "
          "hp41_font_table.json and hp41_pixel_segment_map.json.")
    print("// Do not hand-edit - re-run the script instead.")
    print('#include "hp41_display_tables.h"')
    print()

    print("// Bit i set -> segment SEGMENT_BIT_ORDER[i] (see .h) is lit.")
    print("// Codes 0-31, plus any code that carried the source table's")
    print("// all-1s extraction-failure sentinel (see gen_display_tables.py),")
    print("// are zeroed (blank) rather than rendered as a garbled block.")
    print(f"const uint16_t hp41_char_segments[128] = {{")
    for i in range(0, 128, 8):
        row = d["char_segments"][i:i + 8]
        print("  " + ", ".join(f"0x{m:04X}" for m in row) + ",")
    print("};")
    print()

    flat, offsets, counts = d["flat"], d["offsets"], d["counts"]
    print(f"// {len(flat)} total (x,y) pixel offsets across all "
          f"{len(ALL_SEGMENTS)} segments, local to a "
          f'{d["cell_width_px"]}px-wide character cell '
          f'(y absolute 0-{d["cell_height_px"] - 1}).')
    print(f"const hp41_pixel_t hp41_segment_pixels[{len(flat)}] = {{")
    for i in range(0, len(flat), 8):
        row = flat[i:i + 8]
        print("  " + ", ".join(f"{{{x},{y}}}" for x, y in row) + ",")
    print("};")
    print()

    print("// Index with segment_index (0-13 SEGMENT_BIT_ORDER, "
          "14=dot_top, 15=dot_bottom, 16=comma_tail).")
    print(f"const uint8_t hp41_segment_pixel_offset[{len(ALL_SEGMENTS)}] = {{")
    print("  " + ", ".join(str(o) for o in offsets) + ",")
    print("};")
    print(f"const uint8_t hp41_segment_pixel_count[{len(ALL_SEGMENTS)}] = {{")
    print("  " + ", ".join(str(c) for c in counts) + ",")
    print("};")

    print()
    print("// Summary (for reference, not compiled):")
    for i, name in enumerate(ALL_SEGMENTS):
        print(f"//   [{i:2d}] {name:18s} {counts[i]} px @ offset {offsets[i]}")

    ann_bits = d["ann_bits"]
    ann_flat, ann_offsets, ann_counts = d["ann_flat"], d["ann_offsets"], d["ann_counts"]

    print()
    print(f"const uint16_t hp41_annunciator_bits[{len(ANNUNCIATOR_ORDER)}] = {{")
    print("  " + ", ".join(f"0x{b:03X}" for b in ann_bits) + ",")
    print("};")
    print()

    print(f"// {len(ann_flat)} total absolute (x,y) pixels across all "
          f"{len(ANNUNCIATOR_ORDER)} annunciators.")
    print(f"const hp41_pixel_t hp41_annunciator_pixels[{len(ann_flat)}] = {{")
    for i in range(0, len(ann_flat), 8):
        row = ann_flat[i:i + 8]
        print("  " + ", ".join(f"{{{x},{y}}}" for x, y in row) + ",")
    print("};")
    print()

    print("// Index with ANNUNCIATOR_ORDER (BAT, USER, G, RAD, SHIFT, "
          "0, 1, 2, 3, 4, PRGM, ALPHA).")
    print(f"const uint8_t hp41_annunciator_pixel_offset[{len(ANNUNCIATOR_ORDER)}] = {{")
    print("  " + ", ".join(str(o) for o in ann_offsets) + ",")
    print("};")
    print(f"const uint8_t hp41_annunciator_pixel_count[{len(ANNUNCIATOR_ORDER)}] = {{")
    print("  " + ", ".join(str(c) for c in ann_counts) + ",")
    print("};")

    print()
    print("// Summary (for reference, not compiled):")
    for i, name in enumerate(ANNUNCIATOR_ORDER):
        print(f"//   [{i:2d}] {name:6s} bit=0x{ann_bits[i]:03X} "
              f"{ann_counts[i]} px @ offset {ann_offsets[i]}")


def emit_avr_header(d):
    """Self-contained PROGMEM header for the Arduino display bridge -
    same data as emit_c(), but as a single .h (matching that sketch's
    existing bitmaps.h convention: one PROGMEM header, no separate .c),
    with avr/pgmspace.h's PROGMEM attribute on every array so the ~1.1KB
    of table data lives in flash only, not copied into the Uno's scarce
    2KB SRAM (the default behavior for `const` arrays on classic AVR
    without PROGMEM - a well-known Arduino gotcha, see
    NHD14432_DisplayBridge/CLAUDE.md).
    """
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

    print("// Bit i set -> segment SEGMENT_BIT_ORDER[i] (see gen_display_tables.py) is lit.")
    print("const uint16_t hp41_char_segments[128] PROGMEM = {")
    for i in range(0, 128, 8):
        row = d["char_segments"][i:i + 8]
        print("  " + ", ".join(f"0x{m:04X}" for m in row) + ",")
    print("};")
    print()

    flat, offsets, counts = d["flat"], d["offsets"], d["counts"]
    print(f"// {len(flat)} total (x,y) pixel offsets across all "
          f"{len(ALL_SEGMENTS)} segments, local to a "
          f'{d["cell_width_px"]}px-wide character cell.')
    print(f"const hp41_pixel_t hp41_segment_pixels[{len(flat)}] PROGMEM = {{")
    for i in range(0, len(flat), 8):
        row = flat[i:i + 8]
        print("  " + ", ".join(f"{{{x},{y}}}" for x, y in row) + ",")
    print("};")
    print()

    print("// Index with segment_index (0-13 SEGMENT_BIT_ORDER, "
          "14=dot_top, 15=dot_bottom, 16=comma_tail).")
    print(f"const uint8_t hp41_segment_pixel_offset[{len(ALL_SEGMENTS)}] PROGMEM = {{")
    print("  " + ", ".join(str(o) for o in offsets) + ",")
    print("};")
    print(f"const uint8_t hp41_segment_pixel_count[{len(ALL_SEGMENTS)}] PROGMEM = {{")
    print("  " + ", ".join(str(c) for c in counts) + ",")
    print("};")
    print()

    ann_bits = d["ann_bits"]
    ann_flat, ann_offsets, ann_counts = d["ann_flat"], d["ann_offsets"], d["ann_counts"]

    print("#define HP41_NUM_ANNUNCIATORS 12  /* BAT, USER, G, RAD, SHIFT, 0-4, PRGM, ALPHA */")
    print()
    print(f"const uint16_t hp41_annunciator_bits[{len(ANNUNCIATOR_ORDER)}] PROGMEM = {{")
    print("  " + ", ".join(f"0x{b:03X}" for b in ann_bits) + ",")
    print("};")
    print()

    print(f"// {len(ann_flat)} total absolute (x,y) pixels across all "
          f"{len(ANNUNCIATOR_ORDER)} annunciators.")
    print(f"const hp41_pixel_t hp41_annunciator_pixels[{len(ann_flat)}] PROGMEM = {{")
    for i in range(0, len(ann_flat), 8):
        row = ann_flat[i:i + 8]
        print("  " + ", ".join(f"{{{x},{y}}}" for x, y in row) + ",")
    print("};")
    print()

    print("// Index with ANNUNCIATOR_ORDER (BAT, USER, G, RAD, SHIFT, "
          "0, 1, 2, 3, 4, PRGM, ALPHA).")
    print(f"const uint8_t hp41_annunciator_pixel_offset[{len(ANNUNCIATOR_ORDER)}] PROGMEM = {{")
    print("  " + ", ".join(str(o) for o in ann_offsets) + ",")
    print("};")
    print(f"const uint8_t hp41_annunciator_pixel_count[{len(ANNUNCIATOR_ORDER)}] PROGMEM = {{")
    print("  " + ", ".join(str(c) for c in ann_counts) + ",")
    print("};")


def main():
    d = compute_tables()
    if "--avr" in sys.argv:
        emit_avr_header(d)
    else:
        emit_c(d)


if __name__ == "__main__":
    main()
