#include "st7920.h"
#include "pins.h"

#include "pico/stdlib.h"

// --- Low-level 3-wire serial bus ----------------------------------------
//
// Protocol verified bit-for-bit against the real ST7920 controller
// datasheet (ST7920.pdf, repo root) - the "Serial interface" section's
// timing diagram (p.26) and the 8051 reference routines (p.27), not just
// "standard practice" as previously assumed. Sync byte, nibble framing,
// and CS-spans-all-24-bits behavior all match exactly.
//
// CS polarity: that same datasheet's pin table (p.7) and timing diagram
// (p.44, 8051 code p.27's "SETB CS ... CLR CS") both show CS is
// ACTIVE-HIGH ("1: chip enabled") - this contradicts the NHD-14432WG
// module's own datasheet text ("Active LOW Chip Select"). Runtime
// toggleable here (see st7920_set_cs_active_low()) so both can be tried
// without a rebuild.

static bool cs_active_low = false; // matches the real ST7920 datasheet's "CS=1 enables"

void st7920_set_cs_active_low(bool active_low) {
    cs_active_low = active_low;
}

bool st7920_get_cs_active_low(void) {
    return cs_active_low;
}

#define BIT_DELAY_US 5

static inline void cs_select(void) {
    gpio_put(PIN_LCD_CS, cs_active_low ? 0 : 1);
}

static inline void cs_deselect(void) {
    gpio_put(PIN_LCD_CS, cs_active_low ? 1 : 0);
}

// Shifts one byte out MSB-first. SCLK idles low; SID is set up while SCLK
// is low and sampled by the controller on SCLK's rising edge (SPI mode 0
// equivalent) - matches ST7920.pdf p.44's timing diagram.
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

// Each ST7920 serial write is 3 bytes: sync (11111 RW RS 0, RW always 0 -
// write only), then the data byte split into two nibble-in-upper-bits
// bytes. Matches ST7920.pdf p.26/27 exactly - verified bit position by
// bit position against the 8051 reference routine.
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

void st7920_gpio_init(void) {
    gpio_init(PIN_LCD_CS);
    gpio_init(PIN_LCD_SID);
    gpio_init(PIN_LCD_SCLK);
    gpio_set_dir(PIN_LCD_CS, GPIO_OUT);
    gpio_set_dir(PIN_LCD_SID, GPIO_OUT);
    gpio_set_dir(PIN_LCD_SCLK, GPIO_OUT);
    gpio_put(PIN_LCD_SCLK, 0);
    cs_deselect();
}

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

void st7920_fill(uint8_t pattern) {
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
