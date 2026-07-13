/**
 * @file st7920.c
 * @brief Standalone ST7920 parallel bus driver for lcd_bringup - see
 *        st7920.h for the public API.
 */

#include "st7920.h"
#include "pins.h"

#include <assert.h>

#include "pico/stdlib.h"

// --- Low-level 8-bit parallel bus ---------------------------------------
//
// Confirmed directly against the NHD-14432WG-BTFH-VT datasheet's own
// "Pin Description - Parallel Interface" table and its 8051 reference
// code (both in reference-material/datasheets/NHD-14432WG-BTFH-VT.pdf):
// RS selects instruction(0)/data(1), R/W is fixed 0 (write-only design -
// tied directly to GND in hardware, no Pico pin at all, so it's never
// touched here), and E is FALLING-EDGE triggered - the datasheet's own
// example (Wcom()/Wdata()) sets RS + the data bus, raises E, waits
// briefly, then drops E to actually latch the byte. That's exactly the
// sequence below.
//
// The GDRAM addressing and command sequence (init timing, GDRAM address
// mapping) are unchanged from the previously-dormant 3-wire-serial
// version of this file - those are ST7920-controller-level facts, not
// bus-specific, and were already cross-checked against a separately
// hardware-validated reference (see ../CLAUDE.md). Only this file's bus
// transport layer (write_byte/shift_out_byte equivalent) changed.

static const uint DATA_PINS[8] = {
    PIN_LCD_DB0, PIN_LCD_DB1, PIN_LCD_DB2, PIN_LCD_DB3,
    PIN_LCD_DB4, PIN_LCD_DB5, PIN_LCD_DB6, PIN_LCD_DB7,
};

#define BUS_DELAY_US 2 // generous vs. the datasheet's ns-scale address/data setup and E pulse-width figures

/**
 * @brief Latch one byte onto the ST7920's 8-bit parallel bus.
 *
 * @param is_data Whether this is a data byte (true) or command (false); drives RS.
 * @param value   The byte to write.
 * @param delay_us Extra settle time to wait after the write completes.
 */
static void write_byte(bool is_data, uint8_t value, uint32_t delay_us) {
    /* E must already be low when a new transaction begins - see
     * ../firmware/st7920.c's identical note; the two files share this
     * bus-transport design. */
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
    assert(gpio_get(PIN_LCD_E) == 0);

    busy_wait_us(delay_us);
}

/** @brief Send one command byte with the datasheet's default 72us settle time. */
static inline void write_cmd(uint8_t cmd) {
    write_byte(false, cmd, 72);
}

/** @brief Send one data byte with the datasheet's default 72us settle time. */
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

/**
 * @brief Set the ST7920's GDRAM write address.
 * @param vertical   Row (y), 0 to LCD_HEIGHT_PX-1.
 * @param horizontal Word offset within the row (0-8, 9 words/row).
 */
static void set_gdram_addr(uint8_t vertical, uint8_t horizontal) {
    assert(vertical < LCD_HEIGHT_PX);
    assert(horizontal <= 0x0F);
    write_cmd(CMD_GDRAM_ADDR_BASE | (vertical & 0x3F));
    write_cmd(CMD_GDRAM_ADDR_BASE | (horizontal & 0x0F));
}

/**
 * @brief Configure the GPIOs as outputs in their idle state; see the header.
 */
void st7920_gpio_init(void) {
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
}

/**
 * @brief Run the ST7920 power-on init command sequence; see the header.
 */
void st7920_run_init_sequence(void) {
    sleep_ms(40); // power-on delay per datasheet (>40ms)

    // Timing here follows the real ST7920 datasheet's own power-on init
    // flowchart (ST7920.pdf p.34), which calls for more margin at these
    // specific steps than the general instruction-exec-time table used
    // elsewhere. That same datasheet states "ST7920 has no internal
    // instruction buffer area" - a command sent before the controller
    // finishes the previous one is silently DROPPED, not queued, and
    // there's no busy-flag read possible here (R/W fixed low) to detect
    // it.
    write_byte(false, CMD_FUNCTION_SET_BASIC, 150);  // datasheet: >100us
    write_byte(false, CMD_FUNCTION_SET_BASIC, 50);   // datasheet: >37us
    write_byte(false, CMD_DISPLAY_ON, 150);          // datasheet: >100us
    write_byte(false, CMD_CLEAR, 12000);             // datasheet: >10ms
    write_cmd(CMD_ENTRY_MODE);

    write_cmd(CMD_FUNCTION_SET_EXTENDED);
    write_cmd(CMD_FUNCTION_SET_EXTENDED_GRAPHIC);
}

/**
 * @brief Fill the controller's GDRAM with a repeating byte pattern; see the header.
 * @param pattern Byte to repeat across all of GDRAM.
 */
void st7920_fill(uint8_t pattern) {
    assert(LCD_BYTES_PER_ROW % 2 == 0); /* the write-two-bytes-at-a-time loop below assumes this */
    assert(LCD_HEIGHT_PX > 0);
    for (uint8_t y = 0; y < LCD_HEIGHT_PX; y++) {
        set_gdram_addr(y, 0);
        for (uint8_t w = 0; w < LCD_BYTES_PER_ROW / 2; w++) {
            write_data(pattern);
            write_data(pattern);
        }
    }
}

// Pixel (144x32) -> GDRAM address mapping: vertical address 0-31 maps
// directly to y=0-31, no bank-select fold - confirmed against the
// separately hardware-validated Arduino reference (see main project's
// CLAUDE.md). Each row is 9 words (LCD_BYTES_PER_ROW/2), one contiguous
// burst per row after a single address-set.
/**
 * @brief Push a full framebuffer to the controller's GDRAM; see the header.
 * @param fb LCD_FB_SIZE bytes, MSB-first per row, row-major, 1bpp.
 */
void st7920_draw_frame(const uint8_t *fb) {
    assert(fb != NULL);
    assert(LCD_BYTES_PER_ROW % 2 == 0);
    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        const uint8_t *row = fb + (size_t)y * LCD_BYTES_PER_ROW;

        set_gdram_addr((uint8_t)y, 0);
        for (int w = 0; w < LCD_BYTES_PER_ROW / 2; w++) {
            write_data(row[w * 2]);
            write_data(row[w * 2 + 1]);
        }
    }
}
