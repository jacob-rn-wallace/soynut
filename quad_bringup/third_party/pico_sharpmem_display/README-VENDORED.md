# Vendored from pico_sharpmem_display

Exactly 4 files, copied unmodified from a local checkout of
`pico_sharpmem_display` (LGPL-2.1, see `LICENSE` in this directory) that
had no discoverable upstream git remote to submodule instead (unlike
`emu41gcc/` at the repo root, which *is* a proper submodule — see
`CLAUDE.md`'s "The Nut CPU core" section for why that's the preferred
approach whenever a real upstream URL exists):

- `src/sharpdisp.c` + `include/sharpdisp/sharpdisp.h` — Sharp Memory LCD
  SPI driver (software-toggled VCOM, no EXTCOMIN pin).
- `src/bitmap.c` + `include/sharpdisp/bitmap.h` — the packed 1bpp buffer
  type `sharpdisp_init()` depends on; a real, non-optional dependency,
  not an extra drawing-primitive layer (this project's own display code
  writes packed bytes directly and doesn't use the upstream library's
  higher-level `bitmapshapes`/`bitmaptext`/`bitmapconsole`/`doublebuffer`
  layers at all — skipped on purpose, see the Magellan project's plan
  file for the full reasoning).

**Treat this directory as a black box, same hard rule as `emu41gcc/`:
never edit any file inside it.** If something here needs to change,
that's a sign the fix belongs in `quad_bringup/` (or, later, `quad/`)
code that calls into it instead.
