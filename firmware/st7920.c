#include "st7920.h"
#include "pins.h"

#include <assert.h>

#include "pico/stdlib.h"

// --- Low-level 8-bit parallel bus ---------------------------------------
//
// Confirmed directly against the NHD-14432WG-BTFH-VT datasheet's own
// "Pin Description - Parallel Interface" table and its 8051 reference
// code (reference-material/datasheets/NHD-14432WG-BTFH-VT.pdf): RS
// selects instruction(0)/data(1), R/W is fixed 0 (write-only design -
// tied directly to GND in hardware, no Pico pin at all, so it's never
// touched here), and E is FALLING-EDGE triggered - the datasheet's own
// example (Wcom()/Wdata()) sets RS + the data bus, raises E, waits
// briefly, then drops E to actually latch the byte. That's exactly the
// sequence below. Verified working on real hardware via lcd_bringup/
// (solid-fill + checkerboard test patterns, both rendered correctly)
// before being adopted here - see pins.h's comment for that history.
//
// The GDRAM addressing and command sequence (init timing, address
// mapping) are unchanged from the previously-dormant 3-wire-serial
// version of this file - those are ST7920-controller-level facts, not
// bus-specific, and were already cross-checked against a separately
// hardware-validated reference (see CLAUDE.md). Only this file's bus
// transport layer (write_byte's actual pin wiggling) changed.

static const uint DATA_PINS[8] = {
    PIN_LCD_DB0, PIN_LCD_DB1, PIN_LCD_DB2, PIN_LCD_DB3,
    PIN_LCD_DB4, PIN_LCD_DB5, PIN_LCD_DB6, PIN_LCD_DB7,
};

#define BUS_DELAY_US 2 // generous vs. the datasheet's ns-scale address/data setup and E pulse-width figures

static void write_byte(bool is_data, uint8_t value, uint32_t delay_us) {
    /* E must already be low when a new transaction begins - the
     * datasheet's falling-edge latch only means something relative to a
     * preceding high state, and every prior write_byte() call (including
     * st7920_init()'s own initial gpio_put()) leaves it low on exit. */
    assert(gpio_get(PIN_LCD_E) == 0);

    gpio_put(PIN_LCD_RS, is_data ? 1 : 0);
    for (int i = 0; i < 8; i++) {
        gpio_put(DATA_PINS[i], (value >> i) & 1);
    }
    busy_wait_us(BUS_DELAY_US); // address/data setup before E rises

    gpio_put(PIN_LCD_E, 1);
    busy_wait_us(BUS_DELAY_US); // E pulse width / data setup before E falls
    gpio_put(PIN_LCD_E, 0);     // falling edge - this is what actually latches the byte
    busy_wait_us(BUS_DELAY_US); // data hold time after E falls
    assert(gpio_get(PIN_LCD_E) == 0); // leaves E low, per the precondition above

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
    assert(vertical < LCD_HEIGHT_PX); /* every real caller passes a y coordinate */
    assert(horizontal <= 0x0F);       /* 4-bit field per the datasheet's command layout */
    write_cmd(CMD_GDRAM_ADDR_BASE | (vertical & 0x3F));
    write_cmd(CMD_GDRAM_ADDR_BASE | (horizontal & 0x0F));
}

void st7920_init(void) {
    assert(sizeof(DATA_PINS) / sizeof(DATA_PINS[0]) == 8);
    gpio_init(PIN_LCD_RS);
    gpio_init(PIN_LCD_E);
    gpio_set_dir(PIN_LCD_RS, GPIO_OUT);
    gpio_set_dir(PIN_LCD_E, GPIO_OUT);
    for (int i = 0; i < 8; i++) {
        gpio_init(DATA_PINS[i]);
        gpio_set_dir(DATA_PINS[i], GPIO_OUT);
    }
    gpio_put(PIN_LCD_E, 0);
    assert(gpio_get(PIN_LCD_E) == 0);

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
    assert(LCD_BYTES_PER_ROW % 2 == 0); /* the write-two-bytes-at-a-time loop below assumes this */
    assert(LCD_HEIGHT_PX > 0);
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
// width in one contiguous burst after a single address-set.
void st7920_draw_frame(const uint8_t *fb) {
    assert(fb != NULL);
    assert(LCD_BYTES_PER_ROW % 2 == 0); /* see st7920_clear()'s note */
    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        const uint8_t *row = fb + (size_t)y * LCD_BYTES_PER_ROW;

        set_gdram_addr((uint8_t)y, 0);
        for (int w = 0; w < LCD_BYTES_PER_ROW / 2; w++) {
            write_data(row[w * 2]);
            write_data(row[w * 2 + 1]);
        }
    }
}
