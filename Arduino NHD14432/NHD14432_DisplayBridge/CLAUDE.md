# CLAUDE.md — NHD14432 Display Bridge (Arduino side)

Context file for Claude Code. This is a **copy** of `NHD14432_POC/`
(that original folder and its own `CLAUDE.md` are left completely
untouched — a snapshot of that earlier stage of the project), extended
to also drive the display with **live data from the Pico 2**, not just
its own built-in test bitmaps.

## Why this exists

The Pico's own attempts to drive this LCD directly — first 8-bit
parallel, then a rewrite to 3-wire serial mode — never produced any
visible output on real hardware, through a 4-channel auto-sensing
bidirectional level shifter (a RobotDyn "Logic Level Converter,
Bi-Direction"). The root cause wasn't fully isolated (candidates: the
shifter's rise time, the LCD's serial `/CS` polarity, wiring, or
something else in the serial protocol — see the main project's
`CLAUDE.md` "Hardware" section for the full debugging history).

Rather than keep debugging blind, this LCD + this exact parallel driver
were already hardware-validated working (see `NHD14432_POC/CLAUDE.md`),
so the Pico now runs the Nut CPU emulator only, and hands the actual
display-driving job off to this Arduino, over a simple independent
serial link. This is meant to be **temporary**, until a better
(actively-driven, more channels) level shifter arrives, at which point
the Pico can go back to driving the LCD directly — see the main
project's `firmware/pins.h` and `firmware/main.c` for how that direct
path is being kept intact and ready to resume.

## What changed vs. `NHD14432_POC`

Everything about the original proof-of-concept is unchanged: the parallel
wiring to the LCD (D2-D11), the `st7920Init()`/`gdramClear()`/
`drawBitmap()` driver logic, and the confirmed GDRAM addressing all work
exactly as documented in `NHD14432_POC/CLAUDE.md` — read that file first
for all of that; it isn't repeated here.

**The `1`/`2`/`3` built-in test-pattern switcher over USB Serial is
currently commented out** in `loop()` (not deleted — left in place as a
standalone display sanity check, independent of the Pico link) — with
live Pico frames now the real display source, a stray `1`/`2`/`3`
keystroke in the Arduino's own Serial monitor could otherwise silently
overwrite genuine emulator output with a static mockup. Uncomment it if
you need to bypass the Pico link and confirm the parallel LCD driver
still works standalone.

Also note: `setup()` still unconditionally draws a built-in bitmap once
at boot, before `loop()`'s `pollPicoLink()` ever runs — so the display
will always show that static image for a moment if the Arduino is
powered before the Pico is sending real frames. This is expected and is
not the emulator doing anything; it's purely this sketch's own boot-time
demo image. **Changed this session** from `bmp_00000` to
`bmp_all_segments` — an all-segments-lit pattern is maximally visually
distinct from any real Pico-sent frame (so a display stuck on the boot
image, e.g. because the Pico link never connects, is immediately
obvious rather than looking like a plausible calculator state), and it
doubles as a real display self-test in the spirit of what a real
segmented-LCD device (including the original HP-41) does briefly at
power-on.

**Added:**

- **`drawFrameFromRAM(const uint8_t *fb)`** — identical to `drawBitmap()`
  except it reads from a plain SRAM buffer via direct array indexing
  instead of `pgm_read_byte()` (which only works on PROGMEM/flash
  pointers, wrong for a live-received buffer sitting in RAM).
- **A second, independent serial link to the Pico**, using
  `SoftwareSerial` on **D12 (RX)** / **D13 (TX, currently unused)** at
  9600 baud — deliberately *not* the Arduino's hardware UART (D0/D1),
  since those are shared with the onboard USB-serial chip and wiring
  another device to them while USB is connected causes bus contention.
  This keeps the Pico link and the USB/serial-monitor connection fully
  independent — both work at the same time.
- **`pollPicoLink()`** — drains that link and assembles incoming bytes
  into a state packet (see "Protocol summary" below for the current
  wire format — this changed this session, was originally a full
  576-byte pixel framebuffer). On a checksum match, decodes it into a
  pixel framebuffer and draws it; on a mismatch, silently drops the
  packet (keeps showing whatever was there before) rather than
  displaying something corrupted. Called every `loop()` iteration,
  alongside the original `Serial.available()` check (currently disabled,
  see above) — both work simultaneously.

### Compact wire protocol — sending raw display state instead of pixels (this session)

Originally the Pico computed the full 144×32 pixel framebuffer itself
and sent all 576 bytes over the (slow, 9600-baud, bit-banged
`SoftwareSerial`) link every update — at 9600 baud that's ~600ms just to
transfer one frame. But the only thing that actually changes between
updates is *which segments are lit*, which is already a much smaller
piece of state: `emu41gcc/display.c`'s own `lcd_a[12]`/`lcd_b[12]`/
`lcd_c[12]`/`lcd_ann` registers, exactly the same source data
`firmware/hp41_display_bridge.c` derives its pixel framebuffer from on
the Pico. So the Pico now sends *that* instead (38 bytes: `lcd_a[12]` +
`lcd_b[12]` + `lcd_c[12]` + `lcd_ann` low/high bytes) and this sketch
does the segment-decode-and-plot step itself, ~15x less data over the
wire (well under 50ms instead of ~600ms per update at 9600 baud).

This required porting `hp41_display_bridge.c`'s logic here:
`decodeAscii()` (the raw-code-to-ASCII decode, ported verbatim from
`hp41_decode_ascii()`), `plotSegment()`/`plotAnnunciator()`/`setPx()`,
and `computeFramebufferFromState()` (the ported
`hp41_display_compute_framebuffer()`) — these fill the same `frameBuf`
that used to be the *receive* buffer directly; now the receive buffer is
the much smaller `stateBuf[DISPLAY_STATE_SIZE]` (38 bytes), and
`frameBuf` (576 bytes, unchanged size) holds the pixels this sketch
derives from it before handing off to the unchanged `drawFrameFromRAM()`.

**The segment/pixel lookup tables this needs (`hp41_char_segments`,
`hp41_segment_pixels`, etc.) live in `hp41_display_tables_avr.h`** —
generated by `font-tables/gen_display_tables.py --avr`, from the exact
same 3 JSON sources (`hp41_font_table.json`,
`hp41_pixel_segment_map.json`, `hp41_annunciator_pixel_map.json`) that
produce the Pico's own `font-tables/hp41_display_tables.c` (via the same
script, no flag). The generator was refactored this session to compute
both outputs from one shared parse step, specifically so the two can't
drift apart in *content* — only re-run both commands together if the
JSON source ever changes:
```
python3 font-tables/gen_display_tables.py       > font-tables/hp41_display_tables.c
python3 font-tables/gen_display_tables.py --avr > "Arduino NHD14432/NHD14432_DisplayBridge/hp41_display_tables_avr.h"
```
The AVR variant differs only in using `PROGMEM` on every array (plus
`#include <avr/pgmspace.h>`) — without it, `const` arrays on classic AVR
still get copied into the Uno's scarce 2KB SRAM at boot (a well-known
Arduino gotcha), which would burn ~1.1KB of the ~1.15KB free. Read via
`pgm_read_byte()`/`pgm_read_word()` instead of direct dereference
throughout the ported plot/decode functions.

Confirmed after this change: flash usage went from 4620 → 6284 bytes
(14% → 19% of 32KB — the ported tables + logic), RAM from 896 → 944
bytes (43% → 46% of 2KB — `stateBuf[38]` replacing what used to be a
576-byte receive buffer, net smaller since `frameBuf[576]` already
existed for the draw step). Compiled clean via `arduino-cli`.

**This is a wire-format change, and `drawFrameFromRAM()`'s own per-byte
GDRAM write loop is completely untouched by it — but it turned out to
be the fix for the visible wipe anyway.** See "Row-by-row wipe" below
for the full story: the wipe wasn't actually caused by that loop being
slow (it never was, ~50-80ms), it was caused by the ~600ms serial
transfer stalling *both* boards before each individual frame's (already-
fast) draw step could even begin — confirmed on real hardware after
this change, where the wipe became essentially invisible.

## Wiring added

| Arduino pin | Connects to |
|---|---|
| D12 (SoftwareSerial RX) | Pico GP0 (UART0 TX), via the Pico's level shifter |
| D13 (SoftwareSerial TX) | Pico GP1 (UART0 RX), via the level shifter — wired but not currently used for anything |

D13 is also the Uno's onboard LED pin — since TX is currently idle
(nothing sent from Arduino to Pico in this direction yet), expect the
onboard LED to just stay lit rather than blink. Cosmetic only.

Same shared 3.3V/5V/GND rails on the level shifter as whatever it was
already wired for — see the main project's `firmware/pins.h` for the
full shifter-channel assignment.

## Protocol summary (must match `firmware/hp41_arduino_bridge.c` exactly)

**Changed this session** — was a full 576-byte pixel framebuffer, now
the raw display registers (~15x less data; see "Compact wire protocol"
above for why):

```
Pico -> Arduino, one state packet:
  byte 0:      0xAA (sync)
  bytes 1-12:  lcd_a[12]  (emu41gcc/display.c's shift register)
  bytes 13-24: lcd_b[12]
  bytes 25-36: lcd_c[12]
  byte 37:     lcd_ann low byte
  byte 38:     lcd_ann high byte
  byte 39:     XOR checksum of the 38 payload bytes (37-38 above)
```

`hp41_arduino_bridge_send_frame()`/`drawFrameFromRAM()`'s old full-
framebuffer format still exists on both sides (Pico: kept, unused,
in `firmware/hp41_arduino_bridge.c`; Arduino: `drawFrameFromRAM()` is
still the final draw step, just now fed by locally-decoded data instead
of a directly-received one) as a fallback, not deleted.

Baud rate (9600) must match on both ends — it's set in this sketch's
`setup()` (`picoLink.begin(9600)`) and in the Pico's
`firmware/hp41_arduino_bridge.c` (`BRIDGE_BAUD_RATE`). If either side's
value changes, update both.

## Status

Confirmed working end-to-end on real hardware (see the main project's
`CLAUDE.md` "Arduino display bridge" section for the full bring-up log)
— `arduino-cli` is installed and available in this environment, used to
compile/flash every change described here directly from the shell.

### Row-by-row "wipe" visible during real frame updates — RESOLVED

On real hardware, updating the display was visibly not instantaneous —
the user described a "wipe" scanning down the screen top-to-bottom, and
separately, a full refresh taking "a whole half second." Original
hypothesis was that `drawFrameFromRAM()`'s real, sequential per-row
GDRAM write loop, combined with the panel's own physical liquid-crystal
response time (FSTN-type panels are inherently slow to transition),
explained it. Confirmed early on that this was **not** caused by the
Pico's `TARGET_INSTRUCTIONS_PER_SEC` throttling (that only paces how
*often* a new frame is computed/sent, not how one frame gets painted).

**First optimization tried (this session): eliminated per-bit
`digitalWrite()` overhead.** `writeByte()` previously called
`digitalWrite()` 11 times per byte (1 for RS, 8 for the data bus, 2 for
the `E` strobe) — each costs ~4-6us on an Uno due to Arduino's
pin-number-to-port lookup table, dwarfing the actual bit-set operation.
Replaced the 8 data-bus calls with two direct AVR port register writes
(`writeDataPins()`): D4-D7 map to `PORTD` bits 4-7 and D8-D11 map to
`PORTB` bits 0-3 on an Uno, so one write per port covers the whole byte.
Masks (`& 0x0F` / `& 0xF0`) preserve every other pin sharing each
register — `PORTD` bits 0-3 are D0/D1 (USB Serial, must never glitch)
and D2/D3 (RS/E, still written via `digitalWrite()` separately);
`PORTB` bits 4-7 are D12/D13 (the `SoftwareSerial` Pico link) plus the
crystal bits. RS and `E` itself are left as `digitalWrite()` calls (only
3 remain per byte, not worth the added complexity to fold in) — this
cuts per-byte overhead from 11 `digitalWrite()` calls down to 3 plus 2
cheap port writes. **Deliberately left the mandatory
`delayMicroseconds(72)` post-write delay untouched** — that's the
datasheet's documented worst-case instruction-execution time, and it
can't be safely shortened or replaced with busy-flag polling since
`R/W` is hardwired to GND (write-only, per the wiring table above) — a
too-short delay risks real GDRAM corruption, which would be worse than
the cosmetic wipe. Compiled and flashed via `arduino-cli`.

**Second change, and the one that actually fixed it: the compact wire
protocol (see "Compact wire protocol" above).** The user's own numbers
gave away the real root cause: a "whole half second to completely
refresh" is far too long to be `drawFrameFromRAM()`'s own loop (always
only ~50-80ms) or plausible LC response time - it matches almost exactly
how long the *old* 578-byte packet took to transmit at 9600 baud
(~600ms), during which `uart_write_blocking()` meant **the Pico's own
Nut CPU loop was frozen too**, not just the Arduino waiting. The ROM
naturally emits several intermediate display updates in quick succession
per keypress/computation (each one its own compute -> ~600ms transfer ->
draw cycle) - stacked back to back, that reads to the eye as one long,
slow, continuous "wipe," even though any single frame's actual GDRAM
paint was always comparatively quick. Cutting the payload to 38 bytes
(well under 50ms to transmit) removed that stall almost entirely.
**Confirmed on real hardware: the wipe is now essentially invisible** -
a far bigger improvement than the `digitalWrite()`-to-port-register
change alone would predict, confirming the transfer stall (not the
per-byte write loop, and not the panel's own response time) was the
real dominant cause all along.

## Relationship to the rest of the project

See the main project's `CLAUDE.md` for the full picture — this file only
covers the Arduino side of the display bridge. The Pico side is
`firmware/hp41_arduino_bridge.h`/`.c`, called from `firmware/main.c`
instead of the direct `st7920_init()`/`hp41_display_render()` path
(which is kept intact, just unused, in the same file).
