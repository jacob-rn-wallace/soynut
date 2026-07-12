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

This is very much a project in progress, not a finished product — see
`CLAUDE.md` for exactly what's confirmed, what's still open, and the
current hardware bring-up status.

## How it works, roughly

```
Computer (USB serial)
   │  keypresses in / debug text out
   ▼
Raspberry Pi Pico 2   ──UART──▶   Arduino Uno   ──8-bit parallel──▶   NHD-14432WG LCD
(real Nut CPU core,                (display                           (ST7920 controller,
 real HP-41 ROM images)             driver)                            144×32, no backlight)
```

The Pico runs an adapted, unmodified copy of
[emu41gcc](https://github.com/mmoller2k/emu41gcc) — a real, cycle-level
Nut CPU emulator — against the genuine HP-41 OS ROM. Display output and
keyboard input are bridged into and out of that core without touching a
single line of the emulator itself. The LCD is currently driven through
an intermediary Arduino Uno rather than directly by the Pico, purely as
an interim measure while a better level-shifter part is sourced; the
direct link is implemented and its protocol independently verified, just
not yet lit up on real hardware. None of this is meant to be permanent —
see "Where this is going" below.

Full technical detail — confirmed hardware facts, exact wiring, build
instructions, and current open questions — lives in `CLAUDE.md`.

## Getting started

You'll need:
- A Raspberry Pi Pico 2 (RP2350), an NHD-14432WG-BTFH-VT graphic LCD, an
  Arduino Uno (for now), and a logic level shifter — see `CLAUDE.md`'s
  "Hardware" section for the exact wiring.
- Your own legally-obtained HP-41 ROM images — **not included in this
  repo** (see below).
- The official Raspberry Pi Pico SDK (also not vendored in this repo —
  see `CLAUDE.md`'s "Getting the Pico SDK") and its ARM toolchain (see
  "Toolchain setup" for a no-sudo macOS install path).

```
git clone --recurse-submodules <this repo's URL>
```

(`--recurse-submodules` matters — the Nut CPU core is pulled in as a git
submodule, not vendored directly; see below.)

Then supply your own ROM files per `roms/README.md`, fetch the Pico SDK
per `CLAUDE.md`, and build:

```
cd firmware
cmake -G Ninja -B build   # with the toolchain on PATH, see CLAUDE.md
ninja -C build
```

## A note on what is and isn't included

- **The HP-41 ROM firmware is not in this repo.** It's HP's copyrighted
  calculator firmware, not open source, and this project has no rights
  to redistribute it. You need to supply your own — see
  `roms/README.md`.
- **The Nut CPU emulation core** (`emu41gcc/`) is a git submodule
  pointing at its own upstream repository, not a copy vendored into
  this one — see `CLAUDE.md` for why.
- **The Raspberry Pi Pico SDK** isn't vendored either — it's the
  standard, official, versioned SDK most Pico projects keep outside
  their own repo (`PICO_SDK_PATH` exists exactly for this), and at
  ~675MB with its own nested submodules, not something worth duplicating
  per-project. See `CLAUDE.md`'s "Getting the Pico SDK".
- Everything else — the firmware, the display/keyboard bridging logic,
  the Arduino sketch, the tooling — is original work, licensed
  GPL-2.0-or-later (see `LICENSE`) to match the emulation core's own
  license exactly.

## Where this is going

The current build — a Pico dev board, a bridge Arduino, breadboard
wiring — is a means to an end, not the destination. The intent is to
keep collapsing this down: a direct Pico-to-display link once a better
level shifter resolves the one open hardware question left there,
eventually a dedicated PCB, and eventually a real keyboard and
enclosure, so the finished thing looks and feels like an HP-41 sitting
on your desk — just one that was never at risk of a forty-year-old
component quietly giving out.

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
