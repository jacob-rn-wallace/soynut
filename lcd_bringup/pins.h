#ifndef LCD_BRINGUP_PINS_H
#define LCD_BRINGUP_PINS_H

// NHD-14432WG-BTFH-VT wired in **8-bit parallel mode** (the LCD board's
// own default: J3 shorted / J4 open - see CLAUDE.md "Interface
// Selection" and the datasheet's own jumper table). No jumper change is
// needed for this: the Arduino bridge already drove this panel in
// parallel mode, so the jumper is already in the right position.
//
// Parallel pin table, confirmed directly from the datasheet
// (NHD-14432WG-BTFH-VT.pdf, "Pin Description - Parallel Interface"):
//   1  VSS      -> GND
//   2  VDD      -> +5V
//   3  V0       -> no connect (fixed-contrast variant)
//   4  RS       -> level shifter -> PIN_LCD_RS below
//   5  R/W      -> tied directly to GND (write-only design, same as the
//                  serial-mode driver - no busy-flag reads, so this
//                  never needs to be anything but 0). Since it's a
//                  constant 0V on both sides, it does NOT need a level
//                  shifter channel - wire it straight from LCD pin 5 to
//                  GND (either voltage domain's GND net, they're the
//                  same net).
//   6  E        -> level shifter -> PIN_LCD_E below (falling-edge
//                  triggered - datasheet's own pin table says so
//                  explicitly, and its 8051 example code confirms: set
//                  RS/R-W, set data, E=1, brief delay, E=0 latches)
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
// 10 signals need shifting (RS, E, DB0-7) - one 4-channel board isn't
// enough (that's why this link was serial-only before); three identical
// 4-channel bi-directional boards (12 channels total, 2 spare) cover it.
// Suggested board/channel grouping, chosen to line up with the
// sequential GP0-9 assignment below for simple breadboard wiring:
//   Board A ch1-4 -> RS, E, DB0, DB1   (GP0, GP1, GP2, GP3)
//   Board B ch1-4 -> DB2, DB3, DB4, DB5 (GP4, GP5, GP6, GP7)
//   Board C ch1-2 -> DB6, DB7           (GP8, GP9) - ch3/ch4 spare
// Every board's low side (3.3V/GND) ties to the Pico's 3V3 (pin 36) and
// GND; every board's high side (5V/GND) ties to the same 5V rail as LCD
// VDD (Pico VBUS, pin 40) and GND. All grounds - Pico, all three
// boards' both sides, LCD VSS - must be one common net.

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

#endif // LCD_BRINGUP_PINS_H
