# CLAUDE.md — NHD14432 Arduino Proof-of-Concept

Context file for Claude Code. This documents the Arduino sketch that was
written earlier in this project, before the decision was made to move CPU
emulation onto a Pico 2. The Arduino code itself isn't part of the final
build (see HANDOFF.md for that), but it's the **validated reference
implementation** for driving this specific display — the Pico SDK port
should behave identically, and if display output is ever wrong, this is
the known-working baseline to diff against.

## What this code is

A bring-up test for the NHD-14432WG-BTFH-VT (144x32 graphic LCD, ST7920
controller) on an Arduino Uno, wired in **8-bit parallel mode**. It
initializes the panel, clears GDRAM, and draws one of three static
144x32 bitmap test images, selectable over USB serial (`1`/`2`/`3`).
This was built and **physically tested working** on real hardware — it's
not a guess, it's a confirmed-good reference.

## Files

- `NHD14432_POC.ino` — the sketch itself.
- `bitmaps.h` — three PROGMEM `uint8_t` arrays (`bmp_00000`,
  `bmp_memory_lost`, `bmp_all_segments`), each 576 bytes: 32 rows x 9
  16-bit words/row x 2 bytes/word, packing a 144x32 1-bit-per-pixel image.
- `convert_images.py` — generates `bitmaps.h` from PNG mockups. Black
  pixel → GDRAM bit 1 ("on"); this matches the FSTN transflective
  convention where lit segments appear dark. If a real render comes out
  inverted, flip `ON_IS_BLACK` in that script and regenerate, don't hand-edit
  `bitmaps.h`.

## Wiring (parallel interface)

| Display pin | Signal | Arduino pin |
|---|---|---|
| 1 | Vss | GND |
| 2 | Vdd | 5V |
| 3 | Vo | NC (fixed contrast) |
| 4 | RS | D2 |
| 5 | R/W | **GND, hardwired** |
| 6 | E | D3 |
| 7-14 | DB0-DB7 | D4-D11 |
| 15/16 | LED+/LED- | left disconnected (no backlight, matches the real HP-41C) |

R/W is tied directly to ground rather than driven by a pin: the code
never reads the busy flag, only writes, so R/W is permanently held low.
This means correctness depends entirely on the fixed delays below rather
than polling — if you ever see corrupted output, suspect timing before
suspecting logic.

D0/D1 are deliberately untouched so USB serial keeps working.

## How the driver works

Three layers, bottom to top:

1. **`writeByte(rs, value)`** — sets RS, drives all 8 data pins, then
   pulses E high-then-low. The ST7920 latches on the E falling edge (per
   its datasheet), so data must be stable before that transition — it is,
   because `digitalWrite()` calls are slow enough (microseconds) to
   dwarf the datasheet's nanosecond-scale setup/hold requirements. No
   extra delay needed there.
2. **`writeCommand()` / `writeData()`** — wrap `writeByte()` with a 72µs
   delay afterward, matching the ST7920 datasheet's basic-instruction
   execution time. `RS=0` for commands, `RS=1` for data — same
   convention the ST7920's serial mode uses too, just without the
   nibble-framing serial mode requires.
3. **`st7920Init()` / `gdramClear()` / `drawBitmap()`** — the actual
   panel setup and drawing logic. See "Things that are inferred, not
   sourced" below — this layer is where the real uncertainty lived.

### Init sequence

```
0x30  basic instruction set, 8-bit
0x0C  display on, cursor off, blink off
0x01  clear DDRAM
0x34  extended instruction set, graphic off
0x36  extended instruction set, graphic ON
```

### GDRAM addressing

```
writeCommand(0x80 | y);   // vertical address, 0-31
writeCommand(0x80);       // horizontal address, 0 = start of row
// then 9 words (18 bytes) of pixel data per row, MSB first
```

## Things that are inferred, not sourced — read this before trusting the code blindly

This display's actual datasheet (`NHD-14432WG-BTFH-VT.pdf`, in the
project) only documents the **parallel timing** and the raw instruction
bit-field tables. It does **not** document:

- **The init sequence above.** It's standard, extremely well-established
  ST7920-controller behavior (this chip ships in a huge number of 128x64
  modules), cross-referenced against community convention — not something
  this specific datasheet spells out step by step.
- **The GDRAM address mapping for a 144x32 panel specifically.** The
  standard ST7920 convention is built around 128x64 (two 64-row halves
  sharing vertical addresses 0-31, selected by a horizontal-address bank
  bit). This panel is exactly one "half" — 32 rows — so the working
  assumption is vertical address 0-31 maps directly to y=0-31, with 9
  horizontal words (144px / 16 bits) covering the full width, no
  bank-select trick needed. **This was empirically validated** by
  physically running this code — the test images displayed correctly, so
  treat this specific detail as confirmed-by-testing rather than merely
  theoretical.

Given it's already validated against real hardware, there's no reason to
re-derive the init sequence or addressing scheme — the value of writing
this down is so a future port (e.g. to the Pico) doesn't accidentally
diverge from something that's known to work.

## Relationship to the rest of the project

This Arduino code is now superseded by a Pico 2 port (raw Pico C SDK, not
Arduino framework) using the same three-layer driver logic with
`gpio_put()`/`gpio_init()` instead of `digitalWrite()`/`pinMode()`, and
different pin numbers (GP2-GP11) to avoid the Pico's native USB. The
backlight (pins 15/16) is left disconnected in both versions — deliberate,
matching the real HP-41C having no backlight, not an oversight.

See `HANDOFF.md` for the current state of the overall project (Nut CPU
emulation, ROM images, font table) — this file is scoped only to
explaining the display driver.
