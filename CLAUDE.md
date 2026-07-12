# HP-41CV Replica Project — Soynut

Real Nut CPU emulation running on a Raspberry Pi Pico 2, driving a real
NHD-14432WG 144x32 graphic LCD wired to look and behave like the
original HP-41 display, with keypresses coming from a computer over USB
serial for now (a physical keyboard is a possible future step).

This is the stable reference doc: confirmed architecture, current
hardware/software state, and build/run instructions — read this first,
whether you're a person or a Claude instance picking up this repo cold.
Session-by-session development history (the debugging trails, dead ends,
and reasoning that led here) lives in `DEVLOG.md`, which is gitignored
and local-only — it isn't needed to build or understand the system as
it stands today, but is kept around in case a similar bug resurfaces and
the prior investigation is useful.

**License:** GPL-2.0-or-later (see `LICENSE`) — matches `emu41gcc`'s own
terms exactly, since `firmware/` statically links its code into one
binary. **The HP-41 ROM firmware images are not included and are not
ours to redistribute** — see "ROM images" below for how to supply your
own.

## Current status

Confirmed working end-to-end on real hardware: the real HP-41 ROM boots
on the Pico 2, correctly shows `MEMORY LOST` on cold start, accepts
keypresses via a USB-serial protocol (typed by hand, or via the included
clickable on-screen keyboard GUI), and drives the physical LCD directly
over an 8-bit parallel link — three 4-channel bidirectional level
shifter boards cover the 10 shifted signals (`RS`/`E`/`DB0-7`; `R/W` is
tied straight to GND and needs no shifter channel). Both the cold-start
`MEMORY LOST` screen and live keypress-driven redraws have been
confirmed on the physical glass. Press-and-hold key behavior (USER-mode
label flash / nullify-on-long-hold) is implemented and confirmed. The
calculator auto-powers-off after each keypress the same way the real ROM
timeout logic does — see "Known unknowns" for what's still open.

The Arduino Uno display bridge (the *previous* active display path,
before the parallel level-shifter setup above replaced it) is kept fully
intact but now dormant — see "Arduino display bridge" below. The
separate direct Pico→LCD *serial* (3-wire) link remains parked, never
having lit the display — see "Direct Pico→LCD serial link" below for
that history and a note on what the parallel link's success implies
about it.

## Coding standard: NASA/JPL "Power of 10" (new code, from commit `0b65fa1` onward)

Any code written for this project's own original source **after commit
`0b65fa1`** should comply with NASA/JPL's "Power of 10" rules for
safety-critical code as closely as this project's realities allow. The
goal is long-term legibility as the codebase grows, not certification —
apply these as strong defaults, not absolute law; where a rule is
genuinely impractical here, the exception should be as explicit and
justified as the departures already documented elsewhere in this file
(e.g. the CS-polarity/GDRAM-addressing notes above).

**Reference (pinned to the exact revision this policy is based on):**
- C: https://github.com/Vhivi/Powerof10-NASA/blob/29f6f3975bba9c6d6430f8638d8d561786b04c26/rules/Powerof10-C.md
- Python: https://github.com/Vhivi/Powerof10-NASA/blob/29f6f3975bba9c6d6430f8638d8d561786b04c26/rules/Powerof10-Python.md

**Scope — applies to:** this project's own original C (`firmware/*.c/.h`
except `emu41gcc_compat/` interop shims where noted below,
`lcd_bringup/*.c/.h`, `tests/*.c`, `tools/*.c`) and Python
(`tools/*.py`, `font-tables/gen_display_tables.py`, `roms/*.py`), plus
any future edits to `Arduino NHD14432/NHD14432_DisplayBridge/*.ino`
(project-authored, C-like — apply the C rules there).

**Does NOT apply to:**
- `emu41gcc/` — a git submodule and a hard "never edit" black box (see
  "The Nut CPU core" above); there's no such thing as "new code" there
  from this project's side.
- `pico-sdk/` — an external dependency, not project code.
- `Arduino NHD14432/NHD14432_POC/` — explicitly preserved as an
  untouched, hardware-validated snapshot (see "Directory map"); it isn't
  meant to be edited going forward either.
- Code already in the repo as of `0b65fa1` — not retroactively rewritten
  for this. Only touch existing code for this reason if it's already
  being substantially rewritten for some other reason anyway.

**The 10 rules, applied to this codebase specifically:**

1. **Simple control flow — no `goto`/`setjmp`/`longjmp`, no recursion.**
   Already the de facto style here (state machines like `dokey()`-driving
   code and the key/hold bridges are loop-based, not recursive). Keep it
   that way — e.g. a future GDRAM/font-table walk should be an explicit
   loop, not recursive descent.
2. **Every loop needs a provably fixed upper bound.** Most loops here
   already iterate over fixed-size hardware constants (`LCD_HEIGHT_PX`,
   `LCD_BYTES_PER_ROW`, a `keybuffer[8]` cap) — good fit, keep bounding
   new loops the same way. The one existing pattern that's *not*
   trivially bounded is `main.c`'s USB-byte-drain loop
   (`while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT)`), which
   only terminates because the USB FIFO itself is finite, not because
   the loop counts anything — new code with a similar "drain until
   empty" shape should add an explicit max-iterations cap per Rule 2's
   own guidance for variable-bound loops, rather than relying on an
   external buffer's size as an implicit bound.
3. **No dynamic allocation after init.** Already the norm — display/key
   buffers are static/global arrays (`framebuf[LCD_FB_SIZE]`,
   `keybuffer[]`), never `malloc()`'d. Keep new buffers static or
   stack-allocated with a fixed compile-time size.
4. **~60 lines per function.** Some existing functions (`main()`'s loop
   body, `hp41_key_bridge_feed_byte()`'s state machine) already run
   longer than this and aren't being split up retroactively, but new
   functions should target this, splitting out logical sub-steps (the
   Arduino sketch's `computeFramebufferFromState()`/`pollPicoLink()`
   split is a reasonable model).
5. **≥2 assertions per function, side-effect-free, explicit recovery on
   failure.** Use `assert()` (`<assert.h>`, works fine under the Pico
   SDK/newlib) for pre/postconditions and invariants in new C code;
   Python's `assert` for the tooling scripts. "Explicit recovery action"
   on this project's bare-metal firmware realistically means what
   `main.c` already does for invalid opcodes: report clearly over the
   debug UART, then halt in a tight loop rather than silently continuing
   into undefined behavior — there's no OS/exception handler to hand an
   error to. Follow that existing pattern for new hard-invariant
   failures rather than inventing a new one.
6. **Smallest possible variable scope.** General good practice, applies
   as-is. Note `nut_globals.c`'s file-scope globals
   (`tabpage`/`espaceRAM`/etc.) are a required exception, not a
   violation to fix — they exist purely because `emu41gcc/nutcpu.h`'s
   vendored `GLOBAL` macro pattern demands real storage somewhere, and
   that file's entire job is providing it (see "The Nut CPU core"
   above). New project code should still default to the narrowest scope
   that works.
7. **Check every non-void return value; validate every parameter.**
   Worth being deliberate about here specifically because several
   existing bridge functions (`hp41_key_bridge_feed_byte()`,
   `hp41_display_compute_framebuffer()`) are `void` by design and can't
   surface an error — new functions that *can* fail should return a
   status the caller actually checks, not just log and continue.
8. **Preprocessor limited to header inclusion + simple macros; minimize
   conditional compilation.** `firmware/emu41gcc_compat/`'s force-includes
   and `-fcommon`/`-include` CMake flags are a justified, narrowly-scoped
   exception — they exist solely to compile vendored DOS-era C
   unmodified (see "Firmware" below), not as a pattern to extend
   elsewhere. New project code shouldn't add new conditional-compilation
   directives or macro-heavy tricks beyond that existing, contained
   exception.
9. **Restrict pointers to one level of dereference; no function
   pointers.** This project's own driver/bridge code (`st7920.c`,
   `hp41_*_bridge.c`) already stays at one level (`const uint8_t *fb`,
   simple array pointers) with zero function pointers — keep it that
   way. (`emu41gcc`'s internal opcode dispatch may use deeper
   patterns internally; that's vendored and out of scope, per "Does NOT
   apply to" above.)
10. **Compile with the most pedantic warnings enabled, zero warnings;
    static analysis.** Not yet wired into `firmware/CMakeLists.txt` for
    this project's own sources (`-Wall -Wextra -Wpedantic` scoped to the
    non-vendored `.c` files, alongside a Python linter like `ruff` for
    the tooling scripts) — worth doing as a deliberate follow-up if
    wanted, since scoping it only to project-owned files avoids fighting
    vendored code that was never written to be warning-clean. Ask before
    assuming this should happen automatically, since enabling `-Werror`
    could surface a real backlog of warnings in existing files.

## Hardware

- **Display:** Newhaven NHD-14432WG-BTFH-VT, ST7920 controller, 144x32
  graphic LCD. Supports both 8-bit parallel and 3-wire serial
  (`/CS`/`SID`/`SCLK`) interfaces, selected by an onboard jumper (J3
  shorted/J4 open = parallel, the board's default; J4 shorted/J3 open =
  serial) — not a wire from the MCU, so switching modes means physically
  moving the jumper. Full 16-pin connector: `VSS`, `VDD`, `VO` (contrast
  — explicitly "Do Not Connect" on this fixed-contrast variant), then
  either `RS`/`R/W`/`E`/`DB0-DB7` (parallel) or `/CS`/`SID`/`SCLK` (serial,
  pins 7-14 tied to GND), then `LED+`/`LED-`. No PSB or RST pin exists on
  this connector. In parallel mode `E` is falling-edge triggered.
- **MCU:** Raspberry Pi Pico 2 (RP2350), raw Pico C SDK (not
  Arduino-style) — chosen over an Arduino Uno prototype because the
  emulator needs far more RAM/flash than an Uno has (Uno: 2KB SRAM/32KB
  flash; Pico 2: 520KB SRAM, 2-4MB flash).
- **Backlight:** intentionally disconnected — the real HP-41C has none.
- **CS polarity — only relevant to the dormant serial link:** parallel
  mode (the active path) has no `/CS` pin at all — the datasheet's
  parallel pin table uses that same physical pin for `RS` instead. For
  the serial link, the LCD module's own datasheet
  (`reference-material/datasheets/NHD-14432WG-BTFH-VT.pdf`) says "Active
  LOW Chip Select," but the real ST7920 controller datasheet
  (`reference-material/datasheets/ST7920.pdf`) confirms active-**HIGH**
  three separate ways (pin table, serial timing diagram, and its own
  8051 reference code) — a genuine conflict between the two vendor
  documents, resolved in favor of the controller datasheet in
  `lcd_bringup/`'s (dormant, serial-mode) `st7920.c`.
- **GDRAM addressing:** vertical address 0-31 maps directly to y=0-31,
  9 words/row, no bank-fold — confirmed against a separately
  hardware-validated Arduino reference implementation, via full
  logic-analyzer capture on the (dormant) direct serial link (every
  GDRAM write decoded and cross-checked), and again on the active direct
  parallel link (checkerboard test pattern via `lcd_bringup/` rendered
  correctly-aligned on real glass). `st7920_draw_frame()`/`st7920_clear()`
  implement this; it's a controller-level fact, not bus-specific, so the
  same addressing code carried over unchanged from serial to parallel.
- **Level shifting:** required — `VDD` is 5V and the logic-high input
  threshold (0.7×VDD ≈ 3.5V) is above the Pico's ~3.3V GPIO output high.
  10 signals need shifting for the active parallel link (`RS`/`E`/
  `DB0-7`) — one 4-channel board isn't enough (that's what limited the
  LCD to serial mode for a while), so three identical 4-channel
  bidirectional boards (BSS138-based, auto-sensing — the same type
  described in SparkFun's "Bi-Directional Logic Level Converter Hookup
  Guide"; a RobotDyn "Logic Level Converter, Bi-Direction" board's pinout
  is in `reference-material/datasheets/20171225012850PINOUT-LogLevel.pdf`
  for reference) are used instead, 12 channels total with 2 spare.
  `R/W` is permanently tied straight to GND (never driven — a
  write-only design, no busy-flag reads) and needs no shifter channel at
  all, since it's a constant 0V signal on both voltage domains.
  Confirmed working on real hardware: `lcd_bringup/`'s solid-fill and
  checkerboard test patterns rendered correctly, then the full ROM's
  `MEMORY LOST` cold-start screen and live keypress-driven redraws all
  confirmed on the physical glass — no actively-driven chip
  (74LVC245/etc., previously recommended below for this exact scenario)
  turned out to be necessary; passive auto-sensing shifters work fine
  here, at least at this bus's clock speed. Notably, an *earlier*
  attempt at the 3-wire serial link with a single board of this exact
  same shifter type never lit the display at all — since swapping to
  parallel with more of the identical shifter type immediately worked,
  the shifter hardware itself was likely never that attempt's actual
  problem. See "Direct Pico→LCD serial link" below, left parked rather
  than re-investigated now that parallel works.
- `firmware/pins.h` has the full pin-by-pin wiring table (LCD, level
  shifter, Arduino UART link, Pico power pins) — confirm/edit against
  actual wiring before flashing; the exact GPIO assignments are not
  carried over from any external known-working prototype.

## ROM images — bring your own

**The ROM files themselves are not in this repo, and never should be.**
They're HP's copyrighted HP-41 calculator firmware, not open source —
this project has no rights to redistribute them, regardless of the
license on the emulator/replica code itself. `roms/*.ROM`, `roms/*.rom`,
and the generated `roms/rom_images.c` are all gitignored. See
`roms/README.md` for the full BYO process; short version:

- Only `NUT0.ROM`/`NUT1.ROM`/`NUT2.ROM` (the base HP-41 OS, 3 pages) are
  wired into the build. Supply your own legally-obtained copies (e.g.
  dumped from a physical calculator you own, or extracted from emulator
  software you're licensed to use), place them in `roms/`, then run
  `python3 roms/rom_to_c.py NUT0.ROM NUT1.ROM NUT2.ROM > roms/rom_images.c`
  to (re)generate the C source the build actually compiles.
- **Format:** each file must be 8192 bytes = 4096 words, **big-endian
  uint16_t**, values 0-0x3FF (10-bit words in 16-bit slots, unpacked).
  Matches `emu41gcc/nutcpu.c`'s `tabpage[16]` structure exactly (`short*`
  arrays, page = top 4 bits of a 16-bit address, offset = low 12 bits) —
  no translation needed beyond the endian swap. `roms/check_rom_format.py`
  is a standalone sanity-checker if you want to verify a file/its byte
  order first.
- Expansion ROMs (`XNUT0-2.ROM`, `CXFUNS0-1.ROM`, `ADV0-2.ROM`,
  `TIMER.ROM`, `PRINTER.ROM`, `CrdRdr-1E.rom`) aren't wired into the
  build yet (no plug-in-module support), but the same BYO/format rules
  would apply if that's added later.

## The Nut CPU core: `emu41gcc/`

`emu41gcc` (GPL-2.0-or-later gcc port of J-F Garnier's original DOS-era
Emu41) is the emulation core this project adapts, not a from-scratch
build — and it's a **git submodule** pointing at the upstream repo
(`github.com/mmoller2k/emu41gcc`), pinned to a specific commit, rather
than a vendored copy of its files. This isn't just tidiness: a submodule
makes the "never edit it" rule below structural rather than just a doc
convention — there's no way to change anything inside it from a commit
in this repo at all; you'd have to `cd emu41gcc`, commit there against
the upstream remote, and then update this repo's submodule pointer,
which is a deliberate, visible, separate action.

**Cloning this repo:** `git clone --recurse-submodules <this repo's URL>`
(or, if already cloned without that flag: `git submodule update --init`).

**Hard rule: `emu41gcc/` is a black box. Never edit any file inside it**
— not a one-line fix, not a portability shim. All build-compatibility
work (missing DOS headers, missing printer/timer/HPIL prototypes, stub
globals, `-fcommon` link fixes) lives in `firmware/emu41gcc_compat/` and
is wired in purely from `firmware/CMakeLists.txt` (include-path shims,
`-include` force-includes, per-file compiler flags — see "Firmware"
below). If a future change seems to require touching a file in
`emu41gcc/`, find another adapter-layer trick instead.

**Licensing note:** `emu41gcc`'s own files grant "version 2 of the
License, or (at your option) any later version" (see its `COPYING.TXT`
and e.g. `emu41.c`'s header) — i.e. GPL-2.0-or-later, not GPL-2.0-only.
Since `firmware/` statically links `nutcpu.c`/`display.c` into one
binary, the resulting firmware is a combined work under GPL's copyleft
terms; this project's own code is licensed GPL-2.0-or-later to match
exactly (see `LICENSE` at the repo root) rather than leave any
compatibility ambiguity.

**Key structures** (`nutcpu.h`): `tabpage[16]` (ROM page pointers),
`typmod[16]` (module type per page, 1=ROM), `espaceRAM[8200]`
(calculator RAM), registers (`regA/B/C/M/N[14]`, `regPC`, `regST`, etc.),
`int executeNUT(int n)` (main entry point — runs n instructions, returns
0=OK/1=POWOFF/2=invalid opcode/3=breakpoint).

**Opcode dispatch** (`nutcpu.c`, 1525 lines): `executeNUT(n)` fetches
the word at `regPC`, dispatches on `mot&3` to `exec0()` (NOP, WMLDL,
ENROM1-4, status/flag bits, `LC h`, register moves, `SELPF`, `CXISA`,
RTN variants, POWOFF, DISOFF/DISTOG), `exec1()` (two-word absolute
jump/call), `exec2()` (register/arithmetic — field-selector + ~30 ops;
add/subtract branch decimal-vs-hex via `flagdec`, the real mechanism
behind the HP-41's decimal/hex modes), or `exec3()` (single-word
relative branch) — or `execp()` instead if a prior `SELPF` set
`smartp=1` (HP-IL/HP82143-printer peripheral dispatch; not needed for
base HP-41CV bring-up, but its call signatures pin down the stub
functions below).

**Important:** `nutcpu.c` has two `executeNUT()` definitions gated on
`#ifdef VERS_ASM`. **Never define `VERS_ASM`** when building for the
Pico — the `#else` branch calls an external x86-assembly
`executeNUT2()` (`nutcpu2.asm`, not in this repo, not portable to ARM);
the plain-C `#ifndef VERS_ASM` branch is complete and sufficient on its
own (several opcode groups — LC, PT ops, SELPF, N/STK ops, CXISA, RCR,
`popaddr`/`pushaddr` — are themselves only compiled in that branch).

**Keyboard:** `dokey()` is the state machine every keyboard-facing
opcode (`CHKKB`/`RSTKB`/`C=KEYS`) funnels through
(`flagKB`/`flagKey`/`cptKey`/`regK`/`keybuffer[]`/`lgkeybuf`, all plain
externals via `nutcpu.h`'s `GLOBAL` macro). Injecting a keypress is just
appending to `keybuffer[lgkeybuf++]` (an 8-bit row/column code). By
default every `keybuffer[]` entry becomes a fixed ~200-instruction
"pressed" pulse, decoupled from real hold duration — see "Key
hold-duration" below for how this project drives real press/release
timing anyway, without touching `dokey()` itself (a same-file
`#define`-rename override was tried and found structurally impossible:
`dokey()`'s 3 call sites are all inside `nutcpu.c`, so a textual rename
hits the callers identically and can't selectively redirect them).

**Display:** `display.h`/`display.c` hold three parallel 12-nibble shift
registers (`lcd_a`, `lcd_b`, `lcd_c`), one set per of the 12 display
character positions: `code = (lcd_c[i]<<8) | ((lcd_b[i]&3)<<4) |
lcd_a[i]` (10-bit char code), `punct = lcd_b[i]>>2` (2-bit:
0=blank/1=period/2=colon/3=comma). Index 11 is the **leftmost** screen
position (index 0 is rightmost). Do **not** use the reference
`alpha41()`/`display_to_buf()` — those produce rough ASCII for a text
console; this project decodes to real segment shapes instead (see
"Display bridge" below). The annunciator row is `lcd_ann` (see
`ann_to_buf()` for its bit layout: `BAT`/`USER`/`G`/`RAD`/`SHIFT`/
program-step digits `0`-`4`/`PRGM`/`ALPHA`).

**Peripheral stubs:** `timer.h`/`hpil.h`/printer functions
(`hpil_wr`/`hpil_rd`/`print_char`/`get_printer_status`/
`test_printer_flag`/`timer_wr_n`/`timer_rd_n`) all no-op/return 0 —
correct for a base HP-41CV with no clock/HP-IL/printer module plugged
in.

## Font / display segment tables

- 14 real character segments + 3 punctuation marks per position (top,
  upper/lower-left/right vertical/diagonal, upper/lower-center vertical,
  mid-left/right horizontal, bottom; plus top-dot/bottom-dot/comma-tail)
  — matches the official spec. HP-41 display character codes are plain
  ASCII for the printable range (32-127); codes 128-255 mirror 0-127 (the
  high bit is a printer-control flag, not a different glyph).
- `font-tables/hp41_font_table.json`/`.txt`: all 128 codes, each a
  14-bit segment-on/off string. Codes 32-127 are validated **except**
  codes 102-125 (`f`-`z` minus `a`-`e`, plus `{`/`|`/`}`), which share
  the same "all segments on" extraction-failure sentinel as codes 0-31
  and are rendered blank rather than garbled.
  `font-tables/gen_display_tables.py` detects this by value (any
  all-1s bit string), not by range. If real glyphs for that range are
  ever needed, they need re-extraction — see
  `reference-material/font-tables-source/` for the original design
  files, and `reference-material/display-mockups/` for the mockups the
  original (working) extraction was done from.
- Segment bit order:
  `top, upper_left_vert, upper_right_vert, upper_left_diag, upper_right_diag,
  upper_center_vert, mid_left, mid_right, lower_left_vert, lower_right_vert,
  lower_left_diag, lower_right_diag, lower_center_vert, bottom`
- `font-tables/hp41_pixel_segment_map.json`: which actual GDRAM pixels
  correspond to each named segment, for this 144x32 display. Each
  segment name maps to `[x,y]` pixel offsets local to a character cell's
  top-left corner (x: 0-11, y: 0-31 absolute; add `i*12` to x for cell
  *i*, 0-11). The annunciator row (y=21-25) is excluded from this map —
  it's handled separately below.
- `font-tables/hp41_annunciator_pixel_map.json`: the 12 `lcd_ann` bits'
  **absolute** GDRAM pixels (not per-cell — each annunciator is a single
  static label, not a shared-geometry glyph).
- Punctuation-column pixels (`dot_top`/`dot_bottom`/`comma_tail`) have
  not been cross-checked against a rendered real colon/period/comma the
  way the printable characters were — worth a sanity pass before fully
  relying on them.

`font-tables/gen_display_tables.py` compiles the three JSON sources
above into `font-tables/hp41_display_tables.h`/`.c` (checked in and
regenerable): `hp41_char_segments[128]`, a flattened
`hp41_segment_pixels[]`/`_pixel_offset[17]`/`_pixel_count[17]` (17 = 14
segments + 3 punctuation marks), and the equivalent
`hp41_annunciator_bits[12]`/`_pixels[]`/`_pixel_offset[12]`/
`_pixel_count[12]`. The same script's `--avr` mode emits a PROGMEM
version of the same tables for the Arduino side
(`Arduino NHD14432/NHD14432_DisplayBridge/hp41_display_tables_avr.h`),
from one shared parse of the same JSON — the two can't drift apart in
content.

## Display bridge — `firmware/hp41_display_bridge.h`/`.c`

- `hp41_display_compute_framebuffer(uint8_t *fb)` — pure logic, no
  hardware access. Reads `emu41gcc/display.c`'s `lcd_a`/`lcd_b`/`lcd_c`/
  `lcd_ann` globals directly (declared `extern` here, no header exposes
  them), decodes each of the 12 positions' raw code to ASCII
  (`hp41_decode_ascii()` — re-derived from `display.c`'s static
  `alpha41()`, which is nontrivial: the 3 registers pack a sparse 0-63 ∪
  256-319 value, not a clean 7-bit range), and plots lit
  segments/punctuation/annunciators into `fb` (144x32, 1bpp, row-major,
  MSB-first).
- `hp41_display_render(void)` — the above, plus `st7920_draw_frame()` to
  push it to hardware directly (used only if the direct-drive path is
  active — see below; the live path instead sends raw display state to
  the Arduino bridge).
- Verified via `tests/display_bridge_test.c`: boots the ROM, computes
  the framebuffer (never touches real hardware — a no-op
  `st7920_draw_frame()` stub is defined locally), and checks the
  "MEMORY LOST" cold-start render's lit-pixel count against an
  independently-computed expectation (207, exact match) — confirms cell
  ordering, ASCII decode, and segment-to-pixel plotting all at once (an
  exact match also proves no two simultaneously-lit segments ever
  overlap a pixel). The same test pokes `lcd_ann` directly (per-bit and
  all-12-at-once) and checks each expected pixel count exactly.

## Key bridge — `firmware/hp41_key_bridge.h`/`.c`

Translates incoming USB serial bytes into presses on `keybuffer[]`/
`lgkeybuf` (drained by `dokey()` exactly like a real keyboard scan).
Pure logic — `hp41_key_bridge_feed_byte(int c)` is the whole API, plus
`hp41_key_bridge_reset()` for deterministic/test-isolated state.

- **Direct ASCII keys:** a 128-entry `tabcode[]` table (sourced
  unchanged from `emu41gcc/emu41.c`'s `traite_touche()` — only this one
  data table was copied out, not the DOS console app it lives in) maps
  most bytes straight to an HP-41 keycode: digits, `+-*/`, letters
  (ALPHA mode), Enter (CR/LF), Backspace (→ CLX), ctrl-A/R/X (→
  ALPHA/R-S/XEQ).
- **Named keys with no ASCII equivalent** (`ON`, `SHIFT`, `USER`,
  `PRGM`, `SST`, `BST`, `X<>Y`, `RDN`): sent as `[NAME]`
  (case-insensitive) — `[` is safe to repurpose as an escape character
  since `tabcode[]` maps it to 0 (no real key produces it). `BST` has no
  dedicated keycode on real hardware (it's SHIFT+SST) and is sent as
  that literal two-key sequence. Malformed sequences (unrecognized
  name, unterminated `[`, nested `[`) all recover cleanly via an
  explicit `STATE_OVERFLOW`/restart-on-new-`[` handling rather than
  misfiring partial names as raw keypresses.
- `keybuffer[]`'s 8-slot cap matches `emu41gcc`'s own `push_key()` —
  extra presses beyond 8 pending are silently dropped.
- Verified via `tests/key_bridge_test.c` (17 checks, including
  adversarial malformed-bracket input).

### Press-and-hold — `firmware/hp41_key_hold_bridge.h`/`.c`

The real HP-41 nullifies a key if held too long (~1245 throttled
instructions / ~6ms at this project's throttle rate — see the Owner's
Handbook's USER-mode "hold to see label, hold too long to cancel"
behavior), which the ROM detects by polling `CHKKB` in a decrementing
counter loop (confirmed via ROM disassembly — see `NLT010`/`NULT10`
labels around `0x0E97`-`0x0ED7`, cross-referenced against
`SYSTEMLABELS.TXT`). Since `dokey()` can't be overridden (see above),
this project instead sustains the state directly from outside:

- `hp41_key_hold_press(keycode)` — pushes the keycode into `keybuffer[]`
  and begins tracking a hold.
- `hp41_key_hold_release()` — ends it.
- `hp41_key_hold_sustain()` — while a hold is active, forcibly
  re-asserts `flagKB=1`/`regK=<held keycode>` every call; a no-op
  otherwise.
- `firmware/main.c`'s main loop single-steps `executeNUT(1)` (not the
  normal 1000-instruction batch) while a hold is active, calling
  `hp41_key_hold_sustain()` before every single instruction — required
  because the ROM's hold-check loop clears and re-reads `flagKB` within
  a handful of instructions each iteration. Only the `executeNUT()` call
  itself is single-stepped; USB byte-draining and the heartbeat/pacing
  overhead still run at their normal cadence, so holding a key doesn't
  otherwise slow the system down.

**Wire protocol extension** (`firmware/hp41_key_bridge.c`, additive to
`[NAME]` above): `"[+X]"` begins a real press-and-hold of `X` (a
`named_keys[]` entry, or any single character resolved via `tabcode[]`);
`"[-]"` releases whatever's currently held (only one key tracked at a
time). Two-code combos (`BST`) can't be meaningfully held and are
silently ignored.

Verified via `tests/key_hold_test.c` (11 checks, simulating the ROM's
own repeated `flagKB`/`regK`-clearing) and `tests/hold_trace_test.c`
(boots the real ROM, holds a real function key via the wire protocol,
single-stepping like `main.c`) — a short tap never nullifies; an
unreleased hold drives the ROM into the nullify branch at exactly the
expected instruction count.

## Software keyboard GUI — `tools/hp41_keyboard_gui.py`

A Tkinter window displaying
`HP-41CX_Programmable_Scientific_Calculator_(removed_background,_colour_adjustment).jpg`
(a real HP-41CX keyboard photo, cropped to just the keyboard; CC BY-SA
3.0, Wikimedia Commons — derivative of a photo by Sven.petersen,
retouched by Pittigrilli — see full attribution in the script's header)
with an invisible clickable rectangle over every physical key.
`KEY_MAP`'s 39 hit-boxes were derived from gridded/labeled crops of the
photo (not eyeballed) and verified by rendering all computed boxes back
onto the full image, confirming each lands tightly on its own key with
no overlaps. Clicking a key sends exactly the bytes a human typing at a
terminal would send — no firmware/protocol changes were needed; every
key already maps to something `hp41_key_bridge.c` understands (a plain
`tabcode[]` byte, or a `[NAME]` escape).

**Three button-behavior modes** (`PressMode`, switchable live via radio
buttons or `--press-mode`):
- `tap` — every click sends an instant tap immediately; the hold
  protocol is never touched.
- `hold` — every press immediately engages the real hold protocol
  (`"[+X]"`/`"[-]"`) with no delay. Kept for comparison/reproduction, not
  the recommended mode.
- `threshold` (default) — waits `HOLD_ENGAGE_MS` (150ms) after
  mouse-down before deciding whether this is a hold or a quick tap; a
  release before that engages a plain instant tap instead. This
  threshold exists because a GUI click's round-trip latency (mouse
  event → Python → pyserial → USB → Pico) can itself exceed the ROM's
  ~6ms blink threshold — the tap/hold distinction has to be made at the
  GUI/UX layer, not tuned in firmware or ROM-cycle terms.

No `<Enter>`/`<Leave>` hover-highlight bindings exist — macOS Aqua Tk has
a quirk where changing a canvas item's appearance while the pointer is
over it can spuriously retrigger Enter/Leave in a tight loop that
presents as a total freeze; only one-shot `<Button-1>`/
`<ButtonRelease-1>` bindings are used.

Also includes a live serial log pane: the Pico's single USB CDC
connection carries both key input and `main.c`'s own debug output, so
the GUI reads that connection in a background thread and displays it,
doubling as a debug console.

Usage: `python3 tools/hp41_keyboard_gui.py [--port /dev/cu.usbmodemXXXX]`
(auto-detects the Pico's port if omitted, excluding anything that looks
like the Arduino display bridge). Needs `pyserial` and `Pillow`.

## Direct Pico→LCD parallel link — the active display path

```
Pico 2 (Nut CPU emulator)  --8-bit parallel, via 3x level shifter boards-->  LCD
```

The Pico drives the LCD's 8-bit parallel bus (`RS`/`E`/`DB0-7`) directly
— no second board in the loop. This replaced the Arduino bridge below
once three 4-channel level shifter boards were on hand (one board only
covers the 3-wire serial link's 3 signals, not parallel's 10) — see
"Hardware"'s level-shifting note above for the shifter details and
`pins.h` for the exact GPIO/board/channel wiring table. **Confirmed
working end-to-end** on real hardware: `lcd_bringup/`'s solid-fill and
checkerboard test patterns rendered correctly through the new wiring
before the full firmware was ever flashed (isolating the bus/wiring
question from the ROM/emulator stack entirely), and the full firmware
then showed a correct `MEMORY LOST` cold-start screen plus live
keypress-driven redraws.

- **Pico side** (`firmware/st7920.c`/`.h`): `st7920_init()` configures
  `RS`/`E`/`DB0-7` as GPIO outputs and runs the ST7920 power-on command
  sequence (unchanged from the previously-dormant serial-mode version of
  this file — the command set and its datasheet-driven timing are
  controller-level facts, not bus-specific). `st7920_draw_frame()`
  writes a full framebuffer to GDRAM; `main.c` calls it directly from
  the main loop's `fdsp`-triggered render block, replacing the old
  Arduino-bridge send call — no framing/pacing logic is needed here (no
  second, independently-clocked board's receive timing to coordinate
  with, unlike the Arduino path below).
- Byte transport (`write_byte()` in `st7920.c`): sets `RS` + the 8 data
  lines, raises `E`, waits briefly, then drops `E` — the datasheet's own
  pin table says `E` is **falling-edge triggered**, and its 8051
  reference code (`Wcom()`/`Wdata()`) confirms the same sequence
  bit-for-bit. `R/W` is never touched from software — it's hardwired to
  GND (see "Hardware" above).
- `lcd_bringup/` (see "Directory map" below) carries the same driver
  logic and was used to validate the new wiring in isolation before
  touching the main firmware - kept in the repo for reuse if the wiring
  or shifter setup ever changes again.

## Arduino display bridge — dormant, kept as a fallback

```
Pico 2 (Nut CPU emulator)  --UART, 9600 baud-->  Arduino Uno  --8-bit parallel-->  LCD
```

This *was* the active display path (before the direct parallel link
above replaced it): the direct Pico→LCD *serial* link (see "Direct
Pico→LCD serial link" below) never produced visible output on real
hardware despite an exhaustively verified-correct protocol, so display
output was routed through an Arduino Uno already independently validated
against this exact LCD panel in parallel mode, as an interim measure.
Once three level shifter boards made a direct parallel link possible, it
replaced this path outright. This section, `hp41_arduino_bridge.h`/`.c`,
and the Arduino sketch are all kept fully intact and unmodified — not
deleted — in case the direct link ever needs to be bypassed again;
`main.c` just doesn't call into it right now (its calls are commented
out at the same marked spots, mirroring how the direct-drive path used
to be the commented-out one). Confirmed working end-to-end on real
hardware in its own right, while it was the active path.

- **Pico side** (`firmware/hp41_arduino_bridge.h`/`.c`):
  `hp41_arduino_bridge_init()` sets up UART0 on GP0/GP1 (the same
  physical pins the dormant direct-serial LCD link uses — not a live
  conflict, since that link is inactive while this is in use).
  `hp41_arduino_bridge_send_display_state()` sends the raw
  `lcd_a/b/c[12]`/`lcd_ann` display registers directly (38 bytes, framed
  as `[0xAA sync][38 payload][1 XOR checksum]`) rather than a
  pre-rendered framebuffer — cut from an earlier 576-byte
  full-framebuffer format (`hp41_arduino_bridge_send_frame()`, kept
  intact but unused) for a ~15x smaller payload. `main.c` calls this
  instead of `st7920_init()`/`hp41_display_render()` — both of those and
  `firmware/st7920.c` are kept fully intact, just unused, so the direct
  path can resume once a better level shifter arrives.
- **Arduino side** (`Arduino NHD14432/NHD14432_DisplayBridge/`, a copy
  of the original hardware-validated `NHD14432_POC/` sketch, which is
  kept as its own untouched snapshot): `drawFrameFromRAM()` (like
  `drawBitmap()` but reads a RAM buffer instead of PROGMEM),
  `computeFramebufferFromState()` (decodes the raw display registers
  into a pixel framebuffer locally, ported from
  `hp41_display_bridge.c`), and `pollPicoLink()` (assembles incoming
  bytes into a state packet, checksum-validates, decodes+draws on
  match). Uses `SoftwareSerial` on D12(RX)/D13(TX) rather than the Uno's
  hardware UART, which is reserved for USB.
- Full wiring documented in `firmware/pins.h` and
  `Arduino NHD14432/NHD14432_DisplayBridge/CLAUDE.md`.

**Confirmed correctness method:** `main.c` prints an XOR checksum (and,
optionally, full ASCII-art) of every computed framebuffer to the Pico's
own USB console before sending it. The `MEMORY LOST` and ready-state
checksums match the Arduino's own independently-computed test-bitmap
checksums byte-for-byte — a repeatable way to verify display correctness
without trusting the physical glass or the link. **Caveat:** a matching
checksum does not by itself distinguish "blank" from real content (some
genuinely different frames coincidentally XOR to the same value) —
only the ASCII art (or a stronger hash) can tell them apart with
certainty.

**Known, fixed bugs in this path** (all confirmed on real hardware):
- The Arduino's frame receiver had no resync mechanism — a single
  dropped `SoftwareSerial` byte mid-frame would miscount every
  subsequent byte forever. Fixed with `FRAME_TIMEOUT_MS` (1000ms): a
  stalled frame is abandoned and the receiver waits for a fresh sync
  byte instead of staying wedged.
- `main.c` didn't let the CPU "sleep" after `POWOFF` — real hardware
  halts the clock entirely and resumes at address 0 on a keyboard-scan
  interrupt, but `main.c` was calling `executeNUT()` forever regardless,
  running whatever ROM code follows `POWOFF` as a busy loop. Fixed with
  an `asleep` flag: `executeNUT()` calls are suppressed until
  `lgkeybuf>0`, at which point `regPC=0` and `flagKey=0` are reset
  (mirroring `emu41gcc/emu41.c`'s own reference main loop) before
  resuming.
- CPU speed throttling: the Pico runs the core at ~1.4M instructions/sec
  natively, far faster than the real Nut CPU's actual clock.
  `TARGET_INSTRUCTIONS_PER_SEC` (200,000, an approximate commonly-cited
  figure, not cycle-exact) is enforced via `sleep_us()` pacing.
- **The "screen goes blank, catches up on next key" bug** (root cause,
  confirmed): the ROM legitimately writes several rapid display updates
  per keypress-driven wake (a real "settle, settle, final" pattern, not
  a bug), ~55ms apart. `SoftwareSerial`'s receive is interrupt-driven,
  and the Arduino's GDRAM write loop is precisely timed but doesn't
  disable interrupts — so a burst's next frame arriving mid-draw could
  corrupt the write in progress, even though the packet that triggered
  it had already checksum-verified cleanly. Fixed by wrapping the
  physical write in `noInterrupts()`/`interrupts()` in
  `NHD14432_DisplayBridge.ino`'s `pollPicoLink()`. A related regression
  (CLX/backspace produces 4 rapid updates instead of 3, and disabling
  interrupts for the whole draw made *reception* impossible rather than
  just unreliable during a burst) was fixed by pacing sends from the
  Pico side instead: `MIN_ARDUINO_SEND_INTERVAL_MS` (180ms) enforced a
  minimum gap between sends, in `main.c` while this path was active (the
  constant has since been removed now that the direct parallel link
  doesn't need it — recoverable from git history). Note the gap had to
  account for `uart_write_blocking()` only blocking until bytes are
  queued into the UART hardware FIFO, not until they've finished
  physically transmitting (~42ms for this payload size at 9600 baud) —
  the interval needed to cover that transmission tail plus the Arduino's
  draw time.
- This entire class of bug (a receive-while-drawing rate mismatch
  between two independently-clocked boards) was specific to the
  two-board architecture and, as predicted, hasn't recurred with the
  direct Pico→LCD parallel link now in use — a single-chip path has no
  second board's asynchronous serial reception competing for the same
  write timing.

**Tooling for this path:**
- `arduino-cli` (`brew install arduino-cli`, `arduino:avr` core):
  ```
  arduino-cli compile --fqbn arduino:avr:uno "Arduino NHD14432/NHD14432_DisplayBridge"
  arduino-cli upload -p /dev/cu.usbmodem<port> --fqbn arduino:avr:uno "Arduino NHD14432/NHD14432_DisplayBridge"
  ```
  `arduino-cli board list` identifies which `/dev/cu.usbmodem*` is the
  Arduino (shows a board name) vs. the Pico (shows `Unknown`).
- `picotool` (`brew install picotool`): `picotool reboot -f -u` forces a
  running Pico into BOOTSEL mode remotely; it mounts as
  `/Volumes/RP2350`, and `cp firmware/build/soynut.uf2 /Volumes/RP2350/`
  flashes it (the volume disappearing confirms success). Prefer this
  reflash pattern over `picotool reboot -a -f` if scripting resets from
  Python — the latter can hang when invoked via `subprocess.run()`.
- `pyserial` (`pip3 install pyserial`) for scripted serial access
  (`serial.Serial(port, 115200, timeout=1)`, set `.dtr`/`.rts = True`
  after opening). **Check for and kill stray `cat`/`screen`/`python3`
  processes holding the port before opening a new connection**
  (`lsof /dev/tty.usbmodem*`, `screen -ls`) — a leftover process can
  silently block a new connection attempt with no error.

## Direct Pico→LCD serial link — implemented, protocol-verified, still never lit up

**Superseded by the direct *parallel* link above, which is the active
display path now.** This section is kept for its own sake (a real,
unresolved investigation) but isn't blocking anything anymore, and
hasn't been revisited since parallel started working. One relevant new
data point: the parallel link's three level shifter boards are the
*same type* (BSS138-based, auto-sensing) as the single board this serial
attempt used, and parallel worked immediately. That makes it unlikely
the shifter hardware itself was ever this attempt's problem — something
more specific to the serial protocol, wiring, or CS polarity guess (see
below) is the more likely remaining suspect, if this is ever picked back
up.

A direct 3-wire serial connection from the Pico to the LCD (bypassing
the Arduino) was fully implemented (`firmware/st7920.c`/`.h`,
`firmware/pins.h`, and `lcd_bringup/`'s copies of the same) but never
produced visible output. All of those files have since been overwritten
in-place with the parallel version now that parallel is the
active/validated approach — recover the serial version from git history
if this investigation is ever resumed.

**What's been proven correct**, without the display ever lighting up:
- CS polarity, sync-byte/nibble framing, and GDRAM addressing all match
  the real ST7920 datasheet and a logic-analyzer capture of the actual
  signals at the LCD's own pins: all 1920 CS pulses in a full capture
  decoded to exactly 24 bits with zero errors, correct pulse widths and
  inter-pulse gaps, and the decoded byte stream reconstructs the entire
  expected GDRAM command sequence exactly.
- Init timing was fixed to match the ST7920 datasheet's power-on
  flowchart (>100us after Function Set, >100us after Display ON/OFF,
  >10ms after Display Clear) — the controller has no instruction buffer
  and silently drops commands sent before it finishes the previous one.
- A standalone bring-up project, `lcd_bringup/` (own `CMakeLists.txt`,
  zero dependency on `emu41gcc`/the ROM/the Arduino bridge), reproduces
  the same blank-screen result in isolation — ruling out any
  interaction with the rest of the firmware stack.

**What's still unknown:** whether the actual analog voltage amplitude
reaching the LCD crosses the ST7920's real `VIH` threshold (0.7×VDD ≈
3.5V at 5V VDD) — a digital logic analyzer only confirms "past some
threshold," not the real voltage margin — and whether `VDD` is
genuinely present/stable at the LCD. Neither has been directly measured
with a multimeter or scope. **If this link is revisited**, measure
actual voltage levels first rather than re-litigating the
protocol/timing/wiring, all of which is already proven correct as far as
software alone can show. An actively-driven level shifter (see
"Hardware" above) would also settle the question directly.

`lcd_bringup/`'s serial-mode commands (including `p`=toggle CS polarity
live, which doesn't apply to parallel mode at all — there's no `/CS`
pin) are gone now that the project has been repurposed for parallel
bring-up (see "Direct Pico→LCD parallel link" above for its current
command set) - recover the serial version from git history if this
investigation is ever resumed.

## Firmware — `firmware/`: Pico SDK project

- **`CMakeLists.txt`** — targets `PICO_BOARD=pico2` (RP2350).
  `PICO_BOARD` must be set **before** `project()` — the SDK resolves
  board/platform during that call, so setting it after silently falls
  back to `PICO_PLATFORM=rp2040`/board `pico`. USB stdio is enabled
  (`pico_enable_stdio_usb`); UART stdio is disabled (`hardware_uart` is
  still linked and `hp41_arduino_bridge.c` still builds, for the dormant
  Arduino-bridge fallback, but nothing currently calls into it).
- **`pins.h`** — GPIO assignments for both the active direct-drive
  parallel LCD path and the dormant Arduino-bridge path; full wiring
  table in its header comment.
- **`st7920.c`/`.h`** — low-level 8-bit parallel ST7920 driver. No
  busy-flag polling (never reads; `R/W` is fixed low, tied directly to
  GND in hardware rather than driven from a GPIO), fixed delays instead
  (72us normal, 1.6ms after Clear, plus the datasheet power-on timing
  above). The active display path (see "Direct Pico→LCD parallel link"
  above).
- **`bitmaps.c`/`.h`** — three runtime-generated test patterns
  (all-on, checkerboard, 4px border), unused in normal operation but
  available as a hardware-debugging aid.
- **`hp41_arduino_bridge.h`/`.c`** — the dormant fallback display path
  (see "Arduino display bridge" above).
- **`hp41_display_bridge.h`/`.c`**, **`hp41_key_bridge.h`/`.c`**,
  **`hp41_key_hold_bridge.h`/`.c`** — see their sections above.
- **`main.c`** — full system integration. `stdio_init_all()`, then
  `st7920_init()`/`st7920_clear()`, then `nut_boot()`. Main loop per
  iteration: drain pending USB bytes into the key bridge (always, even
  asleep, since a key is what wakes it); if asleep and a key is now
  queued, reset `regPC=0`/`flagKey=0` and wake; otherwise if asleep,
  skip `executeNUT()` entirely; else run `executeNUT(1000)` (single-step
  instead, sustaining the key-hold state, if a hold is active), throttle
  via `sleep_us()`; on `fdsp`, compute the framebuffer, checksum it, and
  push it straight to the LCD via `st7920_draw_frame()` (no pacing
  needed — unlike the old Arduino path, there's no second,
  independently-clocked board's receive/draw timing to coordinate
  with); on `POWOFF`, go to sleep; once/second,
  print a heartbeat (`PC`/`cptinstr`/`lgkeybuf`/`flagKey`/`regK`/`ret`/
  `asleep`) so a genuine hang is distinguishable from normal sleep. Debug
  logging throughout is lightweight (checksum + heartbeat + byte echo) —
  the previous full ASCII-art-per-frame dump is commented out, not
  deleted, if heavier tracing is ever needed again.
- **`emu41gcc_compat/`** — build-time compatibility shims letting
  `emu41gcc/nutcpu.c` and `display.c` (DOS-era C) compile under a modern
  ARM GCC toolchain without changing the vendored source:
  - `mem.h` → `#include <string.h>`; `dos.h` → `#define near`/`far` to
    nothing (both meaningless on ARM).
  - `nut_stubs.h`/`.c` — force-included for `nutcpu.c` only (`-include`
    in CMakeLists), declaring the printer entry points `execp()` calls
    with no header of their own, plus no-op bodies for
    timer/HP-IL/printer functions and their `GLOBAL`-declared storage.
  - `nut_globals.c` — the one place that `#define GLOBAL` to nothing and
    `#include "nutcpu.h"`, instantiating real storage for
    `regA`/`tabpage`/`espaceRAM`/etc. (playing the role of upstream's
    `emu41.c`, without pulling in that whole DOS console app).
  - `-fcommon` (scoped to `nutcpu.c`/`display.c`/`nut_globals.c`):
    `nutcpu.h` declares `tabpage[16]`/`tabbank[16][4]` as plain
    non-`extern` globals relying on old-style common-symbol linker
    merging; GCC has defaulted to `-fno-common` since GCC 10, which
    breaks the link with "multiple definition" without this flag.

Build-verified: firmware links cleanly and produces
`firmware/build/soynut.uf2` (drag onto the Pico 2 in BOOTSEL mode, or
use `picotool` as described above).

### Getting the Pico SDK

`pico-sdk/` is gitignored, not committed — it's the official, versioned
`raspberrypi/pico-sdk` (this project was built and tested against tag
**2.3.0**), ~675MB including its own nested submodules
(tinyusb/cyw43-driver/lwip/mbedtls/btstack), and — like most Pico
projects — meant to live outside any one project's repo rather than be
vendored per-project (`PICO_SDK_PATH` exists exactly for this). Fetch a
matching copy yourself:

```
git clone --branch 2.3.0 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
```

Either place the checkout at `pico-sdk/` next to this repo's other
top-level directories (`firmware/CMakeLists.txt` defaults to
`${CMAKE_CURRENT_LIST_DIR}/../pico-sdk` when `PICO_SDK_PATH` isn't set),
or point `PICO_SDK_PATH` (env var or `-DPICO_SDK_PATH=...`) at wherever
you already keep it — useful if you have other Pico projects sharing
one SDK checkout.

### Toolchain setup (macOS, no sudo)

Neither `arm-none-eabi-gcc` nor `ninja` come preinstalled:

- `ninja`: `brew install ninja` works directly.
- `brew install --cask gcc-arm-embedded` needs interactive `sudo` and
  may not be usable in a sandboxed/non-interactive environment.
- `brew install arm-none-eabi-gcc` (the formula) installs without sudo,
  but doesn't bundle newlib (`libc.a`/`libg.a` missing) — linking fails
  with `cannot find -lc`/`-lg`.
- **Workaround:** extract the cask's already-downloaded `.pkg` payload
  directly with `pkgutil --expand-full <path-to-pkg> <dest>/toolchain/extracted`
  (no sudo needed) — this is the full ARM GNU Toolchain with newlib
  bundled. `toolchain/` is gitignored; extract it fresh per machine.

**To build**, put the extracted toolchain first on `PATH`:

```
cd firmware
export PATH="$(cd .. && pwd)/toolchain/extracted/Payload/bin:$PATH"
cmake -G Ninja -B build
ninja -C build
```

(adjust the `toolchain/extracted/Payload/bin` path if you extracted it
somewhere else). `toolchain/` and `firmware/build/` are gitignored —
neither should be committed.

### Native (host) tests

No ARM toolchain needed — these compile and run with the system `cc`:

```
cc -std=gnu11 -fcommon -Iemu41gcc -Ifirmware/emu41gcc_compat \
   -include firmware/emu41gcc_compat/nut_stubs.h \
   -o tests/build/nut_smoke_test tests/nut_smoke_test.c \
   emu41gcc/nutcpu.c emu41gcc/display.c \
   firmware/emu41gcc_compat/nut_stubs.c \
   firmware/emu41gcc_compat/nut_globals.c \
   firmware/emu41gcc_compat/nut_rom.c \
   roms/rom_images.c
./tests/build/nut_smoke_test
```

`tests/nut_smoke_test.c` boots the ROM and runs `executeNUT()` in a
bounded loop — the real HP-41 ROM executes cleanly (thousands of
instructions, zero invalid opcodes) and reaches `POWOFF` showing
`MEMORY LOST` on a cold start, exactly matching real hardware. The other
native tests (`display_bridge_test.c`, `key_bridge_test.c`,
`key_hold_test.c`, `hold_trace_test.c`) follow the same pattern with
their own additional source files — see each file's own header comment
for its exact build line, or adapt the one above.

## ROM wiring — `firmware/emu41gcc_compat/nut_rom.h`/`.c`

Wires the base HP-41 OS ROM (`roms/rom_images.c`'s `rom_nut0/1/2[4096]`)
into `tabpage[0-2]`/`typmod[0-2]=1`, and sets the cold-start fields
`initcpu()` would otherwise set (`regPC=0`, `regST=0x0800`, `Carry=1` —
the coldstart flag the ROM's self-test checks — and `mode_printer=-1`).
`roms/rom_images.c` is declared `extern` directly in `nut_rom.c`; update
both together if the wired ROM set ever changes.

## Directory map

Everything below is load-bearing for the system (build/run/test some
part of it) except `reference-material/`, a single top-level catch-all
for research/provenance material nothing in the build reads anymore.

```
Arduino NHD14432/ NHD14432_POC/ (original, hardware-validated, untouched
                 snapshot) + NHD14432_DisplayBridge/ (the now-dormant
                 display bridge sketch - see "Arduino display bridge"
                 above)
emu41gcc/        Nut CPU emulation core - git submodule, not vendored
                 files (see "The Nut CPU core" above); requires
                 --recurse-submodules or `git submodule update --init`
firmware/        Pico SDK project — display bring-up + Nut core wired
                 into the build (emu41gcc_compat/ has the compat shims)
lcd_bringup/     Standalone Pico SDK project (own CMakeLists.txt, no
                 dependency on emu41gcc/ROM/Arduino) - isolated LCD
                 bring-up testing, currently holding the parallel-mode
                 driver. See "Direct Pico→LCD parallel link" above -
                 kept for reuse if the wiring/shifter setup changes
                 again.
font-tables/     HP-41 font/segment table: generated tables
                 (hp41_display_tables.c/h) + the three JSON sources
                 gen_display_tables.py reads. Original .ai/.pdf source
                 files live in reference-material/font-tables-source/.
pico-sdk/        Official raspberrypi/pico-sdk checkout (dependency) -
                 gitignored, not in this repo; see "Toolchain setup"
                 below for how to fetch a matching copy
roms/            ROM converter/format tools + roms/README.md's BYO
                 instructions. The actual .ROM files and generated
                 rom_images.c are gitignored, not in this repo - see
                 "ROM images" above
tests/           Native (non-Pico) tests - confirm the ROM boots, the
                 display bridge renders correctly, and the key bridge
                 parses input correctly, no hardware needed
tools/           Native (non-Pico) diagnostic tools - nut_disasm.c (ROM
                 disassembler using emu41gcc's own desas41.c),
                 powoff_trace.c (single-step ROM/wake-cycle tracer), and
                 hp41_keyboard_gui.py (clickable software keyboard)
HP-41CX_..._(removed_background,_colour_adjustment).jpg
                 Keyboard photo (CC BY-SA 3.0, Wikimedia Commons - see
                 tools/hp41_keyboard_gui.py's header for attribution) the
                 software keyboard GUI renders and overlays clickable
                 regions on
reference-material/ Datasheets, source mockups, and other
                 research/provenance material nothing in the build reads
                 anymore - kept for context only:
                   datasheets/ - ST7920.pdf (controller), NHD-14432WG-
                     BTFH-VT.pdf (LCD module), 20171225012850PINOUT-
                     LogLevel.pdf (level shifter) - all cited throughout
                     this doc as the source for specific design
                     decisions, just not machine-read by anything
                   display-mockups/ - the PNG/Pixen mockups the font
                     table and pixel-segment maps were manually derived
                     from
                   font-tables-source/ - HP41C_Character_Display_Table
                     .ai/.pdf, the original design files font-tables/'s
                     JSON was extracted from
                   synth.png - the Museum of HP Calculators' Nut opcode/
                     instruction table (1997), human reference for ROM
                     disassembly work
                   4bun501eqka91.png - a generic Raspberry Pi Pico (not
                     Pico 2/RP2350) pinout reference chart
                   SystemBlockDiagram.txt - an early ASCII architecture
                     diagram, superseded by this doc
toolchain/       Extracted ARM GNU Toolchain (gitignored, see above)
.gitmodules      Declares emu41gcc/ as a submodule (see above)
LICENSE          GPL-2.0-or-later (see "License" note above)
CLAUDE.md        This file
DEVLOG.md        Session-by-session development history (gitignored,
                 local-only - see the note at the top of this file)
```

## Known unknowns / next steps

- **Direct Pico→LCD serial link**: protocol/timing fully verified, still
  never lit up the display — superseded by the working direct *parallel*
  link, so no longer blocking anything; low priority to revisit. See
  that section above for the multimeter/scope check that was never done,
  and the note on what the parallel link's success implies about the
  level shifter not being the likely culprit.
- **Auto power-off after every keypress**: confirmed real/intentional
  ROM behavior (matches real HP-41 timeout logic, just scaled to trigger
  sooner in this environment) — not a bug. The separate "screen goes
  blank" symptom this was tangled up with is resolved (see "Arduino
  display bridge" above).
- **Font table codes 0-31 and 102-125** (most of lowercase `f`-`z`) need
  proper re-extraction if ever needed; both currently render blank
  rather than garbled.
- **Press-and-hold**: the underlying mechanism and wire protocol are
  confirmed working against the real ROM (host trace + real hardware),
  including two responsiveness fixes (batching overhead, and a
  150ms GUI-side tap/hold threshold to absorb round-trip latency). Not
  yet independently reconfirmed on hardware that quick taps never
  flash the hold-label in practice — worth a fresh check.
- **`firmware/pins.h`'s active (parallel) GPIO assignments** are now
  confirmed working against real hardware (see "Direct Pico→LCD parallel
  link" above). The dormant Arduino-bridge pins (`PIN_ARDUINO_UART_TX/RX`)
  are unverified in the *current* physical wiring, since GP0/GP1 are now
  doing RS/E duty for the direct link instead — reconnecting that
  fallback would need rewiring, not just a firmware swap.
- **Punctuation pixel mapping** (dot_top/dot_bottom/comma_tail) hasn't
  been cross-checked against a rendered real colon/period/comma the way
  the printable characters were.
- `main.c` carries lightweight `TEMPORARY`-marked debug instrumentation
  (heartbeat, byte/keybuffer echo, per-frame checksum) — safe to leave,
  optionally strippable later.
