#!/usr/bin/env python3
"""Convert an HP-41 .MOD (MOD1 format) plug-in module file into a C source file.

Parses the MOD1 container format - public domain, defined by Warren
Furlow for V41 (see MOD1_FILE_HEADER_SIZE etc. below for the byte-exact
layout this was verified against, cross-confirmed three independent
ways: byte-offset arithmetic against a real file, Furlow's own
published field sizes, and emu41gcc's own already-vendored
loadmodule() unpacking logic agreeing on the packed-word format) - and
emits one uint16_t[4096] array per ROM page, in the same big-endian-
unpacked convention rom_to_c.py's own NUT0/1/2 arrays use: ready to
point tabpage[] at, once you know which page number each array's own
declared Page field says it belongs at (printed as a trailing comment
summary, same place rom_to_c.py prints its own).

Only ROM pages (RAM==0) are converted; RAM pages are skipped with a
note on stderr, since this project has no MLDL-style writable-page
support.

Usage:
    python3 mod_to_c.py HPIL.MOD > rom_images_hpil.c
(then wire the resulting arrays into firmware/emu41gcc_compat/nut_rom.c
by hand, using the trailing Page/Bank summary as a guide)
"""
from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

MOD1_MAGIC = b"MOD1\x00"

# sizeof(ModuleFileHeader_t) per Warren Furlow's public-domain MOD1.H
# (FileFormat, Title, Version, PartNumber, Author, Copyright, License,
# Comments, six 1-byte fields, NumPages, and a 32-byte custom trailer).
MOD1_FILE_HEADER_SIZE = 729
MOD1_NUM_PAGES_OFFSET = 696  # byte offset of the NumPages field within the file header

# Per-page header size (Name, ID, and six 1-byte placement/type fields).
MOD1_PAGE_HEADER_SIZE = 36
MOD1_PAGE_CUSTOM_SIZE = 32  # trailing per-page custom field, after the image
PACKED_IMAGE_SIZE = 5120  # 4096 words packed 5 bytes/4 words (.BIN format)
MOD1_PAGE_STRUCT_SIZE = MOD1_PAGE_HEADER_SIZE + PACKED_IMAGE_SIZE + MOD1_PAGE_CUSTOM_SIZE

WORDS_PER_PAGE = 4096  # 4K words per HP-41 ROM page
MAX_ROM_WORD_VALUE = 0x3FF  # 10-bit words
EXPECTED_ARGC = 2  # argv[0] plus exactly one .MOD path


def check(condition: bool, message: str) -> None:
    """Power of 10 (Python adaptation), Rule 5 assertion helper.

    Unlike a bare `assert` statement, this is never compiled out under
    `-O`/`-OO` - see ../DEVIATIONS.md's implementation note on why this
    project uses `check()` instead of `assert` for anything Rule 5
    actually requires.
    """
    if not condition:
        raise AssertionError(message)


def read_cstring(data: bytes) -> str:
    """Decode a null-terminated/padded fixed-size field as ASCII text.

    Args:
        data: The fixed-size field's raw bytes.

    Returns:
        The text before the first null byte (or the whole field if
        there isn't one), decoded as ASCII with invalid bytes replaced.
    """
    return data.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def unpack_rom_words(packed: bytes) -> tuple[int, ...]:
    """Unpack a MOD1 page's 5120-byte packed image into 4096 10-bit words.

    5 bytes hold 4 words:
        Byte0 = Word0[7:0]
        Byte1 = Word1[5:0]<<2 | Word0[9:8]
        Byte2 = Word2[3:0]<<4 | Word1[9:6]
        Byte3 = Word3[1:0]<<6 | Word2[9:4]
        Byte4 = Word3[9:2]

    Args:
        packed: Exactly PACKED_IMAGE_SIZE (5120) raw bytes.

    Returns:
        Exactly WORDS_PER_PAGE (4096) 10-bit word values, in order.
    """
    check(len(packed) == PACKED_IMAGE_SIZE,
          f"expected {PACKED_IMAGE_SIZE} packed bytes, got {len(packed)}")
    words = []
    for i in range(0, PACKED_IMAGE_SIZE, 5):
        b0, b1, b2, b3, b4 = packed[i:i + 5]
        words.append(b0 | ((b1 & 0x03) << 8))
        words.append((b1 >> 2) | ((b2 & 0x0F) << 6))
        words.append((b2 >> 4) | ((b3 & 0x3F) << 4))
        words.append((b3 >> 6) | (b4 << 2))
    check(len(words) == WORDS_PER_PAGE, f"expected {WORDS_PER_PAGE} words, got {len(words)}")
    return tuple(words)


@dataclass
class ModPage:
    """One page's parsed header fields plus its unpacked ROM words.

    Attributes:
        name: The page's declared Name field (usually the original .ROM filename).
        rom_id: The page's declared ROM ID code (typically 2 letters + a revision digit/letter).
        page: Declared target page number (0-15), or a POSITION_* relative-placement
            code (>=0x1F) - see MOD1.H; this script doesn't resolve POSITION_* codes,
            it just reports the raw byte for the caller to interpret.
        bank: Declared target bank number (1-4).
        is_ram: Whether this page is RAM rather than ROM.
        words: The 4096 unpacked 10-bit words, or None if `is_ram`.
    """

    name: str
    rom_id: str
    page: int
    bank: int
    is_ram: bool
    words: tuple[int, ...] | None


def parse_mod_file(path: Path) -> list[ModPage]:
    """Parse a .MOD (MOD1 format) file into its constituent pages.

    Args:
        path: Path to the .MOD file.

    Returns:
        One ModPage per page the file declares, in file order.
    """
    with path.open("rb") as f:
        data = f.read()
    check(data[:5] == MOD1_MAGIC, f"{path}: not a MOD1 file (bad signature)")

    num_pages = data[MOD1_NUM_PAGES_OFFSET]
    pages = []
    for i in range(num_pages):
        page_off = MOD1_FILE_HEADER_SIZE + i * MOD1_PAGE_STRUCT_SIZE
        check(page_off + MOD1_PAGE_STRUCT_SIZE <= len(data),
              f"{path}: truncated (page {i} extends past end of file)")
        name = read_cstring(data[page_off:page_off + 20])
        rom_id = read_cstring(data[page_off + 20:page_off + 29])
        page_num = data[page_off + 29]
        bank = data[page_off + 31]
        is_ram = data[page_off + 33] != 0
        image_off = page_off + MOD1_PAGE_HEADER_SIZE
        words = None
        if not is_ram:
            words = unpack_rom_words(data[image_off:image_off + PACKED_IMAGE_SIZE])
        pages.append(ModPage(name, rom_id, page_num, bank, is_ram, words))
    return pages


def c_name(path: Path, index: int) -> str:
    """Derive a generated C array name for one page (e.g. HPIL.MOD page 0 -> rom_hpil_p0).

    Args:
        path: Path to the source .MOD file.
        index: The page's index within the file (0-based).

    Returns:
        A valid C identifier for this page's array.
    """
    return "rom_" + path.stem.lower().replace("-", "_") + f"_p{index}"


def print_page_array(words: tuple[int, ...], name: str) -> None:
    """Print one ROM page's words as a C array definition, plus any warning.

    Args:
        words: Exactly WORDS_PER_PAGE (4096) 10-bit word values.
        name: The C array name to print it under.
    """
    check(len(words) == WORDS_PER_PAGE, f"expected {WORDS_PER_PAGE} words, got {len(words)}")
    bad = [w for w in words if w > MAX_ROM_WORD_VALUE]
    if bad:
        print(f"// WARNING: {name} has {len(bad)} words > 0x3FF — "
              f"format assumption may not hold for this page", file=sys.stderr)
    print(f"const uint16_t {name}[{WORDS_PER_PAGE}] = {{")
    for j in range(0, WORDS_PER_PAGE, 16):
        chunk = words[j:j + 16]
        print("  " + ", ".join(f"0x{w:03X}" for w in chunk) + ",")
    print("};")
    print()


def main() -> None:
    """Convert every ROM page in the given .MOD file to a C array, printed to stdout."""
    if len(sys.argv) != EXPECTED_ARGC:
        print(__doc__)
        sys.exit(1)
    path = Path(sys.argv[1])
    pages = parse_mod_file(path)

    print(f"// Auto-generated by mod_to_c.py from {path.name} — do not hand-edit.")
    print("#include <stdint.h>")
    print()
    summary = []
    for i, page in enumerate(pages):
        if page.is_ram:
            print(f"// NOTE: page {i} ({page.name!r}, ID {page.rom_id!r}) is RAM — "
                  f"skipped, not converted (no writable-page support).", file=sys.stderr)
            continue
        check(page.words is not None, f"page {i} is not RAM but has no words")
        if page.words is None:
            continue  # unreachable given the check() above; narrows page.words for mypy below
        name = c_name(path, i)
        summary.append((page, name))
        print_page_array(page.words, name)

    print("// Summary (for your reference, not compiled):")
    for page, name in summary:
        print(f"//   {page.name!r} (ID {page.rom_id!r}) -> {name}[{WORDS_PER_PAGE}], "
              f"declared Page={page.page:#x} Bank={page.bank}")


if __name__ == "__main__":
    main()
