#!/usr/bin/env bash
# Convenience launcher for `make -C sim run-gui`: starts soynut_sim and
# tools/hp41_keyboard_gui.py together, wiring the GUI to the sim's
# freshly-opened virtual serial port automatically - see
# sim/README.md's "Using the clickable keyboard GUI instead" section
# for the manual two-terminal equivalent this automates, and
# CLAUDE.md's "Host-native simulator" section for the design.
#
# Not run directly by the Makefile's own recipe logic (kept as a
# separate script rather than inline Makefile shell, since the
# wait-for-file/trap-based cleanup below is easier to read and edit as
# a real script than as escaped Makefile recipe lines).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SIM_BIN="build/soynut_sim"
PORT_FILE="build/soynut_sim.port"

if [[ ! -x "$SIM_BIN" ]]; then
    echo "error: $SIM_BIN not built - run 'make -C sim' first" >&2
    exit 1
fi

# Discard any stale discovery file from a previous (possibly crashed)
# run, so the wait loop below can only ever see a fresh one written by
# the sim instance this script is about to start.
rm -f "$PORT_FILE"

"$SIM_BIN" &
SIM_PID=$!

cleanup() {
    if kill -0 "$SIM_PID" 2>/dev/null; then
        kill "$SIM_PID" 2>/dev/null || true
        wait "$SIM_PID" 2>/dev/null || true
    fi
    rm -f "$PORT_FILE"
}
trap cleanup EXIT INT TERM

# Bounded wait (10s) for the sim to open its virtual serial port and
# announce it via the discovery file - see sim_main.c's
# sim_write_port_file(). Polling a file rather than parsing sim's log
# output avoids any dependency on exact log wording/timing.
attempt=0
while [[ ! -s "$PORT_FILE" ]]; do
    if ! kill -0 "$SIM_PID" 2>/dev/null; then
        echo "error: soynut_sim exited before opening a virtual serial port" >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    if [[ "$attempt" -ge 100 ]]; then
        echo "error: timed out waiting for soynut_sim's virtual serial port" >&2
        exit 1
    fi
    sleep 0.1
done

PORT="$(cat "$PORT_FILE")"
echo "soynut sim launcher: attaching keyboard GUI to $PORT"

# Foreground: the GUI window is the main thing the user interacts with.
# Closing it (or Ctrl-C here) triggers the trap above, which stops the
# sim too - if the sim's own SDL window is closed first instead, the
# GUI keeps running harmlessly (its keypresses just go nowhere) until
# the user closes it themselves.
python3 ../tools/hp41_keyboard_gui.py --port "$PORT"
