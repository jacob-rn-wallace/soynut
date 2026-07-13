#!/usr/bin/env python3
"""Clickable HP-41C keyboard GUI for the Soynut replica.

Lets you drive the emulator with a mouse instead of typing key
names/ASCII by hand over serial. Displays
HP-41CX_Programmable_Scientific_Calculator_(removed_
background,_colour_adjustment).jpg - a real photo of an HP-41CX keyboard,
cropped to just the keyboard - and overlays an invisible clickable
rectangle over each physical key. Swapped in this session for the
previous keyboard image, which wasn't confirmed Creative Commons; see
the license note by KEYBOARD_IMAGE below for this one's attribution.

Three button-behavior modes are available (radio buttons in the side
panel, or --press-mode at startup), reflecting three stages this
project went through while building real key-hold support - kept
side-by-side rather than deleted, so any of them can be revisited or
compared directly instead of only trusting memory of how an earlier
stage behaved:

  - "tap" (PressMode.TAP_ONLY) - the original, pre-hold-feature
    behavior: every click sends the plain instant-tap byte(s)
    immediately on press, full stop. No "[+X]"/"[-]" protocol involved
    at all - a click can never be "held."
  - "hold" (PressMode.HOLD_ONLY) - the first hold implementation this
    session: every press immediately sends "[+X]" (no delay), and
    release sends "[-]". This is what caused the "every tap flashes the
    label" bug (see CLAUDE.md's "Real key hold-duration" section) -
    kept here specifically so that bug can be reproduced/compared
    against if useful, not because it's the recommended mode.
  - "threshold" (PressMode.THRESHOLD, default) - the current, fixed
    behavior: waits HOLD_ENGAGE_MS to see whether this is a genuine
    hold or just a normal quick click before ever touching the hold
    protocol - see that constant's comment for why.

In all modes, "[+X]" on mouse-down / "[-]" on mouse-up (when the hold
protocol is used) go through firmware/hp41_key_bridge.c's hold-press/
release escape protocol (see that file and hp41_key_hold_bridge.c), so
how long you hold a button down on screen is how long the emulator sees
the key as held - this is what makes the HP-41's real "hold briefly to
see a USER-mode key's assigned label; hold too long and it nullifies"
behavior (Owner's Handbook p.11) work through this GUI, not just an
instantaneous tap.

KEY_MAP's pixel coordinates were derived by cropping the image into
gridded, labeled snapshots (real pixel-coordinate rulers overlaid on the
photo, not eyeballed guesses) and reading each key's edges off them
directly - unlike the previous image, this is a real photo, not a
synthetic render, but the underlying key grid is still mechanically
even: consistent ~210px row/column pitch within the 5-column function-
key block, and a separate ~275px column pitch within the 4-column digit
block below it (ENTER spans a double-wide column like the real
hardware). Verified by rendering all 39 computed hit-boxes back onto the
full photo and confirming every one lands tightly on its own key with no
overlaps, rather than trusting the arithmetic alone.

KEYBOARD_IMAGE's license: derivative of a photo by Sven.petersen,
retouched (background removed, gamma/black-level/saturation adjusted) by
Pittigrilli, both via Wikimedia Commons -
https://commons.wikimedia.org/wiki/File:HP-41CX_Programmable_Scientific_Calculator_%28removed_background,_colour_adjustment%29.jpg
- licensed CC BY-SA 3.0. The copy in this repo is further cropped to
just the keyboard region; redistributing it (or this repo) must keep
this attribution and the CC BY-SA 3.0 license per its terms.

Usage:
    python3 tools/hp41_keyboard_gui.py [--port /dev/cu.usbmodemXXXX]

If --port is omitted, tries to auto-detect a single plausible USB serial
port (excludes ones that look like an Arduino Uno, since that's the
*other* board in this project's setup - see CLAUDE.md's Arduino display
bridge section). Prints the list and exits if it can't decide.

The same serial connection carries both directions: keys sent here, and
main.c's own debug output (heartbeat, framebuffer dumps, etc.) read back
and shown in the log pane - so this doubles as the debug console you'd
otherwise use `screen`/`pyserial` directly for.
"""
from __future__ import annotations

import argparse
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from typing import ClassVar

import serial
import serial.tools.list_ports
from PIL import Image, ImageTk

REPO_ROOT = Path(__file__).resolve().parent.parent
KEYBOARD_IMAGE_NAME = (
    "HP-41CX_Programmable_Scientific_Calculator_(removed_background,_colour_adjustment).jpg"
)
KEYBOARD_IMAGE = REPO_ROOT / KEYBOARD_IMAGE_NAME

BAUD_RATE = 115200  # matches firmware/main.c's stdio_init_all() USB CDC - not the
                    # separate 9600-baud Arduino display link, a different port.

# Power of 10 (Python adaptation), Rule 3: the serial log pane below is
# the one place this GUI could grow without bound during a long session
# (every line main.c prints gets appended, forever) - capped rather than
# excepted, per DEVIATIONS.md's Rule 3 note for this file.
MAX_LOG_LINES = 2000


def check(condition: bool, message: str) -> None:
    """Power of 10 (Python adaptation), Rule 5 assertion helper.

    Unlike a bare `assert` statement, this is never compiled out under
    `-O`/`-OO` - see ../DEVIATIONS.md's implementation note on why this
    project uses `check()` instead of `assert` for anything Rule 5
    actually requires.
    """
    if not condition:
        raise AssertionError(message)


class PressMode:
    """The three button-behavior modes this GUI supports.

    See the module docstring for what each one does and why all three
    are kept around instead of just the current recommended one.
    """

    TAP_ONLY = "tap"
    HOLD_ONLY = "hold"
    THRESHOLD = "threshold"

    ALL = (TAP_ONLY, HOLD_ONLY, THRESHOLD)
    LABELS: ClassVar[dict[str, str]] = {
        TAP_ONLY: "Instant tap (no hold)",
        HOLD_ONLY: "Instant hold (no threshold)",
        THRESHOLD: "Threshold (recommended)",
    }


# How long a button must stay down before PressMode.THRESHOLD treats it
# as a real "hold" (engaging "[+X]"/"[-]") rather than a normal instant
# tap - see the module docstring for why this exists. Comfortably longer
# than any realistic click's mechanical duration, comfortably shorter
# than the manual's own "about half a second" nullify point, so a
# deliberate hold still reaches the real hold protocol quickly.
HOLD_ENGAGE_MS = 150

# Keys that must always be sent as a plain instant tap, regardless of the
# active PressMode - see _on_press()'s use of this set for why. Currently
# just ON: confirmed on real hardware that driving it through the hold
# protocol (as THRESHOLD would for any key held past HOLD_ENGAGE_MS, or
# HOLD_ONLY immediately) makes the ROM spin for 100,000+ instructions
# before it finally toggles power on release - a very noticeable stall,
# not a snappy toggle - because ON is a power button, not a USER-mode-
# assignable function key, and firmware/hp41_key_bridge.c's
# resolve_hold_code() now rejects "[+ON]" outright to match. Sending it
# as a tap here avoids ever hitting that rejected path in the first
# place, so a click - however long it's physically held on screen -
# reacts immediately, the same way a real ON button would.
ALWAYS_TAP_KEYS: frozenset[bytes] = frozenset({b"[ON]"})

# Displayed at this fraction of the source image's native resolution
# (1165x1972) - keeps the window a reasonable size on a laptop screen.
# Click coordinates are scaled by the same factor at hit-test time.
DISPLAY_SCALE = 0.4

# (label, center_x, center_y, half_width, half_height, key) - all in
# NATIVE image pixel coordinates (pre-DISPLAY_SCALE). key identifies the
# physical key exactly like typing it at a terminal would: plain ASCII
# for keys tabcode[] already covers (digits, operators, and each
# function key's blue ALPHA-mode letter - e.g. 'A' for Sigma+, since
# tabcode['A'] and that physical key's raw HP-41 code are the same
# value), "[NAME]" for named_keys[] entries (keys with no ASCII
# equivalent on the real keyboard either). _hold_press_bytes()/b"[-]"
# below wrap this into the real press/release protocol at click time -
# a plain tap (e.g. from a script) can still send `key` directly, same
# as before this session's hold-duration work.
KEY_MAP: list[tuple[str, int, int, int, int, bytes]] = [
    # Top row: ON / USER / [blank card-slot, not a key] / PRGM / ALPHA
    ("ON",    113, 93, 95, 45, b"[ON]"),
    ("USER",  337, 93, 115, 45, b"[USER]"),
    ("PRGM",  825, 93, 90, 45, b"[PRGM]"),
    ("ALPHA", 1038, 93, 110, 45, b"[ALPHA]"),

    # Row 1: SIGMA+ / 1/x / sqrt(x) / LOG / LN  (blue letters A-E)
    ("SIGMA+", 165, 360, 70, 47, b"A"),
    ("1/X",    375, 360, 70, 47, b"B"),
    ("SQRT",   585, 360, 70, 47, b"C"),
    ("LOG",    795, 360, 70, 47, b"D"),
    ("LN",     1005, 360, 70, 47, b"E"),

    # Row 2: X<>Y / R-down / SIN / COS / TAN  (blue letters F-J)
    ("X<>Y", 165, 570, 70, 47, b"F"),
    ("RDN",  375, 570, 70, 47, b"G"),
    ("SIN",  585, 570, 70, 47, b"H"),
    ("COS",  795, 570, 70, 47, b"I"),
    ("TAN",  1005, 570, 70, 47, b"J"),

    # Row 3: SHIFT (solid gold key, no label) / XEQ / STO / RCL / SST
    ("SHIFT", 165, 781, 70, 47, b"[SHIFT]"),
    ("XEQ",   375, 781, 70, 47, b"[XEQ]"),
    ("STO",   585, 781, 70, 47, b"L"),
    ("RCL",   795, 781, 70, 47, b"M"),
    ("SST",   1005, 781, 70, 47, b"[SST]"),

    # Row 4: ENTER (double-wide, like the real keyboard) / CHS / EEX / CLx (backspace)
    ("ENTER", 270, 998, 190, 47, b"\r"),
    ("CHS",   585, 998, 70, 47, b"O"),
    ("EEX",   795, 998, 70, 47, b"P"),
    ("CLX",   1005, 998, 70, 47, b"[CLX]"),

    # Row 5: minus / 7 / 8 / 9
    ("-", 160, 1206, 74, 43, b"-"),
    ("7", 435, 1206, 74, 43, b"7"),
    ("8", 710, 1206, 74, 43, b"8"),
    ("9", 985, 1206, 74, 43, b"9"),

    # Row 6: plus / 4 / 5 / 6
    ("+", 160, 1406, 74, 43, b"+"),
    ("4", 435, 1406, 74, 43, b"4"),
    ("5", 710, 1406, 74, 43, b"5"),
    ("6", 985, 1406, 74, 43, b"6"),

    # Row 7: multiply / 1 / 2 / 3
    ("X", 160, 1620, 74, 43, b"*"),
    ("1", 435, 1620, 74, 43, b"1"),
    ("2", 710, 1620, 74, 43, b"2"),
    ("3", 985, 1620, 74, 43, b"3"),

    # Row 8: divide / 0 / decimal point / R/S
    ("/",   160, 1830, 74, 43, b"/"),
    ("0",   435, 1830, 74, 43, b"0"),
    (".",   710, 1830, 74, 43, b"."),
    ("R/S", 985, 1830, 74, 43, b"[RS]"),
]


def _hold_press_bytes(key: bytes) -> bytes:
    r"""Wrap a KEY_MAP entry into a "[+X]" press-and-hold-start escape.

    See firmware/hp41_key_bridge.h: "[NAME]" becomes "[+NAME]", and a
    plain single character (digits, operators, ALPHA-mode letters, or
    raw \r for Enter) becomes "[+<char>]" - hp41_key_bridge.c's
    resolve_hold_code() falls back to a tabcode[] lookup for anything
    that isn't a recognized name, so no special-casing is needed here
    for which case a given key is.

    Args:
        key: A KEY_MAP entry's raw protocol bytes.

    Returns:
        The wrapped "[+...]" hold-press escape sequence.
    """
    check(len(key) > 0, "KEY_MAP entry has an empty key")
    if key.startswith(b"[") and key.endswith(b"]"):
        return b"[+" + key[1:-1] + b"]"
    return b"[+" + key + b"]"


def find_port() -> str:
    """Auto-detect the Pico's USB serial port.

    Excludes anything that looks like the Arduino display bridge (see
    CLAUDE.md's "Arduino display bridge" section) - that's the *other*
    board in this project's setup, not the one this GUI should talk to.

    Returns:
        The single plausible port's device path.

    Raises:
        SystemExit: if zero or more than one plausible port is found;
            prints the available ports first so the user can pass
            --port explicitly.
    """
    ports = list(serial.tools.list_ports.comports())
    candidates = [p for p in ports if "usbmodem" in p.device.lower()
                  and "arduino" not in (p.description or "").lower()]
    if len(candidates) == 1:
        return candidates[0].device
    print("Could not auto-detect the Pico's serial port.")
    if ports:
        print("Available ports:")
        for p in ports:
            print(f"  {p.device}  ({p.description})")
    print("Pass one explicitly with --port.")
    sys.exit(1)


class SerialLink:
    """Owns the serial connection.

    Writes keys from the GUI thread, reads lines in a background thread
    and hands them to the GUI via a queue (Tkinter isn't thread-safe -
    the main loop polls the queue instead of touching widgets from this
    thread directly).
    """

    def __init__(self, port: str) -> None:
        """Open the serial port and start the background read thread.

        Args:
            port: Device path for the Pico's USB serial port (e.g.
                "/dev/cu.usbmodem14201").
        """
        self.ser = serial.Serial(port, BAUD_RATE, timeout=0.2)
        self.line_queue: queue.Queue[str] = queue.Queue()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def _read_loop(self) -> None:
        """Background thread body: read bytes, split on newlines, queue lines."""
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(256)
            except serial.SerialException:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.line_queue.put(line.decode(errors="replace").rstrip("\r"))

    def send(self, data: bytes) -> None:
        """Write bytes to the serial port and flush immediately.

        Args:
            data: Bytes to send (a key protocol byte string, e.g. b"[+A]").
        """
        check(len(data) > 0, "SerialLink.send() called with empty data")
        self.ser.write(data)
        self.ser.flush()

    def close(self) -> None:
        """Stop the read thread and close the serial port."""
        self._stop.set()
        self.ser.close()


class KeyboardApp:
    """The clickable keyboard window: photo canvas, mode selector, and serial log pane."""

    def __init__(
        self, root: tk.Tk, link: SerialLink, initial_mode: str = PressMode.THRESHOLD,
    ) -> None:
        """Build the keyboard window: canvas, side panel, and serial polling.

        Args:
            root: The Tk root window to build the UI into.
            link: An already-open SerialLink to send keys through and
                read the debug log from.
            initial_mode: One of PressMode.ALL, the button-behavior mode
                selected when the window first opens.
        """
        check(initial_mode in PressMode.ALL, f"unknown initial_mode: {initial_mode!r}")
        self.link = link
        root.title("HP-41C Keyboard (Soynut)")

        # Only one physical mouse button can be down at a time, so a
        # single set of "currently pressed key" fields (rather than
        # per-rectangle state) is enough - see _on_press()/_on_release().
        self._hold_timer: str | None = None  # canvas.after() id for the pending engage check
        self._hold_engaged = False  # True once HOLD_ENGAGE_MS has elapsed while still down
        # Mode is captured once per press (in _on_press) rather than
        # re-read live in _on_release/_engage_hold - switching the radio
        # button mid-press (a rare, deliberate action) shouldn't change
        # how a press already in flight gets resolved.
        self._active_mode: str | None = None
        self.press_mode = tk.StringVar(value=initial_mode)

        image = Image.open(KEYBOARD_IMAGE)
        disp_w = int(image.width * DISPLAY_SCALE)
        disp_h = int(image.height * DISPLAY_SCALE)
        self.tk_image = ImageTk.PhotoImage(image.resize((disp_w, disp_h)))

        main = tk.Frame(root)
        main.pack(fill=tk.BOTH, expand=True)

        self.canvas = self._build_canvas(main, disp_w, disp_h)
        self.status, self.log = self._build_side_panel(main)

        root.after(50, self._poll_serial)

    def _build_canvas(self, main: tk.Frame, disp_w: int, disp_h: int) -> tk.Canvas:
        """Build the keyboard photo canvas and bind every KEY_MAP entry's clickable rectangle.

        Split out of __init__ to keep that one under Rule 4's ~60-line target.
        """
        canvas = tk.Canvas(main, width=disp_w, height=disp_h, highlightthickness=0)
        canvas.pack(side=tk.LEFT, fill=tk.BOTH)
        canvas.create_image(0, 0, anchor=tk.NW, image=self.tk_image)

        # No <Enter>/<Leave> hover-highlight bindings here - macOS Aqua Tk
        # has a known feedback-loop quirk where changing a canvas item's
        # appearance (e.g. its outline) while the pointer is over it can
        # spuriously re-trigger Enter/Leave for that same item (Enter ->
        # itemconfig -> Tk re-evaluates pointer containment -> Leave/Enter
        # again -> ...), a tight loop that presents as a total freeze/
        # beachball. Confirmed this was the cause (not an old-Tcl/Tk
        # version issue - checked, this environment already runs 8.6.14)
        # by removing it. <Button-1>/<ButtonRelease-1> are a one-shot
        # press/release pair each, not continuously re-evaluated against
        # pointer position the way hover detection is, so they don't share
        # that failure mode.
        for lbl, cx, cy, hw, hh, key in KEY_MAP:
            x0, y0 = (cx - hw) * DISPLAY_SCALE, (cy - hh) * DISPLAY_SCALE
            x1, y1 = (cx + hw) * DISPLAY_SCALE, (cy + hh) * DISPLAY_SCALE
            rect = canvas.create_rectangle(x0, y0, x1, y1, outline="", fill="", width=2)

            def on_press(
                _event: tk.Event, lbl: str = lbl, key: bytes = key, rect: int = rect,
            ) -> None:
                self._on_press(lbl, key, rect)

            def on_release(
                _event: tk.Event, lbl: str = lbl, key: bytes = key, rect: int = rect,
            ) -> None:
                self._on_release(lbl, key, rect)

            canvas.tag_bind(rect, "<Button-1>", on_press)
            canvas.tag_bind(rect, "<ButtonRelease-1>", on_release)
        return canvas

    def _build_side_panel(self, main: tk.Frame) -> tuple[tk.Label, tk.Text]:
        """Build the mode-selector radio buttons, status line, and serial log pane.

        Split out of __init__ alongside _build_canvas() for the same Rule 4 reason.
        """
        side = tk.Frame(main)
        side.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        mode_frame = tk.LabelFrame(side, text="Button behavior")
        mode_frame.pack(fill=tk.X, padx=4, pady=4)
        for mode in PressMode.ALL:
            tk.Radiobutton(mode_frame, text=PressMode.LABELS[mode],
                            variable=self.press_mode, value=mode,
                            anchor="w").pack(fill=tk.X, anchor="w")

        status = tk.Label(side, text="ready", anchor="w")
        status.pack(fill=tk.X)

        log = tk.Text(side, width=60, bg="black", fg="#33ff33",
                       font=("Menlo", 10), state=tk.DISABLED)
        log.pack(fill=tk.BOTH, expand=True)
        return status, log

    def _send_tap(self, label: str, key: bytes) -> None:
        """Send a plain instant tap (no hold protocol) and update the status line.

        Args:
            label: Human-readable key name, for the status line.
            key: The raw KEY_MAP protocol bytes for this key.
        """
        try:
            self.link.send(key)
        except serial.SerialException as e:
            self.status.config(text=f"send failed: {e}")
            return
        self.status.config(text=f"tapped {label} -> {key!r}")

    def _on_press(self, label: str, key: bytes, rect_id: int) -> None:
        """Handle mouse-down on a key's rectangle: flash it, then act per the active mode.

        Args:
            label: Human-readable key name, for the status line.
            key: The raw KEY_MAP protocol bytes for this key.
            rect_id: Canvas item id of this key's clickable rectangle,
                for the visual press flash.
        """
        # Visual "pressed" flash happens immediately regardless of mode -
        # purely local UI feedback, doesn't need to match the ROM's own
        # timing.
        self.canvas.itemconfig(rect_id, outline="#ff3333")

        # Capture the mode now - see __init__'s comment on _active_mode
        # for why _on_release()/_engage_hold() use this instead of
        # re-reading self.press_mode live.
        mode = self.press_mode.get()
        self._active_mode = mode
        self._hold_engaged = False
        self._hold_timer = None

        if key in ALWAYS_TAP_KEYS:
            # See ALWAYS_TAP_KEYS's comment - this key must never enter
            # the hold protocol, regardless of mode. _on_release() below
            # checks the same set and returns immediately for it, since
            # the tap has already been sent here.
            self._send_tap(label, key)
            return

        if mode == PressMode.TAP_ONLY:
            # Original, pre-hold-feature behavior: send the instant tap
            # right away, full stop - there's nothing left to do on
            # release, and the hold protocol is never touched.
            self._send_tap(label, key)
            return

        if mode == PressMode.HOLD_ONLY:
            # The first hold implementation this session: every press
            # immediately engages the real hold protocol, no threshold
            # delay. Kept for comparison - see PressMode's docs.
            self._engage_hold(label, key)
            return

        # PressMode.THRESHOLD: don't commit to the real hold protocol
        # immediately - wait HOLD_ENGAGE_MS to see whether this is a
        # genuine hold or just a normal quick click (see that constant's
        # comment for why). If released first, _on_release() below sends
        # a plain instant tap instead and this timer never fires.
        self._hold_timer = self.canvas.after(HOLD_ENGAGE_MS, lambda: self._engage_hold(label, key))

    def _engage_hold(self, label: str, key: bytes) -> None:
        """Begin the real "[+X]" hold protocol for the given key.

        Args:
            label: Human-readable key name, for the status line.
            key: The raw KEY_MAP protocol bytes for this key.
        """
        self._hold_timer = None
        self._hold_engaged = True
        send_bytes = _hold_press_bytes(key)
        try:
            self.link.send(send_bytes)
        except serial.SerialException as e:
            self.status.config(text=f"send failed: {e}")
            return
        self.status.config(text=f"holding {label} -> {send_bytes!r}")

    def _on_release(self, label: str, key: bytes, rect_id: int) -> None:
        """Handle mouse-up on a key's rectangle: end a hold, or send a deferred tap.

        Args:
            label: Human-readable key name, for the status line.
            key: The raw KEY_MAP protocol bytes for this key.
            rect_id: Canvas item id of this key's clickable rectangle,
                for clearing the visual press flash.
        """
        self.canvas.itemconfig(rect_id, outline="")

        if key in ALWAYS_TAP_KEYS or self._active_mode == PressMode.TAP_ONLY:
            # _on_press() already sent the full tap - nothing to do.
            return

        if self._hold_engaged:
            self._hold_engaged = False
            try:
                self.link.send(b"[-]")
            except serial.SerialException as e:
                self.status.config(text=f"send failed: {e}")
                return
            self.status.config(text=f"released {label}")
            return

        # Only reachable in THRESHOLD mode: released before
        # HOLD_ENGAGE_MS - cancel the pending engage timer and send a
        # plain instant tap instead (the exact same path this GUI used
        # before the hold feature existed), so a normal quick click
        # never touches the hold protocol at all.
        if self._hold_timer is not None:
            self.canvas.after_cancel(self._hold_timer)
            self._hold_timer = None
        self._send_tap(label, key)

    def _append_log(self, line: str) -> None:
        """Append one line to the log pane, trimming from the top past MAX_LOG_LINES.

        Power of 10 (Python adaptation) Rule 3: an unattended
        long-running session must not let this widget's backing text
        grow without bound.

        Args:
            line: The log line to append (no trailing newline needed).
        """
        self.log.config(state=tk.NORMAL)
        self.log.insert(tk.END, line + "\n")
        line_count = int(self.log.index("end-1c").split(".")[0])
        if line_count > MAX_LOG_LINES:
            overflow = line_count - MAX_LOG_LINES
            self.log.delete("1.0", f"{overflow + 1}.0")
        self.log.see(tk.END)
        self.log.config(state=tk.DISABLED)

    def _poll_serial(self) -> None:
        """Drain any queued serial log lines into the log pane, then reschedule itself."""
        try:
            while True:
                line = self.link.line_queue.get_nowait()
                self._append_log(line)
        except queue.Empty:
            pass
        self.canvas.after(50, self._poll_serial)


def main() -> None:
    """Parse args, connect to the Pico, and run the Tk event loop until the window closes."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="Pico's USB serial port (auto-detected if omitted)")
    parser.add_argument("--press-mode", choices=PressMode.ALL, default=PressMode.THRESHOLD,
                         help="initial button behavior (default: %(default)s) - "
                              "can also be changed live via the radio buttons in the GUI")
    args = parser.parse_args()

    port = args.port or find_port()
    print(f"Connecting to {port} @ {BAUD_RATE} baud...")
    link = SerialLink(port)

    root = tk.Tk()
    KeyboardApp(root, link, initial_mode=args.press_mode)
    try:
        root.mainloop()
    finally:
        link.close()


if __name__ == "__main__":
    main()
