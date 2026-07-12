/*
  NHD-14432WG-BTFH-VT proof of concept — HP-41C replica display
  ----------------------------------------------------------------
  8-bit PARALLEL interface version (matches the board's as-shipped jumper
  setting — no rework needed).

  CONFIDENCE NOTES:
  - Pinout and the write-mode timing (Tc/Tpw/Tas/Tah/Tdsw/Th) below come
    directly from the NHD-14432WG-BTFH-VT datasheet you provided — this
    interface is better documented in that PDF than serial mode was.
  - The 0x30 -> 0x0C -> 0x34 -> 0x36 init sequence is standard, widely-used
    ST7920 controller behavior (same IC family as countless 128x64 modules).
    It's built from the datasheet's instruction bit-field tables, cross-
    checked against well-established community values, but the datasheet
    itself doesn't give you a step-by-step init recipe.
  - How the GDRAM address maps onto this specific 144x32 panel (vs. the
    more common 128x64 arrangement) is still my best inference from the
    standard ST7920 addressing convention + the "144x32 dots" spec — NOT
    confirmed by the datasheet. This is unchanged from the serial version
    and is still the first thing to verify once this is wired up: if the
    ALL SEGMENTS test pattern looks offset, split, or mirrored, look here.

  Wiring (parallel interface, R/W hardwired since we never read the busy
  flag — we rely on the datasheet's execution-time delays instead):
    Display pin 1  Vss  -> Arduino GND
    Display pin 2  Vdd  -> Arduino 5V
    Display pin 3  Vo   -> No connect (fixed contrast per datasheet)
    Display pin 4  RS   -> Arduino D2
    Display pin 5  R/W  -> GND (tied low, write-only)
    Display pin 6  E    -> Arduino D3
    Display pin 7  DB0  -> Arduino D4
    Display pin 8  DB1  -> Arduino D5
    Display pin 9  DB2  -> Arduino D6
    Display pin 10 DB3  -> Arduino D7
    Display pin 11 DB4  -> Arduino D8
    Display pin 12 DB5  -> Arduino D9
    Display pin 13 DB6  -> Arduino D10
    Display pin 14 DB7  -> Arduino D11
    Display pin 15 LED+ -> 5V through ~47ohm series resistor (my calc, not
                            a datasheet value — see chat notes)
    Display pin 16 LED- -> GND

  D0/D1 are deliberately left untouched so USB Serial keeps working
  (needed for the mockup switcher below, and for the future emulator link).
*/

#include "bitmaps.h"

const uint8_t PIN_RS = 2;
const uint8_t PIN_E  = 3;
const uint8_t DATA_PINS[8] = {4, 5, 6, 7, 8, 9, 10, 11}; // DB0..DB7

// ---- Low-level ST7920 parallel transport ----------------------------------

static void writeByte(bool rs, uint8_t value) {
  digitalWrite(PIN_RS, rs ? HIGH : LOW);
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(DATA_PINS[i], (value >> i) & 0x01);
  }
  // E is falling-edge triggered per datasheet: data must be valid before
  // the high->low transition. Arduino digitalWrite() calls take several
  // microseconds each on AVR, which already dwarfs the datasheet's
  // nanosecond-scale Tas/Tdsw setup requirements, so no extra delay is
  // needed before this point.
  digitalWrite(PIN_E, HIGH);
  digitalWrite(PIN_E, LOW);
}

static void writeCommand(uint8_t cmd) {
  writeByte(false, cmd);
  delayMicroseconds(72); // datasheet: most instructions take 72us
}

static void writeData(uint8_t data) {
  writeByte(true, data);
  delayMicroseconds(72);
}

// ---- Init + graphics ------------------------------------------------------

void st7920Init() {
  pinMode(PIN_RS, OUTPUT);
  pinMode(PIN_E, OUTPUT);
  for (uint8_t i = 0; i < 8; i++) pinMode(DATA_PINS[i], OUTPUT);
  digitalWrite(PIN_E, LOW);
  delay(50); // let the panel power up

  writeCommand(0x30); // basic instruction set, 8-bit
  delay(2);
  writeCommand(0x0C); // display on, cursor off, blink off
  writeCommand(0x01); // clear DDRAM
  delay(2);           // clear takes ~1.6ms per datasheet, not just 72us
  writeCommand(0x34); // extended instruction set, graphic off
  writeCommand(0x36); // extended instruction set, graphic ON
  delay(2);
}

// Clears the full GDRAM (both halves, in case the controller still
// exposes the full 64-row address space internally).
void gdramClear() {
  for (uint8_t y = 0; y < 64; y++) {
    writeCommand(0x80 | (y & 0x1F));           // vertical address
    writeCommand(0x80 | ((y & 0x20) ? 8 : 0));  // horizontal address (bank select)
    for (uint8_t x = 0; x < 8; x++) {
      writeData(0x00);
      writeData(0x00);
    }
  }
}

// Draws a 144x32, 1bpp image (as produced by convert_images.py) starting
// at GDRAM row 0. 9 words (18 bytes) per row, 32 rows.
void drawBitmap(const uint8_t *bmp) {
  for (uint8_t y = 0; y < 32; y++) {
    writeCommand(0x80 | y); // vertical address 0-31
    writeCommand(0x80);     // horizontal address 0 (start of row)
    for (uint8_t word = 0; word < 9; word++) {
      uint8_t hi = pgm_read_byte(&bmp[(y * 9 + word) * 2]);
      uint8_t lo = pgm_read_byte(&bmp[(y * 9 + word) * 2 + 1]);
      writeData(hi);
      writeData(lo);
    }
  }
}

// ---- Sketch entry points ---------------------------------------------------

void setup() {
  Serial.begin(9600);
  Serial.println(F("NHD-14432WG POC (parallel). Send 1/2/3 to switch mockups."));

  st7920Init();
  gdramClear();
  drawBitmap(bmp_00000);
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '1': gdramClear(); drawBitmap(bmp_00000);        Serial.println(F("-> 00000"));       break;
      case '2': gdramClear(); drawBitmap(bmp_memory_lost);  Serial.println(F("-> MEMORY LOST"));  break;
      case '3': gdramClear(); drawBitmap(bmp_all_segments); Serial.println(F("-> ALL SEGMENTS")); break;
    }
  }
}
