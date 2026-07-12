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
