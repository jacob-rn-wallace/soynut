#!/usr/bin/env python3
"""
Run this on your Mac against NUT0.ROM (or any of the *.ROM files) to check
whether they're really "4096 words, 16-bit slots, only low 10 bits used."

Usage:
    python3 check_rom_format.py /Applications/my41cx.app/Contents/Resources/NUT0.ROM
"""
import struct
import sys
from pathlib import Path

MAX_ROM_WORD_VALUE = 0x3FF  # 10-bit words, if the format assumption holds
EXPECTED_ARGC = 2  # argv[0] plus exactly one .ROM path
PREVIEW_WORD_COUNT = 16


def analyze(path: Path, endian_fmt: str, label: str) -> int:
    with path.open("rb") as f:
        data = f.read()
    n_words = len(data) // 2
    words = struct.unpack(f"{endian_fmt}{n_words}H", data[:n_words * 2])
    over_10bit = [w for w in words if w > MAX_ROM_WORD_VALUE]
    print(f"--- {label} ---")
    print(f"  word count: {n_words}")
    print(f"  words with bits above 10 set: {len(over_10bit)} / {n_words}")
    print(f"  first {PREVIEW_WORD_COUNT} words: {[hex(w) for w in words[:PREVIEW_WORD_COUNT]]}")
    print()
    return len(over_10bit)


def main() -> None:
    if len(sys.argv) != EXPECTED_ARGC:
        print(__doc__)
        sys.exit(1)
    path = Path(sys.argv[1])
    bad_le = analyze(path, "<", "little-endian uint16")
    bad_be = analyze(path, ">", "big-endian uint16")

    print("=== Verdict ===")
    if bad_le == 0 and bad_be > 0:
        print("Little-endian, unpacked 10-bit-in-16-bit words. Use this.")
    elif bad_be == 0 and bad_le > 0:
        print("Big-endian, unpacked 10-bit-in-16-bit words. Use this.")
    elif bad_le == 0 and bad_be == 0:
        print("Both orderings look clean (can happen if many words are")
        print("small/symmetric) — inconclusive from this file alone, try")
        print("another ROM file or send me both first-16-words listings.")
    else:
        print("Neither ordering is clean — this probably ISN'T simple")
        print("unpacked 16-bit words. Send me this output and I'll rethink it.")


if __name__ == "__main__":
    main()
