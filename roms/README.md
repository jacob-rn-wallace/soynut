# ROM files — bring your own

This directory holds the HP-41 ROM images the emulator boots, plus the
tools to prepare them. **The `.ROM`/`.rom` files themselves, and the
generated `rom_images.c`, are gitignored and not part of this repo** —
they're HP's copyrighted calculator firmware, not open source code, and
this project has no rights to redistribute them. You need to supply
your own, legally obtained (e.g. dumped from a physical calculator you
own, or extracted from emulator software you're licensed to use).

## What's required to build

Only three files are wired into the firmware build:

- `NUT0.ROM`, `NUT1.ROM`, `NUT2.ROM` — the base HP-41 OS (3 pages).

Place them in this directory, then generate `rom_images.c`:

```
cd roms
python3 rom_to_c.py NUT0.ROM NUT1.ROM NUT2.ROM > rom_images.c
```

`firmware/emu41gcc_compat/nut_rom.c` expects exactly the array names
this produces (`rom_nut0`/`rom_nut1`/`rom_nut2`, 4096 words each) — don't
rename the source files or you'll need to adjust the command
accordingly.

## Confirmed file format

Each file must be 8192 bytes = 4096 words, **big-endian `uint16_t`**,
values `0x000`-`0x3FF` (10-bit words in 16-bit slots, unpacked — no
bit-packing). `rom_to_c.py` warns on stderr if a file doesn't fit this
shape. `check_rom_format.py` is a standalone sanity-checker if you want
to verify a file (or its byte order) before converting it:

```
python3 check_rom_format.py /path/to/NUT0.ROM
```

## Optional expansion ROMs

Not currently wired into the build (no plug-in-module support yet), but
`rom_to_c.py` and `nut_rom.c` can be extended to load them later if you
have your own copies: `XNUT0-2.ROM`, `CXFUNS0-1.ROM`, `ADV0-2.ROM`,
`TIMER.ROM`, `PRINTER.ROM`, `CrdRdr-1E.rom`. Same format rules apply.

## HP-IL module (optional, wired into `quad/` when present)

Unlike the plain `.ROM` files above, this one is a **`.MOD` file**
(MOD1 container format — a different, more elaborate wrapper: a text
header with title/author/license/page metadata, then one or more ROM
pages in their own sub-header + packed-word payload). `HPIL.MOD` (the
real HP82160A HP-IL Module, 2 pages) is the one currently supported;
place it in this directory (as `HPIL.MOD`) and convert it with the
dedicated converter — `rom_to_c.py` doesn't understand this container
format, `mod_to_c.py` does:

```
cd roms
python3 mod_to_c.py HPIL.MOD > rom_images_hpil.c
```

This prints a summary to stdout (also landing in the generated file
as trailing comments) telling you which page the file itself declares
for each array — for `HPIL.MOD` specifically: `rom_hpil_p0`
("ILPrinter-2E") → page 6, `rom_hpil_p1` ("ILModule-1H") → page 7.
`firmware/emu41gcc_compat/nut_rom_hpil.c` already expects exactly
those two array names at exactly those two pages — if you ever
regenerate from a *different* HP-IL module dump with different
declared pages, that file's own `tabpage[]`/`typmod[]` assignments
would need updating to match.

**Genuinely optional, unlike the base OS ROM above**: `quad/`'s own
`CMakeLists.txt` and `tools/Makefile` both check for
`roms/rom_images_hpil.c`'s existence at build time and wire it in
automatically when present, or build without it (with the HP-IL video
interface view simply staying blank) when absent — no manual
CMake/Makefile editing needed either way.

**MOD1 format**: public domain, defined by Warren Furlow for V41 (see
`mod_to_c.py`'s own header comment for the byte-exact field layout this
was verified against — independently cross-checked three ways: raw
byte-offset arithmetic against a real file, Furlow's own published
field sizes, and `emu41gcc`'s own already-vendored `loadmodule()`
unpacking logic agreeing on the packed-word format). Only ROM pages are
converted; a MOD file containing RAM pages would have those skipped
(noted on stderr) — this project has no MLDL-style writable-page
support.

**Confirmed reachable, not just correct in isolation**: with this
module wired in, a completely ordinary cold boot (no special key
sequence) drives real HP-IL chip-register activity — see
`tools/hpil_module_trace.c` and CLAUDE.md's "HP-IL video interface"
section for the full empirical confirmation (contrast with the base OS
*alone*, which `tools/hpil_opcode_trace.c` confirmed never touches
HP-IL at all).
