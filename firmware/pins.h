/**
 * @file pins.h
 * @brief GPIO pin assignments for the Pico's two display paths (the
 *        active direct-parallel LCD link, and the dormant Arduino-bridge
 *        UART link) plus the wiring/level-shifting rationale behind them.
 *
 * See CLAUDE.md's "Hardware" and "Direct Pico->LCD parallel link"
 * sections for the full physical wiring story; this header is the
 * GPIO-number source of truth those sections describe in prose.
 */
#ifndef SOYNUT_PINS_H
#define SOYNUT_PINS_H

/**
 * @name Direct Pico->LCD parallel link (ACTIVE PATH)
 * @{
 */
// NHD-14432WG-BTFH-VT, ST7920 controller, wired 8-bit parallel (see
// CLAUDE.md "Hardware" section) - the LCD board's own default interface
// (J3 shorted / J4 open, "Interface Selection" in the datasheet), so no
// jumper change was needed switching to this from the Arduino bridge
// (which also drove it in parallel mode).
//
// Confirmed working on real hardware: solid-fill and checkerboard test
// patterns via lcd_bringup/ both rendered correctly (full-screen fill,
// cleanly-aligned checkerboard - no addressing/timing issues) through
// three RobotDyn-style 4-channel auto-sensing bidirectional level
// shifter boards. An earlier attempt at the 3-wire *serial* link with a
// single board of the same type never lit the display at all - since
// swapping to parallel with the same shifter *type* immediately worked,
// the shifter hardware itself was likely never the problem with that
// serial attempt; something specific to the serial protocol/wiring/CS
// polarity was. Not worth chasing further now that parallel works - see
// "Direct Pico->LCD serial link" in CLAUDE.md for that attempt's
// history, kept for reference.
//
// Full 16-pin connector, parallel pinout confirmed from the datasheet
// (NHD-14432WG-BTFH-VT.pdf, "Pin Description - Parallel Interface"):
//   1  VSS      -> GND
//   2  VDD      -> +5V (NOT 3.3V - see level-shifting note below)
//   3  VO       -> no connect (this variant is fixed-contrast, no pot needed)
//   4  RS       -> level shifter -> PIN_LCD_RS below
//   5  R/W      -> tied DIRECTLY to GND (write-only design, no busy-flag
//                  reads - same approach as the earlier serial driver).
//                  This is a constant 0V signal on both voltage domains,
//                  so it does NOT go through a level shifter channel at
//                  all - just a wire straight from LCD pin 5 to GND.
//   6  E        -> level shifter -> PIN_LCD_E below (FALLING-EDGE
//                  triggered - the datasheet's own pin table says so
//                  explicitly, and its 8051 reference code confirms:
//                  set RS + data bus, raise E, brief delay, drop E to
//                  latch)
//   7  DB0  -> level shifter -> PIN_LCD_DB0
//   8  DB1  -> level shifter -> PIN_LCD_DB1
//   9  DB2  -> level shifter -> PIN_LCD_DB2
//   10 DB3  -> level shifter -> PIN_LCD_DB3
//   11 DB4  -> level shifter -> PIN_LCD_DB4
//   12 DB5  -> level shifter -> PIN_LCD_DB5
//   13 DB6  -> level shifter -> PIN_LCD_DB6
//   14 DB7  -> level shifter -> PIN_LCD_DB7
//   15 LED+, 16 LED- -> no connect (no backlight, by design)
//
// *** LEVEL SHIFTING ***
// This module's VDD is 5V (datasheet: 4.5-5.5V) and its logic-high input
// threshold is 0.7*VDD = 3.5V minimum - above the Pico's ~3.3V GPIO
// output high. 10 signals need shifting (RS, E, DB0-7) - one 4-channel
// board isn't enough (that's why the LCD was serial-only for a while);
// three identical 4-channel bidirectional auto-sensing boards (12
// channels total, 2 spare) cover it. Board/channel grouping used here,
// chosen to line up with the sequential GP0-9 assignment below:
//   Board A ch1-4 -> RS, E, DB0, DB1    (GP0, GP1, GP2, GP3)
//   Board B ch1-4 -> DB2, DB3, DB4, DB5 (GP4, GP5, GP6, GP7)
//   Board C ch1-2 -> DB6, DB7           (GP8, GP9) - ch3/ch4 spare
// Every board's low side (3.3V/GND) ties to the Pico's 3V3 (pin 36) and
// GND; every board's high side (5V/GND) ties to the same 5V rail as LCD
// VDD (Pico VBUS, pin 40) and GND. All grounds - Pico, all three boards'
// both sides, LCD VSS - must land in one common net; the auto-sensing
// bidirectional shifter circuit needs a shared ground reference between
// voltage domains to work at all.

#define PIN_LCD_RS   0
#define PIN_LCD_E    1
#define PIN_LCD_DB0  2
#define PIN_LCD_DB1  3
#define PIN_LCD_DB2  4
#define PIN_LCD_DB3  5
#define PIN_LCD_DB4  6
#define PIN_LCD_DB5  7
#define PIN_LCD_DB6  8
#define PIN_LCD_DB7  9
/** @} */

/**
 * @name Arduino display bridge (DORMANT - kept as a fallback)
 * @{
 */
// Was the active path (Pico UART0 -> Arduino Uno -> LCD parallel) before
// the direct-drive path above was reconfirmed working with a properly-
// channeled level shifter setup (three boards instead of one). Kept
// fully intact - firmware/hp41_arduino_bridge.h/.c, the Arduino sketch
// in "Arduino NHD14432/NHD14432_DisplayBridge/" - in case the direct
// link ever needs to be bypassed again; main.c just doesn't call into it
// right now. GP0/GP1 below are NOT simultaneously wired for both this
// and the direct-drive path above - reconnecting this path means
// physically rewiring GP0/GP1 back to the Arduino's UART pins (they're
// now doing RS/E duty for the direct link instead).
//
//   Pico GP0 (UART0 TX) -> shifter -> Arduino D12 (SoftwareSerial RX)
//   Pico GP1 (UART0 RX) -> shifter -> Arduino D13 (SoftwareSerial TX, currently unused - reserved)

#define PIN_ARDUINO_UART_TX  0   // UART0 TX -> Arduino D12 (via shifter) - dormant, see above
#define PIN_ARDUINO_UART_RX  1   // UART0 RX <- Arduino D13 (via shifter; unused for now) - dormant, see above
/** @} */

#endif // SOYNUT_PINS_H
