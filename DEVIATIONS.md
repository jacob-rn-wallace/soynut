# Power of 10 — deviations

This project's own code (everything except the vendored `emu41gcc/`
submodule and the external `pico-sdk/` dependency) follows NASA/JPL's
"Power of 10" rules for safety-critical code:

- C: <https://github.com/Vhivi/Powerof10-NASA/blob/29f6f3975bba9c6d6430f8638d8d561786b04c26/rules/Powerof10-C.md>
- Python (adaptation): <https://github.com/Vhivi/Powerof10-NASA/blob/29f6f3975bba9c6d6430f8638d8d561786b04c26/rules/Powerof10-Python.md>

This is a hobbyist calculator replica, not flight software — but the
rules are a genuinely good discipline, so we follow them everywhere
they're actually achievable. A few places conflict with decisions this
project already made deliberately (most from `CLAUDE.md`'s own "black
box" doctrine around the vendored Nut CPU core), or with the Pico
SDK/Arduino framework's own design, which is out of our control. Rather
than fake compliance or silently ignore the rule, each such case is
listed here: which rule, exactly what's excepted, why, and — critically
— the boundary of the exception, so it can't quietly expand to cover
things it wasn't meant to.

This file is updated as the file-by-file rewrite proceeds; treat it as
authoritative over any inline comment if the two ever disagree.

## Rule 6 (smallest possible variable scope) & Rule 8 (limit the preprocessor)

**Scope of the exception:** `firmware/emu41gcc_compat/` (`nut_globals.c`,
`nut_stubs.h`/`.c`, `nut_rom.c`/`.h`, `mem.h`, `dos.h`) and the specific
`extern` declarations in `firmware/hp41_display_bridge.c`,
`firmware/hp41_arduino_bridge.c`, and `firmware/hp41_key_hold_bridge.c`
that reach directly into `emu41gcc`'s own state (`lcd_a`/`lcd_b`/`lcd_c`/
`lcd_ann`, `flagKB`, `regK`, `keybuffer[]`, `lgkeybuf`, `tabpage[]`,
`espaceRAM[]`, etc.).

**Why:** `emu41gcc` is a vendored, unmodifiable black box (see
`CLAUDE.md`'s "The Nut CPU core" section) — we are never allowed to edit
a file inside it, full stop. It declares all of its runtime state as
plain, non-`static` globals via its own `GLOBAL` macro, and there is
literally no other symbol any external code could link against to
observe or drive its keyboard/display state. Bridging into it requires
matching `extern` declarations on our side, and compiling its DOS-era C
under a modern ARM toolchain without editing it requires `-include`
force-included compatibility headers and `-fcommon` (see
`firmware/CMakeLists.txt`). Neither is optional if the "never edit
`emu41gcc`" rule stays absolute, which it does.

**Boundary:** no file outside this exact list may introduce a *new*
global for its own convenience, or reach for `-include`/token-pasting
tricks to solve a problem that a function parameter or return value
could solve instead. If a rewrite pass finds a place doing that, it gets
fixed, not added to this list.

## Rule 2 (every loop must have a fixed, provable upper bound)

**Scope of the exception:** the top-level `while (true)` in
`firmware/main.c` and `lcd_bringup/main.c`, the Arduino sketches'
`loop()` functions (which the Arduino runtime itself calls forever —
there's no way to "bound" that from inside the sketch), and the
halt-on-fatal-error `while (true) { tight_loop_contents(); }` inside
`firmware/main.c`'s invalid-opcode handler.

**Why:** this is the exact case Rule 2's own text anticipates:
"special cases exist for intentionally nonterminating iterations, which
should be provably unable to terminate." Firmware for a device with no
concept of "exiting" needs exactly one genuinely unbounded loop at the
top. The rule's actual concern — a loop that's *supposed* to terminate
but might not, due to a bug — doesn't apply to a loop that was never
meant to terminate in the first place. The halt-on-fatal-error loop is
the same case for a different reason: it's Rule 5's own "explicit
recovery on failure" for a hard invariant violation on bare-metal
firmware with no OS/exception handler to hand an error to (see
`CLAUDE.md`'s Rule 5 discussion) — halting is the recovery action, so
this loop is deliberately unbounded too.

**Boundary:** every *other* loop in this codebase (the 12-cell display,
the 8-slot `keybuffer[]`, the 128-entry `tabcode[]` table, etc.) must
have a real, checkable fixed bound — this exception covers exactly the
designated top-level loop per entry point and the halt-on-fatal-error
idiom, nothing else nested inside either.

## Rule 9 (no function pointers)

**Scope of the exception:** using Pico SDK APIs that take a callback
(GPIO IRQ handlers, TinyUSB's USB stack internals) and Arduino framework
APIs with the same shape (`attachInterrupt`, `SoftwareSerial`'s
interrupt-driven receive).

**Why:** these are third-party dependencies, not our own code — the
Pico SDK and the Arduino core are both out of this project's control in
exactly the same sense `emu41gcc` is, just without a formal "black box"
doctrine written down for them because nobody was ever tempted to edit
vendor SDK internals.

**Boundary:** this exception covers *calling into* SDK/framework APIs
that happen to be callback-shaped. It does not cover declaring a new
function-pointer-typed variable, field, or table in any of our own
logic — if a rewrite pass finds one of those, it gets redesigned instead
of added here.

## Rule 3 (no dynamic allocation after initialization) — Python GUI note

**Scope:** `tools/hp41_keyboard_gui.py`'s Tkinter/PIL usage.

**Why:** a GUI framework's entire object model is "dynamic allocation"
in a way with no real embedded analog — widgets, images, and canvas
items are necessarily created as the program runs, not preallocated
once. The Python adaptation's actual concern is *unbounded* growth
during steady-state operation, not object creation in general.

**Boundary:** this doesn't blanket-except the GUI. The one real
instance of unbounded growth found so far — the serial log pane
appending forever during a long session — is fixed during the rewrite
(capped to a fixed number of lines), not excepted.

## Implementation note: `-UNDEBUG` keeps C assertions active on the Pico/AVR builds

Not a deviation, but worth recording since it isn't obvious: the Pico
SDK's default CMake build type is Release, which sets `-DNDEBUG` - and
`<assert.h>` compiles every `assert()` to nothing under `NDEBUG`,
silently disabling every Rule 5 check this rewrite added. This was
caught for real, not hypothetically: `hp41_arduino_bridge_init()`'s
`uart_init()` return-value check (an `assert()` reading a variable that
would otherwise go unused) turned an ARM build failure
(`-Werror=unused-variable`) into the actual symptom. `firmware/
CMakeLists.txt` and `lcd_bringup/CMakeLists.txt` both append `-UNDEBUG`
to their `SOYNUT_OWN_SOURCES`/own-two-files `set_property(... COMPILE_
OPTIONS ...)` blocks so `assert()` stays active in exactly this
project's own sources regardless of build type, without touching
optimization flags or `emu41gcc`/`pico-sdk`'s own `NDEBUG`-gated
behavior. `tests/Makefile` and `tools/Makefile` never set `-DNDEBUG` at
all (plain `cc`, no build-type default), so this doesn't apply there.
The Arduino toolchain (`arduino-cli`/avr-gcc) also doesn't set `NDEBUG`
by default, so `NHD14432_DisplayBridge.ino`'s `assert()`s are active as
compiled - no equivalent fix was needed there.

## Implementation note: Rule 5 assertions in Python use a helper, not bare `assert`

Not a deviation, but worth recording since it isn't obvious: Python's
`assert` statement is compiled out entirely when the interpreter runs
with `-O`/`-OO`, which would silently disable every Rule 5 precondition/
postcondition check — the opposite of what the rule wants. All Python
files in this project use a small `check(condition, message)` helper
(raises `AssertionError` unconditionally) instead of bare `assert` for
anything Rule 5 actually requires. `ruff`'s `S101` (flags bare `assert`)
is deliberately left *enabled* in `pyproject.toml` rather than ignored —
it should never fire once this convention is applied consistently, and
if it ever does, that's a real regression worth seeing.
