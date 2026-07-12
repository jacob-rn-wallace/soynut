#ifndef SOYNUT_HP41_ARDUINO_BRIDGE_H
#define SOYNUT_HP41_ARDUINO_BRIDGE_H

#include <stdint.h>

// Interim display path (see CLAUDE.md "Arduino display bridge" section):
// sends the computed framebuffer to an Arduino Uno over UART, which
// drives the LCD directly via its own already-hardware-validated
// parallel driver (Arduino NHD14432/NHD14432_DisplayBridge.ino). This
// exists because the only level shifter on hand (a 4-channel
// auto-sensing bidirectional board) couldn't reliably drive the LCD's
// 3-wire serial mode, and can't cover parallel mode's 10 signals either.
// It's meant to be temporary, until a properly-channeled level shifter
// arrives - at which point firmware/st7920.c's direct GPIO drive (still
// fully intact and unused while this is active, not deleted) can resume
// - see main.c for how the two paths are switched between.

void hp41_arduino_bridge_init(void);

// fb must be LCD_FB_SIZE bytes (see st7920.h). Frames it as
// [0xAA sync][576 payload bytes][1 XOR-checksum byte] - the Arduino
// silently drops the frame if the checksum doesn't match, rather than
// showing a corrupted image.
//
// Superseded by hp41_arduino_bridge_send_display_state() below as the
// path main.c actually calls (see that function's comment) - kept
// intact, not deleted, as a fallback/debugging option (e.g. for sending
// a full test pattern the Arduino doesn't need to decode).
void hp41_arduino_bridge_send_frame(const uint8_t *fb);

// Sends the Nut CPU's raw display registers (emu41gcc/display.c's
// lcd_a/b/c[12] + lcd_ann) directly, instead of the fully-rendered
// 576-byte pixel framebuffer - the Arduino now does the segment decode
// and pixel plotting itself (it has its own copy of the same
// compile-time tables, see NHD14432_DisplayBridge/
// hp41_display_tables_avr.h), using logic ported from
// firmware/hp41_display_bridge.c. Frames it as
// [0xAA sync][38 payload bytes][1 XOR-checksum byte]: lcd_a[12],
// lcd_b[12], lcd_c[12], then lcd_ann's low byte, then high byte.
// ~15x less data than hp41_arduino_bridge_send_frame() (38 bytes vs
// 576) - at 9600 baud that's the difference between ~600ms and well
// under 50ms per update. This is purely a wire-format change; it has
// no effect on how long the Arduino then takes to actually paint the
// result onto the LCD (see CLAUDE.md's "wipe effect" investigation -
// that's bound by drawFrameFromRAM()'s GDRAM write loop and the panel's
// own response time, not by how the data got there).
void hp41_arduino_bridge_send_display_state(void);

// TEMPORARY diagnostic: sends a hardcoded, captured-from-a-real-run
// 38-byte payload (the "2.0000" state, captured via the raw-payload hex
// dump added to hp41_arduino_bridge_send_display_state()) directly, with
// no Nut CPU/ROM involvement at all - isolates whether this specific
// content renders correctly on the Arduino when sent in isolation, free
// of any burst/timing pressure from the ROM's normal 3-frames-per-key
// pattern. See main.c for the serial-command trigger.
void hp41_arduino_bridge_send_test_payload(void);

#endif // SOYNUT_HP41_ARDUINO_BRIDGE_H
