#include "st7920.h"
#include "pins.h"

#include "pico/stdlib.h"

// --- Low-level 3-wire serial bus ----------------------------------------
//
// R/W is fixed to 0 (write) in every transaction - we never read the busy
// flag, same as the parallel wiring this replaced - so every write is
// followed by a fixed delay instead. Per the ST7920 datasheet, most
// instructions/data writes complete within ~72us; Clear Display and Return
// Home need ~1.6ms. These are conservative pads, not hardware-timed (this
// part is unchanged from the parallel version - the ST7920's internal
// command execution time doesn't depend on which bus reaches it).
//
// The serial protocol itself (sync byte + two nibble bytes, MSB-first,
// SCLK idle low) is NOT from the NHD-14432WG-BTFH-VT datasheet - it
// doesn't document serial timing at all, only the pin names. This is the
// standard ST7920 serial protocol used near-universally elsewhere. CS
// polarity, however, IS from that datasheet's text (active low) - see
// pins.h's big warning: this contradicts common ST7920 serial-module
// practice (active high) and is unverified. Flip LCD_CS_ACTIVE_LOW below
// first if the display doesn't respond at all.

#define LCD_CS_ACTIVE_LOW 0

#define BIT_DELAY_US 5 // conservative; nowhere near this interface's real speed limit
// (tried 100us to rule out the level shifter's rise time as the cause of
// a blank display - no change, reverted back to 5us to keep this a
// single-variable experiment; see CLAUDE.md for the full history)

static inline void cs_select(void) {
    gpio_put(PIN_LCD_CS, LCD_CS_ACTIVE_LOW ? 0 : 1);
}

static inline void cs_deselect(void) {
    gpio_put(PIN_LCD_CS, LCD_CS_ACTIVE_LOW ? 1 : 0);
}

// Shifts one byte out MSB-first. SCLK idles low; SID is set up while SCLK
// is low and (per standard ST7920 serial practice) sampled by the
// controller on SCLK's rising edge - equivalent to SPI mode 0.
static void shift_out_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        gpio_put(PIN_LCD_SCLK, 0);
        gpio_put(PIN_LCD_SID, (b >> i) & 1);
        busy_wait_us(BIT_DELAY_US);
        gpio_put(PIN_LCD_SCLK, 1);
        busy_wait_us(BIT_DELAY_US);
    }
    gpio_put(PIN_LCD_SCLK, 0);
}

// Each ST7920 serial write is 3 bytes: a sync byte encoding RS/RW
// (11111 RW RS 0 - RW is always 0 here, write-only), then the data byte
// split into two nibble-in-upper-bits bytes (high nibble, then low
// nibble shifted up) - the controller reassembles them internally. This
// is the standard ST7920 serial framing, not specific to this module.
static void write_byte(bool is_data, uint8_t value, uint32_t delay_us) {
    uint8_t sync = 0xF8 | (is_data ? 0x02 : 0x00);

    cs_select();
    shift_out_byte(sync);
    shift_out_byte(value & 0xF0);
    shift_out_byte((uint8_t)(value << 4));
    cs_deselect();

    busy_wait_us(delay_us);
}

static inline void write_cmd(uint8_t cmd) {
    write_byte(false, cmd, 72);
}

static inline void write_data(uint8_t data) {
    write_byte(true, data, 72);
}

// --- ST7920 instructions -----------------------------------------------

#define CMD_FUNCTION_SET_BASIC             0x30
#define CMD_FUNCTION_SET_EXTENDED          0x34
#define CMD_FUNCTION_SET_EXTENDED_GRAPHIC  0x36
#define CMD_DISPLAY_ON                     0x0C
#define CMD_ENTRY_MODE                     0x06
#define CMD_CLEAR                          0x01
#define CMD_GDRAM_ADDR_BASE                0x80

static void set_gdram_addr(uint8_t vertical, uint8_t horizontal) {
    write_cmd(CMD_GDRAM_ADDR_BASE | (vertical & 0x3F));
    write_cmd(CMD_GDRAM_ADDR_BASE | (horizontal & 0x0F));
}

void st7920_init(void) {
    gpio_init(PIN_LCD_CS);
    gpio_init(PIN_LCD_SID);
    gpio_init(PIN_LCD_SCLK);
    gpio_set_dir(PIN_LCD_CS, GPIO_OUT);
    gpio_set_dir(PIN_LCD_SID, GPIO_OUT);
    gpio_set_dir(PIN_LCD_SCLK, GPIO_OUT);
    gpio_put(PIN_LCD_SCLK, 0);
    cs_deselect();

    sleep_ms(40); // power-on delay per datasheet (>40ms)

    // Timing below follows the real ST7920 controller datasheet's own
    // power-on init flowchart (ST7920.pdf p.34) rather than the general
    // instruction-exec-time table used elsewhere in this file - the
    // flowchart calls for extra margin at exactly these steps, and that
    // same datasheet states "ST7920 has no internal instruction buffer
    // area": a command sent before the controller finishes the previous
    // one is silently DROPPED, not queued. Under-waiting here can
    // silently break the rest of the init chain with no way to detect it
    // (no busy-flag read is possible - R/W is fixed low in this design).
    write_byte(false, CMD_FUNCTION_SET_BASIC, 150);  // datasheet: >100us
    write_byte(false, CMD_FUNCTION_SET_BASIC, 50);   // datasheet: >37us
    write_byte(false, CMD_DISPLAY_ON, 150);          // datasheet: >100us
    write_byte(false, CMD_CLEAR, 12000);             // datasheet: >10ms (the previous 1.6us here was CMD_CLEAR's general steady-state exec time, not this flowchart's own larger figure)
    write_cmd(CMD_ENTRY_MODE);

    write_cmd(CMD_FUNCTION_SET_EXTENDED);
    write_cmd(CMD_FUNCTION_SET_EXTENDED_GRAPHIC);
}

void st7920_clear(void) {
    for (uint8_t y = 0; y < LCD_HEIGHT_PX; y++) {
        set_gdram_addr(y, 0);
        for (uint8_t w = 0; w < LCD_BYTES_PER_ROW / 2; w++) {
            write_data(0x00);
            write_data(0x00);
        }
    }
}

// Pixel (144x32) -> GDRAM address mapping.
//
// *** CONFIRMED against real hardware - this is not a guess. ***
// Cross-checked against Arduino NHD14432/NHD14432_POC (a separate,
// physically-tested-working reference implementation for this same
// panel, predating the Pico port): vertical address 0-31 maps directly
// to y=0-31, no bank-select trick needed - this panel is exactly one
// "half" of the standard ST7920 128x64 addressing convention. Each row
// is 9 words (LCD_BYTES_PER_ROW/2 = 18/2 = 9), covering the full 144px
// width in one contiguous burst after a single address-set. The
// previous version of this function guessed a "fold columns 128-143
// into the vertical+32 bank" scheme that was never actually confirmed -
// that guess was wrong; this replaces it.
void st7920_draw_frame(const uint8_t *fb) {
    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        const uint8_t *row = fb + (size_t)y * LCD_BYTES_PER_ROW;

        set_gdram_addr((uint8_t)y, 0);
        for (int w = 0; w < LCD_BYTES_PER_ROW / 2; w++) {
            write_data(row[w * 2]);
            write_data(row[w * 2 + 1]);
        }
    }
}
