# Soynut

A ground-up hardware-and-software replica of the HP-41C family of
calculators — real Nut CPU emulation, running the real HP-41 ROM images,
driving a real graphic LCD wired and shaped to behave exactly like the
original 12-character display. The name is a nod to *Coconut*, the
HP-41C's own development codename.

## Why

Original HP-41 hardware is now over 45 years old. It's a genuinely
remarkable machine for its era, but time hasn't been kind to it in a few
specific ways: displays and battery contacts degrade, some of the era's
boundary-pushing design decisions didn't fully bake in the way modern
hardware would, and working units and spare parts get scarcer (and
pricier) every year.

Soynut's goal is to preserve the *experience* of the machine — the exact
ROM behavior, the exact display look and feel, eventually the exact
keyboard feel — without depending on any of that aging original
hardware to do it. Everything here is built from new, off-the-shelf
parts that are easy to source today and will keep being easy to source.
The end goal is a complete hardware unit that's virtually
indistinguishable from a real HP-41 in look, feel, and behavior, but
built entirely fresh.

This is emphatically not a from-scratch reimplementation of HP-41
behavior — it runs the actual original ROM images through a real Nut
CPU emulator, for exact fidelity rather than an approximation.

## Status

Working end-to-end today:

- The real HP-41 ROM boots on a Raspberry Pi Pico 2 and correctly shows
  `MEMORY LOST` on cold start, exactly like real hardware.
- The physical LCD renders the calculator's actual display state —
  real 14-segment character shapes and annunciators, not an
  approximation — decoded live from the emulator's own display
  registers.
- Keypresses go in over USB serial, either typed by hand or via an
  included clickable on-screen keyboard (rendered over a real HP-41CX
  keyboard photo).
- Press-and-hold key behavior is modeled: hold a key briefly to see its
  USER-mode label, hold too long and the press nullifies — a real,
  documented HP-41 behavior, working against the genuine ROM logic that
  drives it.

This is very much a project in progress, not a finished product. Still
open:

- The earlier direct 3-wire serial Pico→LCD link (see below) was fully
  protocol- and timing-verified against the ST7920 datasheet, but never
  lit the display. It's low priority now that the direct parallel link
  works, but the likely remaining question — whether the actual analog
  voltage reaching the LCD crosses its logic-high threshold — was never
  directly measured with a multimeter or scope.
- Font table codes 0-31 and 102-125 (most of lowercase `f`-`z`) render
  blank rather than a real glyph; proper extraction for that range was
  never completed.
- Press-and-hold is confirmed working against the real ROM logic (host
  trace plus real hardware), but hasn't been independently reconfirmed
  that quick taps never flash the hold-label in practice.
- Punctuation pixel positions (period/colon/comma) haven't been
  cross-checked against a rendered real character the way the main
  14-segment digits were.
- The dormant Arduino-bridge wiring is unverified against the *current*
  physical setup, since the same Pico pins now do parallel-link duty
  instead.

## How it works, roughly

```
Computer (USB serial)
   │  keypresses in / debug text out
   ▼
Raspberry Pi Pico 2   ──8-bit parallel──▶   NHD-14432WG LCD
(real Nut CPU core,     (direct GPIO,        (ST7920 controller,
 real HP-41 ROM images)  via 3 level          144×32, no backlight)
                         shifter boards)
```

The Pico runs an adapted, unmodified copy of
[emu41gcc](https://github.com/mmoller2k/emu41gcc) — a real, cycle-level
Nut CPU emulator — against the genuine HP-41 OS ROM. Display output and
keyboard input are bridged into and out of that core without touching a
single line of the emulator itself. The Pico drives the LCD directly
over its 8-bit parallel bus — no intermediary board in the loop.

Two earlier display paths are kept in the repo, fully intact but
dormant, in case either is ever needed again:

- **Arduino bridge** (`Arduino NHD14432/NHD14432_DisplayBridge/`): an
  interim setup that routed display output through an Arduino Uno over
  UART while a suitable level shifter was being sourced — the Pico sent
  raw display-register state (38 bytes/frame) and the Arduino decoded
  and drew it. Confirmed working end-to-end on real hardware in its own
  right while it was the active path; superseded outright once three
  level shifter boards made the direct parallel link possible.
- **Direct Pico→LCD serial link** (superseded, recoverable from git
  history): an even earlier attempt to talk to the LCD over its 3-wire
  serial interface directly from the Pico, bypassing any second board.
  The protocol, timing, and wiring were all verified correct against
  the ST7920 datasheet and a logic-analyzer capture of the real signals
  at the LCD's own pins, but the display never lit up, and the root
  cause was never pinned down (see "Status" above) — not blocking
  anything now that the parallel link works.

## Getting started

You'll need:
- A Raspberry Pi Pico 2 (RP2350), an NHD-14432WG-BTFH-VT graphic LCD,
  and three 4-channel bidirectional logic level shifter boards (the
  BSS138-based, auto-sensing kind — e.g. a RobotDyn "Logic Level
  Converter, Bi-Direction" board). Level shifting is required because
  the LCD's `VDD` is 5V and its logic-high input threshold (0.7×VDD ≈
  3.5V) is above the Pico's ~3.3V GPIO output high. 10 signals need
  shifting (`RS`/`E`/`DB0-7`); `R/W` is tied straight to GND on both
  sides and needs no shifter channel at all. The LCD's onboard jumper
  must be set for parallel mode (J3 shorted/J4 open — the board's
  default). See `firmware/pins.h` for the exact pin-by-pin wiring
  table.
- Your own legally-obtained HP-41 ROM images — **not included in this
  repo** (see below).
- The official Raspberry Pi Pico SDK and its ARM toolchain — see
  "Getting the Pico SDK" and "Toolchain setup" below.

```
git clone --recurse-submodules <this repo's URL>
```

(`--recurse-submodules` matters — the Nut CPU core is pulled in as a git
submodule, not vendored directly; see below.)

### Getting the Pico SDK

`pico-sdk/` isn't vendored in this repo — it's the official, versioned
`raspberrypi/pico-sdk`, tested against tag **2.3.0**, ~675MB including
its own nested submodules (tinyusb/cyw43-driver/lwip/mbedtls/btstack).
Fetch a matching copy yourself:

```
git clone --branch 2.3.0 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
```

Either place the checkout at `pico-sdk/` next to this repo's other
top-level directories (`firmware/CMakeLists.txt` defaults to
`../pico-sdk` when `PICO_SDK_PATH` isn't set), or point the
`PICO_SDK_PATH` environment variable (or `-DPICO_SDK_PATH=...`) at
wherever you already keep it — useful if other Pico projects share one
SDK checkout.

### Toolchain setup (macOS, no sudo)

Neither `arm-none-eabi-gcc` nor `ninja` come preinstalled:

- `ninja`: `brew install ninja` works directly.
- `brew install --cask gcc-arm-embedded` needs interactive `sudo` and
  may not be usable in a sandboxed/non-interactive environment.
- `brew install arm-none-eabi-gcc` (the formula) installs without sudo,
  but doesn't bundle newlib (`libc.a`/`libg.a` missing) — linking fails
  with `cannot find -lc`/`-lg`.
- **Workaround:** extract the cask's already-downloaded `.pkg` payload
  directly with
  `pkgutil --expand-full <path-to-pkg> <dest>/toolchain/extracted`
  (no sudo needed) — this is the full ARM GNU Toolchain with newlib
  bundled.

### Build

Supply your own ROM files per `roms/README.md`, then:

```
cd firmware
export PATH="$(cd .. && pwd)/toolchain/extracted/Payload/bin:$PATH"
cmake -G Ninja -B build
ninja -C build
```

(adjust the `toolchain/extracted/Payload/bin` path if you extracted the
toolchain somewhere else.)

## A note on what is and isn't included

- **The HP-41 ROM firmware is not in this repo.** It's HP's copyrighted
  calculator firmware, not open source, and this project has no rights
  to redistribute it. You need to supply your own — see
  `roms/README.md`.
- **The Nut CPU emulation core** (`emu41gcc/`) is a git submodule
  pointing at its own upstream repository, not a copy vendored into
  this one. This isn't just tidiness: this project's rule is that
  `emu41gcc/` is never edited, not even for a one-line portability fix
  (all build-compatibility work lives in `firmware/emu41gcc_compat/`
  instead). A submodule makes that rule structural rather than just a
  doc convention — there's no way to change anything inside it from a
  commit in this repo at all; you'd have to commit against the upstream
  remote and then update this repo's submodule pointer, a deliberate,
  visible, separate action.
- **The Raspberry Pi Pico SDK** isn't vendored either — it's the
  standard, official, versioned SDK most Pico projects keep outside
  their own repo (`PICO_SDK_PATH` exists exactly for this), and at
  ~675MB with its own nested submodules, not something worth duplicating
  per-project. See "Getting the Pico SDK" above.
- Everything else — the firmware, the display/keyboard bridging logic,
  the Arduino sketch, the tooling — is original work, licensed
  GPL-2.0-or-later (see `LICENSE`) to match the emulation core's own
  license exactly.

## Code quality

This project's own original code (not the vendored emulation core or
Pico SDK) follows NASA/JPL's "Power of 10" rules for safety-critical C
and Python — not because a calculator replica needs flight-software
rigor, but because the discipline (bounded loops, real assertions,
zero-warning builds, small functions) keeps a hobby project legible as
it grows. The 10 rules, applied here:

1. Simple control flow — no `goto`/`setjmp`/`longjmp`, no recursion.
2. Every loop has a provably fixed upper bound.
3. No dynamic allocation after init — buffers are static/global arrays.
4. ~60 lines per function.
5. At least 2 assertions per function, with explicit recovery on
   failure — on this bare-metal firmware, that means reporting clearly
   over the debug UART and then halting, since there's no OS to hand an
   error to.
6. Smallest possible variable scope.
7. Every non-void return value is checked; every parameter validated.
8. The preprocessor is limited to header inclusion and simple macros;
   conditional compilation is minimized.
9. Pointers are restricted to one level of dereference; no function
   pointers.
10. The build compiles with the most pedantic warnings enabled, zero
    warnings, plus static analysis (`ruff`/`mypy` for Python).

A handful of specific, narrow exceptions to these rules exist (a couple
of pre-existing functions left structurally alone, some vendored-code
interop shims, and so on) — each documented with the rule it bends,
exactly what's excepted, and why, in `DEVIATIONS.md`, which is
authoritative over any inline comment on the same topic.

## Where this is going

The current build — a Pico dev board, breadboard wiring, level shifter
boards — is a means to an end, not the destination. The intent is to
keep collapsing this down: eventually a dedicated PCB, and eventually a
real keyboard and enclosure, so the finished thing looks and feels like
an HP-41 sitting on your desk — just one that was never at risk of a
forty-year-old component quietly giving out.

## Acknowledgments

- [emu41gcc](https://github.com/mmoller2k/emu41gcc) (Michael Moller's
  gcc port of J-F Garnier's original Emu41), the real Nut CPU emulation
  core this project builds on.
- The HP-41 keyboard photo is a derivative of a photograph by
  Sven.petersen, retouched by Pittigrilli, via Wikimedia Commons,
  CC BY-SA 3.0 — see `tools/hp41_keyboard_gui.py`'s header for full
  attribution.
- Newhaven Display, for the NHD-14432WG-BTFH-VT module this project is
  built around.
