# Soynut simulator (`sim/`)

A host-native "virtual Pico 2 + virtual LCD" - boots the real HP-41 ROM
on the same emulated Nut CPU core the real firmware uses, renders to an
SDL2 window instead of a real ST7920 panel, and reads keypresses
straight from that window instead of USB serial. See CLAUDE.md's
"Host-native simulator" section for the full design.

## Build

Requires SDL2 (`brew install sdl2` on macOS) and `roms/rom_images.c` to
already exist (gitignored, user-generated - see `roms/README.md`).

```
make -C sim run
```

(`make -C sim` alone builds without running; `make -C sim clean` removes
`sim/build/`.)

## Controls

Digits/operators/ENTER/backspace map directly; letters are ALPHA-mode
keys. Named keys with no PC equivalent: `F1`=ON, `TAB`=USER, `F2`=PRGM,
`` ` ``=ALPHA, `F3`=SHIFT, `F4`=SST, `F5`=BST, `F6`=X&lt;&gt;Y, `F7`=R&darr;,
`F8`/`SPACE`=R/S, `F9`=XEQ. Hold a key past ~150ms to engage the real
HP-41 press-and-hold protocol (USER-mode label flash/nullify); release
to send the corresponding wire-protocol byte, matching
`tools/hp41_keyboard_gui.py`'s own threshold behavior. See
`sim/sim_keyboard.c`'s `key_map[]` for the exact table.

## Using the clickable keyboard GUI instead

**One command:**

```
make -C sim run-gui
```

Builds if needed, starts `soynut_sim`, waits for it to open its virtual
serial port, then launches `tools/hp41_keyboard_gui.py` already
attached to it (`sim/run_with_gui.sh` does the wiring - see that
script's header comment for how). Closing the GUI window (or Ctrl-C)
stops the sim too.

**Manual two-terminal equivalent**, useful if you want to restart the
GUI without restarting the sim, or vice versa: `soynut_sim` opens a
virtual serial port (a PTY) at startup and prints its slave path, e.g.:

```
[195ms] soynut sim: virtual serial port at /dev/ttys002 - point tools/hp41_keyboard_gui.py --port /dev/ttys002 at it
```

In a second terminal, point the existing GUI tool at that path - no
code changes needed, it just opens the path like any other serial port:

```
python3 tools/hp41_keyboard_gui.py --port /dev/ttys002
```

Either way: clicking keys in the GUI window drives the sim exactly like
typing in the SDL window does (both work at the same time); the GUI's
own log pane also mirrors the sim's debug output, the same way it shows
real hardware's log over a real USB-CDC connection. The GUI is keyboard
input only here - the LCD is only ever shown in the SDL window, not in
the GUI's own canvas. If PTY creation fails for some reason, the sim
logs a one-line note and keeps working fine standalone via the SDL
window (and `run-gui` will report an error rather than hang).

## Continuous memory

Persists to `sim/soynut_sim_persist.bin` (gitignored) - a genuine
restart of `soynut_sim` restores the last saved state exactly like a
real HP-41's continuous memory, not a fresh `MEMORY LOST` boot. Delete
that file (or send `[CLRMEM]`) to force a cold start.
