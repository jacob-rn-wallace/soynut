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
timeout logic does. Continuous memory (see "Continuous memory" below) is
also confirmed: calculator state now survives a reset/power cycle via
flash, the same way the real HP-41's battery-backed CMOS RAM would —
see "Known unknowns" for what's still open.

The Arduino Uno display bridge (the *previous* active display path,
before the parallel level-shifter setup above replaced it) is kept fully
intact but now dormant — see "Arduino display bridge" below. The
separate direct Pico→LCD *serial* (3-wire) link remains parked, never
having lit the display — see "Direct Pico→LCD serial link" below for
that history and a note on what the parallel link's success implies
about it.

## Coding standard: NASA/JPL "Power of 10"

This project's own original source complies with NASA/JPL's "Power of
10" rules for safety-critical code, as closely as this project's
realities allow. The goal is long-term legibility as the codebase
grows, not certification — these are strong defaults, not absolute law;
where a rule is genuinely impractical here, the exception is documented
explicitly rather than silently ignored or faked.

**Status: applied repo-wide, not just a forward-looking policy.** The
policy was adopted in commit `8ab4107`; every in-scope file existing at
that point was then actually rewritten to comply (tests/, firmware/
including `emu41gcc_compat/` and every bridge file, `lcd_bringup/`,
tools/, `NHD14432_DisplayBridge.ino`, and every in-scope Python script),
verified by a full clean rebuild of every target (native tests, the ARM
firmware, `lcd_bringup`, the native `tools/` binaries, and the Arduino
sketch) plus a zero-warning `ruff`/`mypy` pass, all still true as of the
most recent rewrite. New code should keep meeting this bar going
forward — see `DEVIATIONS.md` for the full, authoritative list of
specific exceptions (which rule, exactly what's excepted, why, and the
boundary of the exception) rather than re-deriving them from the
summary below, which only sketches enough to orient new code.

**Reference (pinned to the exact revision this policy is based on):**
- C: https://github.com/Vhivi/Powerof10-NASA/blob/29f6f3975bba9c6d6430f8638d8d561786b04c26/rules/Powerof10-C.md
- Python: https://github.com/Vhivi/Powerof10-NASA/blob/29f6f3975bba9c6d6430f8638d8d561786b04c26/rules/Powerof10-Python.md

**Scope — applies to:** this project's own original C (`firmware/*.c/.h`
except `emu41gcc_compat/` interop shims where noted below,
`lcd_bringup/*.c/.h`, `tests/*.c`, `tools/*.c`, `sim/*.c/.h`) and Python
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
- A handful of specific pre-existing functions the rewrite deliberately
  left structurally alone rather than splitting apart purely for line
  count (`main()`'s loop body, `hp41_key_bridge_feed_byte()`'s state
  machine, the Arduino sketch's `pollPicoLink()`) — see Rule 4 below and
  `DEVIATIONS.md`.

**The 10 rules, applied to this codebase specifically:**

1. **Simple control flow — no `goto`/`setjmp`/`longjmp`, no recursion.**
   Already the de facto style here (state machines like `dokey()`-driving
   code and the key/hold bridges are loop-based, not recursive). Keep it
   that way — e.g. a future GDRAM/font-table walk should be an explicit
   loop, not recursive descent.
2. **Every loop needs a provably fixed upper bound.** Most loops here
   already iterate over fixed-size hardware constants (`LCD_HEIGHT_PX`,
   `LCD_BYTES_PER_ROW`, a `keybuffer[8]` cap) — good fit, keep bounding
   new loops the same way. `main.c`'s USB-byte-drain loop
   (`drain_usb_bytes()`) used to terminate only because the USB FIFO
   itself is finite, not because the loop counted anything — it now has
   an explicit `MAX_BYTES_PER_DRAIN` iteration cap; follow that same
   pattern for any new "drain until empty" loop rather than relying on
   an external buffer's size as an implicit bound. The two genuinely
   unbounded loops left (the top-level `while (true)` and the
   halt-on-fatal-error loop) are documented exceptions — see
   `DEVIATIONS.md`.
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
   SDK/newlib) for pre/postconditions and invariants in new C code —
   **but see the `-UNDEBUG` note below first**, since the Pico SDK's
   default Release build type silently compiles every `assert()` to
   nothing otherwise. For Python, use the small `check(condition,
   message)` helper defined locally in each tooling script, not bare
   `assert` — Python's `assert` is stripped under `-O`/`-OO`, which
   would silently disable every check; see `DEVIATIONS.md`'s
   implementation note. Trivial one-line setter/getter/wrapper
   functions (e.g. `write_cmd()`, `hp41_key_bridge_reset()`) are a
   deliberate exception to the ≥2 rule — see those functions' own
   comments for why an assertion there would just restate the line
   above it. "Explicit recovery action" on this project's bare-metal
   firmware realistically means what `main.c` already does for invalid
   opcodes: report clearly over the debug UART, then halt in a tight
   loop rather than silently continuing into undefined behavior —
   there's no OS/exception handler to hand an error to. Follow that
   existing pattern for new hard-invariant failures rather than
   inventing a new one.

   **`-UNDEBUG` note:** `firmware/CMakeLists.txt` and
   `lcd_bringup/CMakeLists.txt` both append `-UNDEBUG` when compiling
   this project's own sources, specifically to counteract the Pico
   SDK's default `-DNDEBUG` (Release build type) and keep `assert()`
   genuinely active on real hardware. If you add a new CMake target
   for Pico-side code, it needs this too, or every assertion in it is a
   silent no-op — see `DEVIATIONS.md` for how this was actually caught
   (an ARM-only `-Werror=unused-variable` from a variable an `assert()`
   would otherwise have read).
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
    static analysis.** Wired in and enforced, zero warnings on this
    project's own sources as of the most recent rewrite:
    - `firmware/CMakeLists.txt` / `lcd_bringup/CMakeLists.txt`:
      `-Wall -Wextra -Wpedantic -Werror` via `set_property(SOURCE
      ${SOYNUT_OWN_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ...)` —
      `APPEND`/`set_property` rather than `target_compile_options()` or
      `set_source_files_properties()`, both of which either leak onto
      vendored Pico SDK sources compiled into the same target (confirmed:
      fails on `pico-sdk`'s own `bootrom.c`) or clobber the `-fcommon`
      flags already set on the same files.
    - `tests/Makefile` / `tools/Makefile`: same flags, applied per-object
      so this project's own sources get them and vendored
      `emu41gcc`/generated-table sources don't.
    - Python: `ruff check .` (config in `pyproject.toml`, `select =
      ["ALL"]` plus a small justified `ignore` list) and `mypy .`
      (`requirements-dev.txt` has both tools); both exclude
      `Arduino NHD14432/NHD14432_POC/` (untouched snapshot, see "Does NOT
      apply to" above) and the external `pico-sdk/`/`toolchain/`
      directories.
    - The Arduino sketch: confirmed clean via `arduino-cli compile
      --build-property compiler.warning_level=all`, though nothing
      currently enforces this automatically on every build the way the
      CMake/Makefile targets do.

## Commenting standard

This project's own code is fully commented, to the same standard/scope
as the Power of 10 pass above — **applied repo-wide, not just a
forward-looking policy**: every in-scope file's functions/classes got a
doc-comment header in the pass that added this section, verified by a
full clean rebuild of every target (native tests, the ARM firmware,
`lcd_bringup`, the native `tools/` binaries, and the Arduino sketch)
plus a zero-warning `ruff`/`mypy` pass. New code should keep meeting
this bar going forward — a function/file without a doc comment is
incomplete, the same way an uncompiled warning would be.

**Scope:** identical to the Power of 10 section's "Scope"/"Does NOT
apply to" lists above — this project's own original C/Arduino-sketch
code and Python tooling, not `emu41gcc/`, `pico-sdk/`, or
`NHD14432_POC/`.

**C, and the Arduino sketch (project-authored, C-like — same files the
Power of 10 section holds to C rules):** Doxygen-style `/** ... */`.
- Every file gets a `@file`/`@brief` header block at the top.
- Every function — including `static` helpers — gets a `/** @brief ...
  */` block: a one-line summary, then `@param`/`@return` for anything
  non-`void`/non-trivial. A genuinely trivial one-line wrapper (e.g.
  `write_cmd()` in `st7920.c`) can skip `@param`/`@return` if the
  summary line already says it all.
- A `struct`/`typedef` or a `#define` whose name doesn't already explain
  the "why" gets its own `/** ... */` (or `/** @name ... @{ ... @} */`
  for a related group of macros).
- Inline `//`/`/* */` comments still cover non-obvious logic *inside* a
  function body (bit-packing, timing-sensitive hardware sequences,
  state machines) exactly as the top-level system prompt's default
  commenting guidance describes — the Doxygen header is what's new
  here, not a replacement for that.
- Not currently enforced by a build-time tool (no `doxygen`/coverage
  check wired into CMake or the Makefiles) — same "confirmed clean but
  not automated" caveat as the Arduino warning-level check just above.
  Holding the line here is a code-review/self-review responsibility,
  not a compiler gate.

**Python (`tools/*.py`, `font-tables/gen_display_tables.py`,
`roms/*.py`):** Google-style docstrings, PEP 257-compliant.
- Every module gets a top-of-file docstring (already the norm here).
- Every function/method gets a docstring: a one-line summary (its own
  line, blank line, then any extended description — pydocstyle's D205
  enforces this split), plus `Args:`/`Returns:`/`Raises:` sections for
  anything not self-evident from the signature alone. A
  `TypedDict`/dataclass-like structure gets an `Attributes:` section.
- **This one *is* enforced by tooling**, not just convention:
  `pyproject.toml`'s `[tool.ruff.lint]` `select = ["ALL"]` no longer
  ignores pydocstyle's `"D"` codes, and `[tool.ruff.lint.pydocstyle]`
  pins `convention = "google"` — `ruff check .` fails the same way a
  missing/malformed docstring would fail any other Rule 10 lint check.

**Generated files are the one exception — document the *generator*,
not its output.** `font-tables/hp41_display_tables.c`, `Arduino
NHD14432/NHD14432_DisplayBridge/hp41_display_tables_avr.h`, `Arduino
NHD14432/NHD14432_DisplayBridge/bitmaps.h`, and the gitignored
`roms/rom_images.c` are all regenerated wholesale by their respective
scripts (`font-tables/gen_display_tables.py`,
`Arduino NHD14432/NHD14432_DisplayBridge/convert_images.py`,
`roms/rom_to_c.py`) — hand-added comments there would just be silently
discarded next run. Each already carries an "Auto-generated — do not
hand-edit" header from its generator; that's sufficient. Put real
documentation in the generator script itself, and in the
hand-maintained header that declares the generated arrays (e.g.
`font-tables/hp41_display_tables.h`, which is *not* generated — it's
hand-written and does get the full Doxygen treatment) instead.

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
silently ignored. **`ON` is also rejected outright** (`resolve_hold_code()`
special-cases it before either lookup) — found via real-hardware
debugging of a reported bug ("the GUI's ON key doesn't seem to power
off"): a GUI click held past `HOLD_ENGAGE_MS` (150ms, easy to exceed
with an ordinary mouse click) engaged this same hold protocol for `ON`,
and since `ON` is a power toggle rather than a USER-mode-assignable
function key, it was never exercised against the sustained
resustain-every-instruction mechanism the way `SIGMA+`/`A` was in
`tests/hold_trace_test.c` — on real hardware this drove the ROM into an
unbounded spin, **106,000+ instructions** before the release finally
registered and toggled power, a multi-second stall easily mistaken for
"doesn't work" rather than "very slow." Fixed at both layers: the
firmware rejects `"[+ON]"` as a no-op (confirmed on real hardware:
`lgkeybuf`/`cptinstr` now stay completely flat across a held-then-released
`ON`, zero spin), and `tools/hp41_keyboard_gui.py`'s `ALWAYS_TAP_KEYS`
set makes the GUI send `ON` as a plain instant tap regardless of the
active `PressMode`, so it never even attempts the now-rejected path.
Plain-tap `ON` (unaffected by this fix, since `handle_named_key()` was
never touched) was independently reconfirmed still correctly toggles
power off on real hardware.

Verified via `tests/key_hold_test.c` (13 checks, simulating the ROM's
own repeated `flagKB`/`regK`-clearing, including the `"[+ON]"`
rejection and a follow-up `"[-]"` no-op) and `tests/hold_trace_test.c`
(boots the real ROM, holds a real function key via the wire protocol,
single-stepping like `main.c`) — a short tap never nullifies; an
unreleased hold drives the ROM into the nullify branch at exactly the
expected instruction count.

## Display blanking on POWOFF

`POWOFF` fires after essentially every keystroke (see "Firmware" below)
— it's the real Nut CPU's power-saving CPU halt between keystrokes, not
"the user turned the calculator off." On real HP-41 hardware the
direct-drive segment display stays lit through this halt (the segments
are held by the ROM's own display-on state, not by a running CPU); this
project's ST7920 panel needs the equivalent behavior recreated
explicitly, since it's a graphic controller with its own persistent
GDRAM (`st7920_draw_frame()` is only called on `fdsp`, so without doing
anything at `POWOFF` the glass would just keep showing whatever was
last drawn indefinitely, which happens to be correct for this frequent
between-keystroke case, but wrong for a genuine "display should be
off" case).

**The distinguishing signal:** `emu41gcc/nutcpu.c`'s `POWOFF` opcode
dispatch sets `Carry=(dspon==0)` — `dspon` is the ROM's own explicit
display-on flag, toggled by the separate `DISOFF`/`DISTOG` opcodes,
completely independent of the `POWOFF` halt itself. `firmware/main.c`'s
`ret==1` handling checks `dspon` directly (the same signal Carry
momentarily reflects at that exact instant, made explicit rather than
relying on that side effect) and only calls `st7920_clear()` when
`dspon==0`.

**Found as a real regression, not designed in from the start:** an
earlier version of this fix cleared the LCD unconditionally on every
`POWOFF`, which is wrong given how often `POWOFF` fires — confirmed on
real hardware as a serious regression (the screen went blank within a
fraction of a second of drawing anything, since the very next keystroke
almost immediately re-triggers `POWOFF`). The `dspon`-conditional
version was verified via the serial log: entering a multi-digit value
now produces a `POWOFF (Carry=0)` for each digit with **no** "LCD
cleared" line following it (display persists, matching real hardware),
while a lone `ON` tap with nothing else queued (`dspon` never turned
back on) correctly still clears.

## Continuous memory — `firmware/hp41_persist_state.h`/`.c`, `firmware/hp41_persist_flash.h`/`.c`

The real HP-41 keeps its RAM alive across power-off via battery-backed
CMOS ("continuous memory") — pulling the batteries is what produces
`MEMORY LOST`. This project has no battery, but the Pico's own on-die
QSPI flash holds its contents with zero power, so it stands in for that
CMOS: the calculator's RAM/CPU state survives unplugging the Pico
entirely, and only shows `MEMORY LOST` on a genuinely first-ever boot,
an invalid/corrupted snapshot, or an explicit `[CLRMEM]` request (below).

Split into the same pure-logic/hardware pattern as the display bridge
(`hp41_display_compute_framebuffer()` vs `st7920_draw_frame()`):

- **`hp41_persist_state.h`/`.c`** (pure, no pico-sdk dependency, host-
  testable): `hp41_persist_state_t` is a flat, versioned/checksummed
  struct holding the subset of `emu41gcc`'s globals that constitute real
  HP-41 "memory" — `espaceRAM[8200]`, `regA-N[14]`, `regST`, `regPQ`,
  `regG`, `Carry`, `regK`, `regFO`, `regFI`, `regPT`, `flagdec`,
  `regData`, `regPer`, and the printer flags (`mode_printer`,
  `flagPrter`, `flagPrx`, `flagAdv`). `hp41_persist_capture()`/
  `hp41_persist_apply()` snapshot/restore those globals;
  `hp41_persist_validate()` checks a magic number + version + FNV-1a
  checksum, safely rejecting erased flash (reads back as all `0xFF`) or
  a struct saved by a different firmware version's layout.
  Deliberately **excludes** key-scan/execution/render bookkeeping
  (`regPC`, `flagKey`, `flagKB`, `cptKey`, `keybuffer[]`/`lgkeybuf`,
  `smartp`, `breakcode`, `selper`, `cptinstr`, `fjmp`, `dspon`,
  `facces_dsp`, `fdsp`) — restoring those verbatim risks resurrecting a
  stuck mid-instruction/mid-keypress state, and they already reset
  correctly on their own via the wake path below.
- **`hp41_persist_flash.h`/`.c`** (ARM/pico-sdk only, never linked into
  the host-native test build): reserves the last 3 flash sectors
  (12 KiB, at `PICO_FLASH_SIZE_BYTES - 3*FLASH_SECTOR_SIZE`) for the
  snapshot — comfortably clear of the firmware image itself (~150 KiB,
  placed from the start of a 4 MiB chip). `hp41_persist_flash_load()`
  reads directly from the XIP-mapped flash address (no SDK call needed
  for reads) and validates it. `hp41_persist_flash_save()` first
  compares against what's already on flash and skips the erase/program
  cycle entirely if nothing changed. `hp41_persist_flash_erase()` wipes
  the region outright.

**Wiring in `firmware/main.c`:** deliberately reuses the exact same,
already-hardware-validated `POWOFF`→wake transition the emulator already
had, rather than inventing a new boot path. At boot, right after
`nut_boot()`'s unconditional ROM-wiring/cold-start defaults,
`hp41_persist_flash_load()` + `hp41_persist_apply()` restore a valid
snapshot if one exists and set `asleep = true` — the existing
`flagKey=0`/`regPC=0` wake block (see "Key bridge" above) then takes
over on the very next keypress exactly as it already does for a same-
session `POWOFF`→wake, with zero duplicated reset logic. This also
matches real continuous-memory HP-41 power-on semantics: it doesn't
unprompted redraw/run, it waits for you to press ON.

**The save itself is deferred/debounced, not synchronous with
`POWOFF`.** An earlier version called `hp41_persist_capture()` +
`hp41_persist_flash_save()` directly inside the `POWOFF` handling, on
the reasoning that since `POWOFF` already fires after essentially every
keypress, saving there would give near-continuous persistence "for
free." **Confirmed as a real hardware regression, not theoretical:**
`hp41_persist_state_t` includes `Carry` and `regK`, both of which change
on almost every keystroke, so the change-detection guard in
`hp41_persist_flash_save()` almost never actually skipped the write —
meaning nearly every keypress triggered a real
`flash_range_erase()`+`flash_range_program()` cycle (12 KiB, interrupts
disabled, ~100ms+) *before* the main loop could get back around to
draining the next USB byte. The result was exactly what got reported:
noticeably higher latency between a keypress and the system responding,
where before this feature existed the round trip had been essentially
instant. Fixed by decoupling capture from save: `POWOFF` still calls the
cheap, pure `hp41_persist_capture()` immediately (into a `main()`-local
static `pending_snapshot`) and sets a `persist_dirty` flag, but the
actual `hp41_persist_flash_save()` call is deferred to the main loop's
`asleep`-and-idle branch, firing at most once, only after the system has
sat idle (asleep, no new key queued) for `PERSIST_SAVE_DELAY_MS` (1.5s)
with no further keypress resetting that timer. Since a real typing
cadence keeps re-triggering `POWOFF` (and thus resetting the idle timer)
faster than 1.5s apart, the blocking flash write never happens while
actively interacting with the calculator — only once activity actually
stops. This does widen the "honestly-documented gap" from "between
keypresses" to "up to ~1.5s after the last keypress" — a literal power
yank inside that window loses the in-flight session — but is a
deliberate, worthwhile trade for restoring normal interactive
responsiveness. `[CLRMEM]`'s handling also clears any pending
`persist_dirty` flag, so a stale pre-erase snapshot can never get
written back over a fresh erase by the deferred flush firing late.
**Verified on real hardware:** with the fix in place, serial-log
timestamps of incoming keypress bytes track the sender's actual send
cadence with no added delay, and exactly one
`"idle - flushing deferred continuous-memory save"` line appears,
~1.5s after the last keypress in a burst — confirming both that the
per-keystroke stall is gone and that the deferred write still happens
reliably once idle.

**`[CLRMEM]`** (`firmware/hp41_key_bridge.c`, a bridge-level command, not
a real HP-41 key — deliberately handled in `main.c`, not inside the key
bridge itself, which stays pure/host-testable and has no business
touching flash or CPU state): erases the persisted snapshot
(`hp41_persist_flash_erase()`), re-runs `nut_boot()`'s cold-start reset
plus an explicit `espaceRAM` clear (since `nut_boot()` itself never
touches `espaceRAM` — see "ROM wiring" above), and drops out of
`asleep` so the ROM's own cold-start code runs immediately — the
deliberate way to get `MEMORY LOST` back without reflashing firmware.

Verified via `tests/persist_state_test.c` (13 checks: a full
capture/wipe/apply round trip across every persisted field group, plus
`hp41_persist_validate()` rejecting an all-zero struct, erased-flash
content, a flipped checksum bit, and a version mismatch, each checked
against a genuine positively-accepted snapshot). The flash-touching half
(`hp41_persist_flash.c`) has no host-native equivalent — same as
`st7920_draw_frame()`, real confirmation needs real hardware, and this
has been done: a value entered and committed with `ENTER` survived a
genuine reset and reappeared with the exact same rendered-display
checksum, confirmed across two independent reset cycles, and
`[CLRMEM]`'s erasure was confirmed to persist too (not just the live
state) — see "Known unknowns" for the full results and one caveat found
along the way (reflashing the firmware, as opposed to a plain reset or
power cycle, currently wipes the persisted region).

## Elite User Mode — `firmware/hp41_elite_display_bridge.h`/`.c` (currently deactivated)

**Status: built, reached real hardware, found real bugs there
(the ALPHA annunciator getting stuck lit with no way to clear it, and
the elite grid always showing all zeros no matter what was actually on
the stack), and deactivated rather than debugged further for now** —
the user's own call, not a decision to reopen without being asked. Kept
fully intact, not deleted, matching this project's established pattern
for dormant features (see "Arduino display bridge" and "Direct
Pico→LCD serial link" below for the same treatment elsewhere in this
codebase). Deactivated at a single, well-contained point:
`firmware/hp41_key_bridge.c`'s `elite_mode_feature_enabled` static
defaults to `false`, checked first thing inside `push_key_tracked()` —
while it's off, every keycode passes through completely unmodified and
the trigger sequence is never even looked at, so the feature is a true
no-op in production, not just visually hidden. `tests/key_bridge_test.c`
calls `hp41_key_bridge_set_elite_mode_feature_enabled(true)` so the
underlying trigger logic itself keeps being exercised and verified even
while production stays off — re-enabling for real hardware use is a
one-line flip of that static's initializer once the display bugs are
actually diagnosed, not a rewrite. **Nothing about the two known bugs
has been investigated yet** — they're recorded here exactly as
reported, not analyzed, since debugging was explicitly deferred.

An Easter egg, not a real HP-41 feature: typing the real key sequence
`XEQ`, `ALPHA`, `L`, `E`, `E`, `T`, `ALPHA` — exactly how you'd invoke a
real global-label program — toggles the display from its normal single
line of 12 characters into 4 lines of 24 tiny (3×5 pixel) characters,
showing the T/Z/Y/X stack registers top-to-bottom, each as a fully
formatted signed decimal number with exponent. Typing the same sequence
again returns to the normal view. While active, pressing `ALPHA` alone
swaps the bottom row (normally X) for the most recently typed
ALPHA-mode text instead; pressing it again swaps back. Elite Mode's
on/off state is ephemeral — a plain `main()`-local, not part of
`hp41_persist_state_t` — so it always resets to the normal view on any
firmware reset or power-cycle, never persisted.

**Register access — confirmed empirically, not from documentation.**
`emu41gcc` has no C-level concept of "the stack" or "the ALPHA
register" at all — it's a raw Nut CPU microcode emulator; all HP-41
semantics above the bare instruction set are ROM code manipulating
generic RAM registers via `storeData()`/`recallData()`. Two facts this
project needed were dug out by reading `nutcpu.c` directly and, for the
second, by building a throwaway diagnostic (same technique as
`tools/powoff_trace.c`) that boots the real ROM, drives real key
sequences through it, and diffs `espaceRAM` before/after each keystroke:

- **T/Z/Y/X** live at fixed `espaceRAM` register indices 0/1/2/3 (`M`
  onwards are OS scratch registers, confirmed via `monit.c`'s debug
  register-naming string `"TZYXLMNOPQ-abcde"` — a naming coincidence
  with `nutcpu.h`'s unrelated `regM`/`regN` CPU scratch registers, not a
  connection). Each register is 8 bytes: byte 0 is a write-protect flag
  (always 0 in practice for the stack), bytes 1-7 pack the same 14-nibble
  BCD format the CPU already uses for arithmetic (confirmed from
  `recallData()`'s unpack loop and `exec2()`'s field-selector table):
  nibble 0 = exponent sign (0=positive/9=negative), nibbles 1-2 =
  exponent digits (nibble 1 = units, nibble 2 = tens), nibbles 3-12 =
  10 mantissa digits (nibble 3 = least significant/last-typed digit,
  nibble 12 = most significant/leading digit), nibble 13 = mantissa
  sign. No formatter for this exists anywhere in `emu41gcc` — the
  decode in `hp41_elite_decode_register()` is new code.
- **ALPHA-mode text entry** has no dedicated register either. Empirical
  finding: `espaceRAM` register 5 reliably holds the most-recently-typed
  ~7 characters as plain 8-bit ASCII (not the sparse 10-bit LCD code —
  confirmed directly, e.g. typing `'A'` writes literal byte `0x41`),
  most-recent-character-first, shifting by one byte per keystroke.
  **Deliberately not chased further**: past ~24 typed characters, the
  registers that initially held characters 8-24 (6, 7, 8) stop updating
  and only register 5 keeps rotating — the real underlying mechanism is
  more complex than a simple 4-register 24-byte ring buffer, and fully
  reverse-engineering it would need real ROM single-step tracing beyond
  what this feature's scope justified. So the ALPHA row only ever shows
  the most recent ~7 characters, not the full 24-character register —
  an accepted, documented limitation, not a bug to fix later without
  new information.

**Grid layout — pixel-exact, derived from the user's own mockup**
(`reference-material/display-mockups/NHD14432_Elite_User_Mockup_Colored.png`,
analyzed programmatically, not eyeballed): 24 columns × 4 rows of 3px-wide
× 5px-tall character cells, 6px pitch (`x = 1 + 6·col`, `y = 1 + 6·row`),
exactly filling the 144px width. A single punctuation pixel per column
sits in the gap after it (`x = 5 + 6·col`), at three possible y-offsets
matching normal mode's `dot_top`/`comma`/`dot_bottom` marks, scaled down
to one pixel each. Punctuation semantics for a *number* (numeric rows
only — see column layout below) are a deliberate mapping, not
self-evident from the mockup, which only showed solid placeholder boxes:
`dot_bottom` (after column 1) is always the decimal point — fixed
position, since numbers are always rendered `D.DDDDDDDDD`; `comma`
(after column 10) is always the mantissa/exponent separator; `dot_top`
is deliberately unused/reserved (using it for anything would create the
same off-by-one ambiguity a sign glyph in a punctuation dot would).
Column assignment for a numeric row: col 0 = mantissa sign (`-` or
blank), cols 1-10 = the 10 mantissa digits, col 11 = exponent sign,
cols 12-13 = the 2 exponent digits, cols 14-23 unused. The annunciator
row reuses the exact same 12-annunciator table normal mode uses
(`hp41_annunciator_bits`/`_pixels`/`_offset`/`_count`, from
`font-tables/hp41_display_tables.h`) with a fixed **+5 pixel y-offset**
— confirmed by measuring the mockup's annunciator markers against the
existing `hp41_annunciator_pixel_map.json` coordinates: identical
x-ranges for all 12, uniformly shifted down. No new annunciator table
needed.

**Font — hand-authored, not extracted.** The mockup showed cell
*positions* only (solid `#C1C1C1` placeholder boxes), not real glyph
shapes, so `font-tables/hp41_elite_font_table.json` (a per-code 3×5
bitmap, `{"code": ["row0",...,"row4"]}`, each row 3 chars of `'0'`/`'1'`)
plus its generator `font-tables/gen_elite_font_table.py` (deliberately a
separate script from `gen_display_tables.py`, not a mode of it — a
bitmap font doesn't fit that script's named-segment pipeline at all) is
new, hand-designed artwork. Covers all 80 HP-41 display codes that
already render *something* in the normal 14-segment font (0, 1, 4, 5, 6,
12, 13, 29, 32-101, 126, 127 — checked directly against
`hp41_font_table.json`, not re-derived from CLAUDE.md's "Font" section's
summary, which doesn't fully match: several codes below 32 do have real
segment data, just no identified real-world meaning, marked `'?'` in
`hp41_font_table.txt`). Full legibility for all 80 at 3px width is not
achievable — some letters (e.g. `M`/`N`/`W`) are best-effort
approximations, an accepted tradeoff of the resolution, not an
oversight. The ALPHA row's content is always plain ASCII already (per
the register-access finding above), so it indexes straight into this
table with no decode step.

**Trigger mechanism — orthogonal to, and non-disruptive of, normal key
handling.** `firmware/hp41_key_bridge.c`'s `push_key_tracked()` — the
single choke point every real keypress goes through, whether it arrived
as a plain ASCII byte (`tabcode[]` lookup) or a resolved `"[NAME]"`
bracket escape — watches for the matching *keycode* sequence (`0x32`
XEQ, `0xc4` ALPHA, `0x72` L, `0xc0` E, `0xc0` E, `0x84` T, `0xc4` ALPHA)
via a small progress counter. Every keycode of the sequence is still
pushed to `keybuffer[]` normally *except* the final ALPHA, swallowed
only once the full sequence actually completes — so a real
`XEQ ALPHA <other name> ALPHA` sequence is completely unaffected, and no
buffering/replay of already-sent keystrokes is ever needed. `"[LEET]"`
is also accepted as a bracket-escape alias (mirroring `"[CLRMEM]"`'s
precedent) for testing/tooling convenience. While active, a *bare*
ALPHA press (not part of a completing trigger sequence) is separately
intercepted and swallowed to toggle the alpha row instead —
`hp41_key_bridge_set_elite_mode_active()` tells the bridge whether this
interception should be active, since Elite Mode's own on/off state is
owned by `main.c`, not the key bridge. **Known, accepted caveat**: since
the closing ALPHA is swallowed, the ROM is left mid-alpha-entry (with
"LEET" in its own alpha buffer) after every toggle — confirmed
non-fatal but not deeply chased further (e.g. whether re-triggering to
toggle off cleanly restarts the ROM's alpha buffer or compounds onto it
is unconfirmed) — a real, documented tradeoff, not a bug.

**Real bug found and fixed on first real-hardware use, the same session
this feature was built:** the first version tracked the trigger at the
raw-byte level (watching `hp41_key_bridge_feed_byte()`'s incoming bytes
directly, before bracket resolution) — every native test passed, since
they fed the raw ctrl-X/ctrl-A bytes directly, but on real hardware the
sequence **never fired at all**, always falling through to the ROM as a
genuine (and nonexistent) program name, showing `NONEXISTENT`. Root
cause: `tools/hp41_keyboard_gui.py`'s XEQ and ALPHA buttons send
`"[XEQ]"`/`"[ALPHA]"` bracket escapes, not the raw control bytes — those
are consumed entirely by the `"[NAME]"` bracket state machine and
resolved via `handle_named_key()`, which called the old unconditional
`push_key()` directly, never touching the byte-level tracker at all. No
amount of native testing caught this because the tests only exercised
raw-byte input, never the bracket-escape path a real user (via the GUI)
actually uses. Fixed by moving detection down to `push_key_tracked()`
itself — the one point both the raw-byte and bracket-escape paths
converge on before anything reaches `keybuffer[]` — which fixes both
uniformly and, as a bonus, correctly handles a sequence typed partly one
way and partly the other. `tests/key_bridge_test.c` gained a direct
regression test feeding the exact bracket-escaped form
(`"[XEQ][ALPHA]LEET[ALPHA]"`) to make sure this specific gap can't
reopen silently. A reminder worth keeping in mind for future features
in this file: a host test that only feeds raw bytes doesn't prove a
feature works for every real input path — the bracket-escape route is
just as "real" as raw bytes here, since it's literally how the primary
GUI tool sends most named keys.

**`main.c` integration**: `elite_mode_active`/`alpha_row_active` are
plain locals, polled once per loop iteration alongside the existing
`"[CLRMEM]"` check, branching the existing `if (fdsp)` render block
between `hp41_display_compute_framebuffer()` and the two new elite
renderers. Toggling either flag sets a same-iteration `redraw_needed`
flag to force an immediate render rather than waiting for the next
`fdsp` — this needed a real fix, not just an addition: since the
sequence's closing ALPHA is swallowed (never reaches `keybuffer[]`), the
system is very likely already asleep with no key queued at the exact
moment the toggle fires (each of the sequence's earlier real keypresses
plausibly already triggered its own wake→process→`POWOFF`→asleep cycle,
per this project's well-documented "auto power off after every
keystroke" behavior) — the pre-existing `if (asleep) { ... continue; }`
idle path had to be taught to fall through to the render block on
`redraw_needed` instead of skipping it, while still correctly leaving
`executeNUT()` un-called (the CPU must stay genuinely halted).

**Testing**: `tests/elite_display_bridge_test.c` (new, exact-pixel-count
style like `tests/display_bridge_test.c`, but doesn't need to boot the
ROM at all — `espaceRAM` is poked directly with hand-computed nibble
patterns, verified against a standalone Python re-implementation of the
same unpack logic before being hardcoded) covers register decode, the
4-row grid's exact lit-pixel counts (including a sum-of-parts check
across all 4 rows), the annunciator +5 y-offset re-derived from the
existing table, and the alpha row. `tests/key_bridge_test.c` gained
checks for the trigger sequence, its case-insensitivity, near-miss/
broken-sequence non-interference with real key sequences, the
`"[LEET]"` alias, and the bare-ALPHA sub-toggle.

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
  Arduino-bridge fallback, but nothing currently calls into it). Also
  applies Power of 10 Rule 10 (`-Wall -Wextra -Wpedantic -Werror`,
  scoped to `SOYNUT_OWN_SOURCES` only) and `-UNDEBUG` (keeps `assert()`
  active despite the Pico SDK's default `-DNDEBUG` Release build type) —
  see "Coding standard" above.
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
- **`hp41_display_bridge.h`/`.c`**, **`hp41_elite_display_bridge.h`/`.c`**,
  **`hp41_key_bridge.h`/`.c`**, **`hp41_key_hold_bridge.h`/`.c`**,
  **`hp41_persist_state.h`/`.c`**, **`hp41_persist_flash.h`/`.c`** — see
  their sections above.
- **`main.c`** — full system integration. `stdio_init_all()`, then
  `st7920_init()`/`st7920_clear()`, then `nut_boot()`, then a
  `hp41_persist_flash_load()` attempt (see "Continuous memory" above) -
  either restores a valid snapshot and starts `asleep`, or leaves
  `nut_boot()`'s cold-start defaults in place. Main loop per iteration:
  drain pending USB bytes into the key bridge (always, even asleep,
  since a key is what wakes it); handle a pending `[CLRMEM]` request, if
  any; if asleep and a key is now queued, reset `regPC=0`/`flagKey=0`
  and wake; otherwise if asleep, skip `executeNUT()` entirely; else run
  `executeNUT(1000)` (single-step instead, sustaining the key-hold
  state, if a hold is active), throttle via `sleep_us()`; on `fdsp`,
  compute the framebuffer, checksum it, and push it straight to the LCD
  via `st7920_draw_frame()` (no pacing needed — unlike the old Arduino
  path, there's no second, independently-clocked board's receive/draw
  timing to coordinate with); on `POWOFF`, conditionally clear the
  physical LCD — see "Display blanking on POWOFF" below for why this is
  conditional, not unconditional — then capture and save a persistence
  snapshot, then go to sleep; once/second,
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

No ARM toolchain needed — `tests/Makefile` builds and runs all five
with the system `cc`:

```
make -C tests run
```

(`make -C tests` alone builds without running; `make -C tests clean`
removes `tests/build/`.) Power of 10, Rule 10: the Makefile compiles
this project's own sources (the `tests/*.c` files themselves, plus
`firmware/emu41gcc_compat/{nut_stubs,nut_globals,nut_rom}.c` and
`firmware/hp41_*.c`) with `-Wall -Wextra -Wpedantic -Werror`, and the
vendored/generated sources they link against (`emu41gcc/*.c`,
`roms/rom_images.c`, `font-tables/hp41_display_tables.c`) with only the
flags they actually need to build (`-fcommon`, or `nutcpu.c`'s
`nut_stubs.h` force-include) — mirroring `firmware/CMakeLists.txt`'s own
per-file flag scoping exactly, so strict warnings never leak onto code
this project doesn't own.

`tests/nut_smoke_test.c` boots the ROM and runs `executeNUT()` in a
bounded loop — the real HP-41 ROM executes cleanly (thousands of
instructions, zero invalid opcodes) and reaches `POWOFF` showing
`MEMORY LOST` on a cold start, exactly matching real hardware. The other
native tests (`display_bridge_test.c`, `key_bridge_test.c`,
`key_hold_test.c`, `hold_trace_test.c`) follow the same pattern with
their own additional source files — see `tests/Makefile` for exactly
which objects each binary links.

## ROM wiring — `firmware/emu41gcc_compat/nut_rom.h`/`.c`

Wires the base HP-41 OS ROM (`roms/rom_images.c`'s `rom_nut0/1/2[4096]`)
into `tabpage[0-2]`/`typmod[0-2]=1`, and sets the cold-start fields
`initcpu()` would otherwise set (`regPC=0`, `regST=0x0800`, `Carry=1` —
the coldstart flag the ROM's self-test checks — and `mode_printer=-1`).
`roms/rom_images.c` is declared `extern` directly in `nut_rom.c`; update
both together if the wired ROM set ever changes.

## Host-native simulator — `sim/`

A self-contained "virtual Pico 2 + virtual LCD" that boots the real ROM
on the real emulated Nut CPU core and lets the firmware logic be
exercised on a development machine with no physical hardware at all —
built so the physical prototype (Pico 2, LCD, level shifters) could be
freed up for other projects without losing the ability to keep
developing/testing this one. Confirmed working: cold boot correctly
shows `MEMORY LOST` (same 207-lit-pixel render `tests/display_bridge_test.c`
independently verifies, since it's the exact same
`hp41_display_compute_framebuffer()` call), and continuous memory
round-trips correctly across a process restart via a local file.

**Architecture: the same hardware/logic split this project already
has, with three small sim-side replacements for the hardware-boundary
files.** Exactly like real firmware, only `firmware/main.c`,
`firmware/st7920.c`, and `firmware/hp41_persist_flash.c` touch
anything hardware-specific — every other file (`emu41gcc/nutcpu.c`/
`display.c`, `firmware/emu41gcc_compat/*.c`, `hp41_display_bridge.c`,
`hp41_elite_display_bridge.c`, `hp41_key_bridge.c`,
`hp41_key_hold_bridge.c`, `hp41_persist_state.c`, `roms/rom_images.c`,
the font tables) is pure C already proven to build/run natively by
`tests/Makefile`, and `sim/Makefile` links those exact same files
unmodified. `sim/`'s own new files replace just the hardware boundary:

- **`sim_main.c`** — adapted line-by-line from `firmware/main.c`; every
  piece of *logic* (CLRMEM handling, Elite Mode toggles, the
  asleep/wake transition, the hold-vs-batch `executeNUT()` split, the
  `fdsp`-gated render, POWOFF's `dspon`-conditional clear, the
  deferred continuous-memory save) is unchanged — only the
  hardware-facing calls are replaced. Unlike `main.c`'s own
  Rule-4-grandfathered monolithic `main()` (see "Coding standard"
  above), this file's loop body is factored into named `static`
  helpers, since that exception is specific to the original file and
  doesn't extend to new code.
- **`sim_display.c`/`.h`** — implements `firmware/st7920.h`'s exact
  3-function contract (`st7920_init`/`_clear`/`_draw_frame`) via SDL2
  instead of GPIO: a window (144×32 scaled 6x by default —
  `SIM_DISPLAY_SCALE`) that only ever blits the already-decoded 1bpp
  framebuffer bytes it's handed, on the same `fdsp`/`redraw_needed`-
  gated call sites real firmware uses (never a fixed timer), so redraw
  cadence stays faithful to real hardware. No segment-decode logic
  here — `hp41_display_bridge.c` remains the single source of truth
  for what a pixel means.
- **`sim_persist_file.c`/`.h`** — implements `firmware/hp41_persist_flash.h`'s
  exact 3-function contract via a local file
  (`sim/soynut_sim_persist.bin`, gitignored) instead of QSPI flash,
  delegating to the same unmodified `hp41_persist_validate()` real
  firmware uses.
- **`sim_keyboard.c`/`.h`** — maps SDL keyboard events to the exact
  same wire-protocol bytes `tools/hp41_keyboard_gui.py` sends (see
  `sim/README.md` for the key table), reproducing that tool's
  `PressMode.THRESHOLD` tap-vs-hold behavior (`HOLD_ENGAGE_MS=150`,
  same constant/value) in C. Every byte still funnels through the
  unmodified `hp41_key_bridge_feed_byte()` — this file only ever
  decides *which* bytes to send, never touches `keybuffer[]`/hold
  state directly.
- **`sim_clock.c`/`.h`** — host timing (`SDL_GetTicks()` for
  `to_ms_since_boot(get_absolute_time())`, `nanosleep()` for
  `sleep_us()` — chosen over `SDL_Delay()` specifically because its
  1ms granularity is too coarse for the throttle's legitimately
  sub-millisecond values at `TARGET_INSTRUCTIONS_PER_SEC`).
- **`sim_pty.c`/`.h`** — an optional virtual serial port (a PTY, via
  macOS's `openpty()` from `<util.h>`) letting
  `tools/hp41_keyboard_gui.py`'s clickable photo-keyboard drive the sim
  too, with **zero code changes to that tool** — its `SerialLink` class
  just does `serial.Serial(port, BAUD_RATE, timeout=0.2)` with no
  DTR/RTS handling, so it attaches to the PTY's slave path exactly like
  a real port via `--port` (auto-detect won't find it - it only matches
  `usbmodem` device names). `sim_main.c` prints the slave path at
  startup and drains bytes arriving on it through the exact same
  `hp41_key_bridge_feed_byte()` sink SDL keypresses already use (via
  `sim_drain_pty_bytes()`, mirroring `firmware/main.c`'s
  `drain_usb_bytes()` shape) — both input paths coexist with no
  special-casing. `sim_dbg()` also tees every log line to the PTY, so a
  connected GUI's serial log pane shows the same trace real hardware's
  shared USB-CDC connection would. **Keyboard only** — the SDL window
  remains the sole display; no framebuffer streaming to the GUI (see
  below). **A real bug found and fixed during development, not
  theoretical:** `openpty()`'s default slave termios has echo/canonical
  mode on, so bytes `sim_dbg()` wrote to the master (intended only for
  an external reader) were echoed straight back and re-read by
  `sim_pty_read_byte()` as if they were incoming keypresses — confirmed
  as spurious wake/render/POWOFF cycles with no real input at all.
  Fixed by passing a `cfmakeraw()`-initialized `termios` to `openpty()`
  so the PTY never echoes. `sim_main.c` also writes the slave path to
  `build/soynut_sim.port` (a small discovery file, needing no extra
  `.gitignore` entry since `build/` is already ignored wholesale) so a
  wrapper script can find it without parsing log text — see
  `sim/run_with_gui.sh` (`make -C sim run-gui`), which starts both
  `soynut_sim` and the GUI together, attaches the GUI to the discovered
  port automatically, and tears both down when the GUI window closes.

**Known, deliberate differences from real hardware:** (1) on a clean
exit (window closed), `sim_main.c` flushes any not-yet-idle-flushed
continuous-memory save immediately rather than leaving up to
`PERSIST_SAVE_DELAY_MS` of state genuinely at risk the way a real power
yank would — a host process has a clean-exit path real bare-metal
firmware doesn't, so there's no reason not to use it. (2) An invalid
opcode exits the process (`exit(EXIT_FAILURE)`) rather than spinning
forever in a halt loop — the real firmware's halt loop is itself an
explicit, documented Rule 2 exception (see `DEVIATIONS.md`) needed only
because bare-metal code has no OS to hand control back to; host
software does, so a clean exit is the equivalent "explicit recovery"
without introducing an unbounded loop.

**Explicitly not built:** streaming the LCD framebuffer back to
`tools/hp41_keyboard_gui.py` so that tool's own window could render the
display too — a deliberate scope decision (confirmed with the user),
not an oversight: it would need a binary framebuffer protocol
multiplexed with the existing text debug log on the same PTY link, real
added complexity for no benefit when the SDL window already renders the
LCD correctly. The GUI is keyboard-input-only against the sim; see
`sim/README.md` for how to build/run, the current keyboard mapping, and
the two-terminal GUI-attach workflow.

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
                 Also holds Elite User Mode's separate, hand-authored 3x5
                 bitmap font (hp41_elite_font_table.json/.c/.h,
                 gen_elite_font_table.py) - see "Elite User Mode" above.
pico-sdk/        Official raspberrypi/pico-sdk checkout (dependency) -
                 gitignored, not in this repo; see "Toolchain setup"
                 below for how to fetch a matching copy
roms/            ROM converter/format tools + roms/README.md's BYO
                 instructions. The actual .ROM files and generated
                 rom_images.c are gitignored, not in this repo - see
                 "ROM images" above
sim/             Host-native "virtual Pico 2 + virtual LCD" simulator -
                 no pico-sdk/hardware dependency, plain Makefile like
                 tests/. See "Host-native simulator" below.
tests/           Native (non-Pico) tests - confirm the ROM boots, the
                 display bridge renders correctly, and the key bridge
                 parses input correctly, no hardware needed. Makefile
                 builds/runs all five (`make -C tests run`) with Power
                 of 10 Rule 10's strict-warnings scoping - see "Native
                 (host) tests" above.
tools/           Native (non-Pico) diagnostic tools - nut_disasm.c (ROM
                 disassembler using emu41gcc's own desas41.c),
                 powoff_trace.c (single-step ROM/wake-cycle tracer), and
                 hp41_keyboard_gui.py (clickable software keyboard).
                 Makefile builds the two C tools (`make -C tools`),
                 same Rule 10 scoping as tests/Makefile.
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
DEVIATIONS.md    Power of 10 exception list - see "Coding standard"
                 above; authoritative over any inline comment on the
                 same topic if the two ever disagree
pyproject.toml   ruff + mypy config for this project's Python files
                 (Power of 10 Rule 10) - see "Coding standard" above
requirements-dev.txt
                 Python lint/typecheck tooling (`pip3 install -r
                 requirements-dev.txt`, then `ruff check . && mypy .`)
DEVLOG.md        Session-by-session development history (gitignored,
                 local-only - see the note at the top of this file)
```

## Known unknowns / next steps

- **Continuous memory (flash persistence)**: confirmed working on real
  hardware. A value entered and committed with `ENTER` (display checksum
  `0xE3`) survived a genuine reset (`picotool reboot -f`, no `-u`/BOOTSEL
  involved — the CPU fully re-executes from the reset vector with BSS
  zeroed, same as a real power-cycle from the firmware's point of view)
  and reappeared with the exact same checksum after waking — repeated
  successfully across two independent reset cycles. `[CLRMEM]` was also
  confirmed: it live-reruns the ROM's cold-start `MEMORY LOST` sequence,
  and that erased state is itself what a subsequent reset restores (i.e.
  the erasure itself persists, not just the live state).
  **Caveat found during this testing, not previously anticipated:**
  reflashing the firmware via a BOOTSEL UF2 drag-and-drop (`picotool
  reboot -f -u` + copying the `.uf2` onto the mounted volume) wipes the
  persisted region, even though the reserved flash offset is nowhere
  near the addresses the `.uf2` file actually covers — most likely the
  RP2350 boot ROM's mass-storage UF2 write path erases a broader region
  than just the blocks present in the file. A genuine power cycle or
  software reset does **not** have this problem, only a firmware
  reflash does — acceptable for now (a firmware update legitimately
  resetting user memory isn't unreasonable), but worth knowing before
  relying on it, and worth revisiting if it turns out to matter (e.g. a
  smaller, more surgical `picotool load` invocation instead of a full
  drag-and-drop might avoid it). Separately, `picotool reboot -f`
  itself was unreliable when scripted here — it intermittently hangs
  after sending the reboot command (the device resets fine regardless;
  only the tool's own post-reboot wait/probe seems to hang) — matching
  the `-a -f` hang CLAUDE.md already documents for the BOOTSEL variant,
  just not previously known to also affect plain `-f`.
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
