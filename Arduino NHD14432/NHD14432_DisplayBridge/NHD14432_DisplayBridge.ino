/*
  NHD-14432WG-BTFH-VT display bridge — HP-41C replica display
  ----------------------------------------------------------------
  8-bit PARALLEL interface version (matches the board's as-shipped jumper
  setting — no rework needed). This is a copy of NHD14432_POC (kept
  untouched as a snapshot of that stage of the project - see its own
  CLAUDE.md), extended to also receive live raw display state (lcd_a/b/c/
  lcd_ann, not a pre-rendered pixel framebuffer - see
  computeFramebufferFromState() below) from a Raspberry Pi Pico 2 running
  the actual Nut CPU emulator, over a second, independent serial link —
  see "Pico display bridge" below. The original 1/2/3 built-in
  test-pattern switcher (over the Arduino's normal USB Serial) is
  currently commented out in loop() - see the note there.

  Why this exists: the Pico's own attempt to drive this display directly
  (first 8-bit parallel, then 3-wire serial, in both cases through a
  4-channel auto-sensing bidirectional level shifter) never produced any
  visible output, cause not fully isolated. This LCD + this exact parallel
  driver were already hardware-validated working via NHD14432_POC, so
  rather than keep debugging blind, the Pico now runs the emulator only
  and hands off the actual display driving to this already-working
  Arduino code, until a better level shifter arrives to let the Pico
  drive the LCD directly again.

  CONFIDENCE NOTES:
  - Pinout and the write-mode timing (Tc/Tpw/Tas/Tah/Tdsw/Th) below come
    directly from the NHD-14432WG-BTFH-VT datasheet you provided — this
    interface is better documented in that PDF than serial mode was.
  - The 0x30 -> 0x0C -> 0x34 -> 0x36 init sequence is standard, widely-used
    ST7920 controller behavior (same IC family as countless 128x64 modules).
    It's built from the datasheet's instruction bit-field tables, cross-
    checked against well-established community values, but the datasheet
    itself doesn't give you a step-by-step init recipe.
  - The GDRAM addressing below (vertical 0-31 direct, 9 words/row, no
    bank-select fold) is CONFIRMED - this exact code was physically run
    and the test images displayed correctly. (A separate, independent
    hypothesis on the Pico side's own driver guessed a bank-folding
    scheme instead; that guess was wrong and has since been corrected to
    match this validated version.)

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
  (needed for the mockup switcher below).

  Pico display bridge (NEW):
    Arduino D12 <- (via the Pico's existing level shifter) <- Pico GP0 (UART0 TX)
    Arduino D13 -> (via the level shifter) -> Pico GP1 (UART0 RX) - wired but
                    currently unused; reserved for a future ack/status channel
  Deliberately NOT wired to the Arduino's D0/D1 hardware UART - those are
  shared with the onboard USB-serial chip, and driving them from a second
  external source at the same time as USB is connected causes bus
  contention. SoftwareSerial on D12/D13 keeps this link fully independent
  of the USB Serial connection, so both can be used at once (e.g. for
  debugging via the serial monitor while the Pico link is live).
*/

#include "bitmaps.h"
#include "hp41_display_tables_avr.h"
#include <SoftwareSerial.h>
#include <string.h>

const uint8_t PIN_PICO_RX = 12; // <- Pico GP0 (UART0 TX), via level shifter
const uint8_t PIN_PICO_TX = 13; // -> Pico GP1 (UART0 RX), via level shifter (unused for now)
// Note: D13 is also the Uno's onboard LED pin - since this TX side is
// currently unused (idles high), expect the onboard LED to just stay lit
// rather than blink. Cosmetic only, not a sign of anything wrong.
SoftwareSerial picoLink(PIN_PICO_RX, PIN_PICO_TX);

const uint8_t FRAME_SYNC = 0xAA;

// Raw HP-41 display registers now, not a pre-rendered pixel framebuffer:
// lcd_a[12] + lcd_b[12] + lcd_c[12] (emu41gcc/display.c's own shift
// registers) + lcd_ann (2 bytes, low byte then high byte). ~15x less
// data than the old 576-byte framebuffer - see firmware/
// hp41_arduino_bridge.h's hp41_arduino_bridge_send_display_state() for
// the Pico-side half of this protocol change. This Arduino now does the
// segment decode + pixel plotting itself (computeFramebufferFromState()
// below), using its own PROGMEM copy of the same tables
// (hp41_display_tables_avr.h) - logic ported from firmware/
// hp41_display_bridge.c, kept in sync by hand; if the two ever produce
// different pixels for the same display state, that's the bug to look
// for first.
const uint16_t DISPLAY_STATE_SIZE = HP41_NUM_CELLS * 3 + 2;
const uint16_t FRAME_PIXEL_SIZE = 576; // 144x32 1bpp - drawFrameFromRAM()'s input size

uint8_t stateBuf[DISPLAY_STATE_SIZE];
uint16_t stateBufPos = 0;
bool receivingState = false;
unsigned long stateStartTime = 0;
// A full state packet (40 bytes) takes well under 50ms to arrive at 9600
// baud (vs ~600ms for the old 578-byte framebuffer packet) - but the
// same resync problem from before still applies in principle if
// SoftwareSerial (bit-banged, known to be fragile especially while the
// Arduino is busy inside drawFrameFromRAM()) ever drops a byte mid-
// packet: stateBufPos would never reach DISPLAY_STATE_SIZE and
// receivingState would stay true forever, permanently misaligning every
// byte after that point. This timeout notices "stuck too long" and
// gives up, so the next valid sync byte can resynchronize things instead
// of wedging forever.
const unsigned long FRAME_TIMEOUT_MS = 1000;

uint8_t frameBuf[FRAME_PIXEL_SIZE]; // filled by computeFramebufferFromState(), fed to drawFrameFromRAM()

const uint8_t PIN_RS = 2;
const uint8_t PIN_E  = 3;
const uint8_t DATA_PINS[8] = {4, 5, 6, 7, 8, 9, 10, 11}; // DB0..DB7

// ---- Low-level ST7920 parallel transport ----------------------------------

// Direct AVR port writes for the 8 data-bus bits, instead of 8x
// digitalWrite() (each ~4-6us on Uno due to its pin->port lookup-table
// overhead - by far the dominant cost next to the mandatory post-write
// delay below). On an Uno, D4-D7 are PORTD bits 4-7 and D8-D11 are
// PORTB bits 0-3 (DATA_PINS[] order above), so a byte splits into one
// write per port. Masks preserve every other pin sharing each register:
// PORTD bits 0-3 are D0/D1 (USB Serial RX/TX - must never glitch) and
// D2/D3 (RS/E, written separately below); PORTB bits 4-7 are D12/D13
// (the SoftwareSerial Pico link) plus the crystal bits.
static inline void writeDataPins(uint8_t value) {
  PORTD = (PORTD & 0x0F) | ((value & 0x0F) << 4);
  PORTB = (PORTB & 0xF0) | ((value >> 4) & 0x0F);
}

static void writeByte(bool rs, uint8_t value) {
  digitalWrite(PIN_RS, rs ? HIGH : LOW);
  writeDataPins(value);
  // E is falling-edge triggered per datasheet: data must be valid before
  // the high->low transition. Even with the port-write speedup above,
  // digitalWrite(PIN_RS, ...) just ran and already dwarfs the datasheet's
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

// Same as drawBitmap(), but reads from a plain RAM buffer instead of
// PROGMEM - used for frames received live from the Pico bridge link,
// which land in ordinary SRAM (frameBuf below), not flash. pgm_read_byte()
// would misinterpret a normal RAM pointer, so this can't just call
// drawBitmap() with the RAM buffer - it needs direct array indexing.
void drawFrameFromRAM(const uint8_t *fb) {
  for (uint8_t y = 0; y < 32; y++) {
    writeCommand(0x80 | y);
    writeCommand(0x80);
    for (uint8_t word = 0; word < 9; word++) {
      writeData(fb[(y * 9 + word) * 2]);
      writeData(fb[(y * 9 + word) * 2 + 1]);
    }
  }
}

// ---- Raw display-state decode + plot (ported from firmware/hp41_display_bridge.c) -----

static inline void setPx(uint8_t *fb, int x, int y) {
  fb[y * 18 + x / 8] |= (uint8_t)(0x80 >> (x % 8)); // 18 = 144px / 8 bits-per-byte
}

// Same raw-code-to-ASCII decode as emu41gcc/display.c's static
// alpha41() / firmware/hp41_display_bridge.c's hp41_decode_ascii() -
// keep all three in sync if this ever changes. v is the raw HP-41
// display code: (lcd_c[i]<<8) | ((lcd_b[i]&3)<<4) | lcd_a[i].
static int decodeAscii(int v) {
  v &= 0x13f;
  if (v <= 0x1f) return v + '@';
  if (v <= 0x3f) {
    if (v == 0x2c) return '<';  // backward flying goose
    if (v == 0x2e) return '>';  // flying goose
    if (v == 0x3a) return '*';  // starburst
    return v;
  }
  if (v <= 0x105) return v - 0xa0;
  if (v <= 0x11f) {
    switch (v) {
      case 0x106: return '~';  // top bar
      case 0x107: return '\''; // append
      case 0x10c: return 'u';  // micro
      case 0x10d: return '#';  // different sign
      case 0x10e: return 's';  // sigma
      case 0x10f: return 'a';  // angle
      default:    return 'x';  // non-displayable
    }
  }
  return v - 0x120 + 'a' - 1;
}

static void plotSegment(uint8_t *fb, int cellX0, int segIndex) {
  uint8_t off = pgm_read_byte(&hp41_segment_pixel_offset[segIndex]);
  uint8_t cnt = pgm_read_byte(&hp41_segment_pixel_count[segIndex]);
  for (uint8_t k = 0; k < cnt; k++) {
    uint8_t px = pgm_read_byte(&hp41_segment_pixels[off + k].x);
    uint8_t py = pgm_read_byte(&hp41_segment_pixels[off + k].y);
    setPx(fb, cellX0 + px, py);
  }
}

static void plotAnnunciator(uint8_t *fb, int annIndex) {
  uint8_t off = pgm_read_byte(&hp41_annunciator_pixel_offset[annIndex]);
  uint8_t cnt = pgm_read_byte(&hp41_annunciator_pixel_count[annIndex]);
  for (uint8_t k = 0; k < cnt; k++) {
    uint8_t px = pgm_read_byte(&hp41_annunciator_pixels[off + k].x);
    uint8_t py = pgm_read_byte(&hp41_annunciator_pixels[off + k].y);
    setPx(fb, px, py); // absolute, not per-cell
  }
}

// state layout: lcd_a[12], lcd_b[12], lcd_c[12], lcd_ann low byte, high byte
// (see hp41_arduino_bridge_send_display_state()'s comment on the Pico side).
void computeFramebufferFromState(const uint8_t *state, uint8_t *fb) {
  memset(fb, 0, FRAME_PIXEL_SIZE);

  const uint8_t *a = state;
  const uint8_t *b = state + HP41_NUM_CELLS;
  const uint8_t *c = state + HP41_NUM_CELLS * 2;
  uint16_t ann = state[HP41_NUM_CELLS * 3] | ((uint16_t)state[HP41_NUM_CELLS * 3 + 1] << 8);

  for (int pos = 0; pos < HP41_NUM_CELLS; pos++) {
    // lcd_*[11] is the leftmost screen position, [0] the rightmost -
    // matches display.c/hp41_display_bridge.c.
    int i = (HP41_NUM_CELLS - 1) - pos;
    int v = (c[i] << 8) | ((b[i] & 3) << 4) | a[i];
    int ascii = decodeAscii(v) & 0x7f;
    int punct = b[i] >> 2;
    int cellX0 = pos * HP41_CELL_WIDTH_PX;

    uint16_t segbits = pgm_read_word(&hp41_char_segments[ascii]);
    for (int bit = 0; bit < 14; bit++) {
      if (segbits & (1u << bit)) plotSegment(fb, cellX0, bit);
    }

    switch (punct) {
      case 1: // period
        plotSegment(fb, cellX0, HP41_SEG_DOT_BOTTOM);
        break;
      case 2: // colon
        plotSegment(fb, cellX0, HP41_SEG_DOT_TOP);
        plotSegment(fb, cellX0, HP41_SEG_DOT_BOTTOM);
        break;
      case 3: // comma
        plotSegment(fb, cellX0, HP41_SEG_DOT_BOTTOM);
        plotSegment(fb, cellX0, HP41_SEG_COMMA_TAIL);
        break;
      default:
        break;
    }
  }

  for (int annIdx = 0; annIdx < HP41_NUM_ANNUNCIATORS; annIdx++) {
    uint16_t bit = pgm_read_word(&hp41_annunciator_bits[annIdx]);
    if (ann & bit) plotAnnunciator(fb, annIdx);
  }
}

// Drains any bytes waiting on the Pico link and assembles them into a
// state packet: [0xAA sync][DISPLAY_STATE_SIZE payload bytes][1
// XOR-checksum byte]. On a checksum match, decodes it into a pixel
// framebuffer and draws it; on a mismatch, silently drops it (keeps
// showing whatever was there before) rather than showing a corrupted
// image - matches firmware/hp41_arduino_bridge.c on the Pico side.
//
// Checked even when no new byte has arrived yet (that's why this isn't
// nested inside the availability loop below) - otherwise a truly stuck
// receiver (no more bytes ever coming) could never notice it should
// give up and resync.
void pollPicoLink() {
  if (receivingState && (millis() - stateStartTime > FRAME_TIMEOUT_MS)) {
    receivingState = false; // gave up waiting - ready for a fresh sync byte
  }

  while (picoLink.available()) {
    uint8_t b = (uint8_t)picoLink.read();

    if (!receivingState) {
      if (b == FRAME_SYNC) {
        receivingState = true;
        stateStartTime = millis();
        stateBufPos = 0;
      }
      // else: not in sync yet, ignore stray bytes
    } else if (stateBufPos < DISPLAY_STATE_SIZE) {
      stateBuf[stateBufPos++] = b;
    } else {
      // this byte is the checksum
      uint8_t checksum = 0;
      for (uint16_t i = 0; i < DISPLAY_STATE_SIZE; i++) checksum ^= stateBuf[i];
      // TEMPORARY diagnostic (investigating the "blank screen, catches up
      // next key" bug - see CLAUDE.md): the Pico can fire off several
      // frames within ~1-2ms of each other (multiple fdsp events during
      // one ROM operation), but drawFrameFromRAM() below takes ~30-50ms
      // of real time per frame - if SoftwareSerial (bit-banged, no flow
      // control with the Pico) drops/corrupts bytes for a frame that
      // arrives while still busy drawing the previous one, this print
      // will show a checksum mismatch here, confirming that as the cause
      // rather than anything ROM-side.
      static uint32_t frameNum = 0;
      frameNum++;
      // [Tms] prefix, same idea as the Pico's own dbg() helper in
      // main.c - lets the two independent serial logs be lined up
      // chronologically (the two boards' clocks aren't synchronized
      // with each other, so this only works if both are power-cycled
      // at roughly the same moment - see CLAUDE.md).
      if (checksum == b) {
        Serial.print(F("["));
        Serial.print(millis());
        Serial.print(F("ms] frame "));
        Serial.print(frameNum);
        Serial.print(F(": OK, drawing (checksum 0x"));
        Serial.print(checksum, HEX);
        Serial.println(F(")"));
        computeFramebufferFromState(stateBuf, frameBuf);
        // TEMPORARY test: SoftwareSerial's receive is interrupt-driven
        // (pin-change ISR sampling bits) - if a receive interrupt fires
        // mid-write during drawFrameFromRAM()'s precisely-timed GDRAM
        // byte-write loop (which happens naturally when a burst's next
        // frame starts arriving while this draw is still in progress),
        // it could disrupt the actual LCD bus write even though the
        // packet's own checksum (verified before this point) is
        // unaffected - checksums only prove reception was clean, not that
        // the subsequent write to the LCD went uninterrupted. Testing
        // whether blocking interrupts for the duration of the write
        // fixes the "blank during a rapid burst" symptom.
        noInterrupts();
        drawFrameFromRAM(frameBuf);
        interrupts();
      } else {
        Serial.print(F("["));
        Serial.print(millis());
        Serial.print(F("ms] frame "));
        Serial.print(frameNum);
        Serial.print(F(": CHECKSUM MISMATCH (got 0x"));
        Serial.print(b, HEX);
        Serial.print(F(", expected 0x"));
        Serial.print(checksum, HEX);
        Serial.println(F(") - dropped, keeping previous image"));
      }
      receivingState = false;
    }
  }
}

// ---- Sketch entry points ---------------------------------------------------

void setup() {
  Serial.begin(9600);
  Serial.print(F("["));
  Serial.print(millis());
  Serial.println(F("ms] NHD-14432WG display bridge (parallel). Listening for live Pico frames. (1/2/3 mockup switcher currently disabled - see loop().)"));

  picoLink.begin(9600); // must match firmware/hp41_arduino_bridge.c's BRIDGE_BAUD_RATE

  st7920Init();
  gdramClear();
  // Boot-time self-test: light every segment, like a real segmented-LCD
  // power-on display test (and it's maximally distinct from any real
  // Pico frame, so a stuck-on-this-image display is easy to spot).
  drawBitmap(bmp_all_segments);
}

void loop() {
  // 1/2/3 test-pattern switcher disabled while live Pico frames are the
  // real display source - a stray keypress in the Arduino's own Serial
  // monitor could otherwise overwrite real emulator output. Left in
  // place (commented, not deleted) since it's a handy standalone display
  // sanity check independent of the Pico link.
  // if (Serial.available()) {
  //   char c = Serial.read();
  //   switch (c) {
  //     case '1': gdramClear(); drawBitmap(bmp_00000);        Serial.println(F("-> 00000"));       break;
  //     case '2': gdramClear(); drawBitmap(bmp_memory_lost);  Serial.println(F("-> MEMORY LOST"));  break;
  //     case '3': gdramClear(); drawBitmap(bmp_all_segments); Serial.println(F("-> ALL SEGMENTS")); break;
  //   }
  // }

  pollPicoLink();
}
