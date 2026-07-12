#ifndef SOYNUT_PINS_H
#define SOYNUT_PINS_H

// *** CURRENTLY DORMANT - see "Arduino display bridge" section below for
// the path actually in use right now. ***
// This direct-drive LCD path didn't produce any visible output on real
// hardware (see CLAUDE.md's hardware bring-up log) - cause not yet fully
// isolated (candidates: level shifter speed/rise-time, wiring, still
// something in the serial protocol). Rather than keep debugging it
// blind, display output has temporarily been rerouted through an
// Arduino Uno (already validated hardware-correct for this exact panel)
// over a simple UART link, while a better (actively-driven, more
// channels) level shifter is on order. This section, `st7920.c`, and the
// GPIO pins below are kept fully intact and unmodified - not deleted -
// so this path can resume once the new shifter arrives; only `main.c`
// currently chooses not to call into it. GP0/GP1 below are physically
// reassigned to the Arduino UART link for now (see that section) - they
// are not simultaneously in use for both purposes.
//
// GPIO assignments for the NHD-14432WG-BTFH-VT, ST7920 controller, wired in
// **3-wire serial mode** (see CLAUDE.md "Hardware" section). Switched from
// 8-bit parallel this session: only one 4-channel logic level shifter
// board is available, and parallel mode needs 10 signals (RS, E, DB0-7)
// shifted - serial mode only needs 3 (CS, SID, SCLK), fitting comfortably
// with a channel to spare.
//
// *** REQUIRES A PHYSICAL JUMPER CHANGE ON THE LCD BOARD ***
// Interface select is an onboard jumper, not a wired signal (datasheet,
// "Interface Selection"): J3 shorted/J4 open = parallel (the board's
// default); J4 shorted/J3 open = serial. You must move this jumper
// before serial mode will work at all.
//
// Full 16-pin connector, serial pinout confirmed from the datasheet
// (NHD-14432WG-BTFH-VT.pdf, "Pin Description" - Serial Interface table):
//   1  VSS      -> GND
//   2  VDD      -> +5V (NOT 3.3V - see level-shifting note below)
//   3  VO       -> no connect (this variant is fixed-contrast, no pot needed)
//   4  /CS      -> level shifter A-side -> PIN_LCD_CS below
//   5  SID      -> level shifter A-side -> PIN_LCD_SID below
//   6  SCLK     -> level shifter A-side -> PIN_LCD_SCLK below
//   7-14 (NC in serial mode) -> tie to GND per datasheet
//   15 LED+, 16 LED- -> no connect (no backlight, by design)
// No PSB pin, no RST pin exist on this connector at all (only power-on
// reset; mode select is the J3/J4 jumper above).
//
// *** LEVEL SHIFTING ***
// This module's VDD is 5V (datasheet: 4.5-5.5V) and its logic-high input
// threshold is 0.7*VDD = 3.5V minimum - above the Pico's ~3.3V GPIO
// output high, and Newhaven's own reference wiring diagram shows no
// level shifting in their design at all (assumes a 5V MPU). With only
// one 4-channel bidirectional level shifter board (RobotDyn "Logic Level
// Converter, Bi-Direction": 3.3V/GND/B1-4 low side, 5V/GND/A1-4 high
// side) on hand, wiring is:
//   Pico 3V3 (pin 36)  -> shifter 3.3V
//   Pico VBUS (pin 40) -> shifter 5V (same 5V rail as LCD VDD)
//   Pico GND           -> shifter GND (both sides - see note below)
//   Pico GP0  -> shifter B1 <-> shifter A1 -> LCD pin 4 (/CS)
//   Pico GP1  -> shifter B2 <-> shifter A2 -> LCD pin 5 (SID)
//   Pico GP2  -> shifter B3 <-> shifter A3 -> LCD pin 6 (SCLK)
//   (shifter B4/A4 channel unused/spare)
// All of the Pico's GND pins are the same net internally, so which one
// you use is a convenience choice, not an electrical one - pin 3 (right
// next to GP0/GP1/GP2) and pin 38 (right next to 3V3(OUT)/VBUS) are the
// two most physically convenient here. What DOES matter: the shifter has
// two separate GND pins (one per voltage side) and BOTH must land in
// this same common ground network alongside the Pico and the LCD's VSS
// - not just for tidiness, the auto-sensing bidirectional shifter
// circuit needs a shared ground reference between the two voltage
// domains to work at all.
//
// *** CS POLARITY - UNVERIFIED, LIKELY THE FIRST THING TO CHECK ***
// The datasheet's pin description literally says "/CS ... Active LOW
// Chip Select signal" - but this datasheet includes NO serial-mode
// timing diagram at all (only the parallel read/write timing is given),
// and the near-universal convention among other ST7920 serial-mode
// modules/libraries is ACTIVE HIGH chip select instead. This is a real,
// specific discrepancy that can't be resolved from the document alone.
// st7920.c's LCD_CS_ACTIVE_LOW switch defaults to matching the
// datasheet's literal text (active low); if the display doesn't respond
// at all, flipping that one define is the first thing to try. The
// serial byte format itself (0xF8-based sync byte, MSB-first nibble
// pairs, SCLK idle low) is NOT from this datasheet either (it doesn't
// document that far) - it's the standard, extremely well-established
// ST7920 serial protocol used near-universally elsewhere.

#define PIN_LCD_CS    0   // /CS - see CS polarity note above
#define PIN_LCD_SID   1   // Serial data in (MOSI-equivalent)
#define PIN_LCD_SCLK  2   // Serial clock

// *** Arduino display bridge - THE PATH CURRENTLY ACTIVE ***
// See firmware/hp41_arduino_bridge.h and
// "Arduino NHD14432/NHD14432_DisplayBridge/" (a copy of the original,
// hardware-validated NHD14432_POC sketch, extended with a frame
// receiver - the original POC folder is left untouched as a snapshot of
// that stage of the project).
//
// Pico UART0, reusing the same two physical GPIOs (and the same level
// shifter channels) the direct-drive LCD link above used for /CS and
// SID - those aren't in use while this path is active, so there's no
// real conflict, just a documentation reminder not to expect both at
// once. Unlike the LCD link, this one is comfortably slow (9600 baud)
// and well within the auto-sensing bidirectional shifter's real
// capability - this link was never the suspect for the blank-display
// problem.
//
//   Pico GP0 (UART0 TX) -> shifter B1 <-> shifter A1 -> Arduino D12 (SoftwareSerial RX)
//   Pico GP1 (UART0 RX) -> shifter B2 <-> shifter A2 -> Arduino D13 (SoftwareSerial TX, currently unused - reserved)
//   (same shared 3.3V/5V/GND shifter supply rails as before)
//
// The Arduino's own D0-D11 stay exactly as documented in
// Arduino NHD14432/NHD14432_POC/CLAUDE.md (unchanged) - D12/D13 were
// free on that board, chosen specifically to avoid the Arduino's
// hardware UART (D0/D1, shared with its onboard USB-serial chip -
// wiring another device there while USB is connected causes bus
// contention), using SoftwareSerial instead.

#define PIN_ARDUINO_UART_TX  0   // UART0 TX -> Arduino D12 (via shifter)
#define PIN_ARDUINO_UART_RX  1   // UART0 RX <- Arduino D13 (via shifter; unused for now)

#endif // SOYNUT_PINS_H
