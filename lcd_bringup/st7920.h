#ifndef LCD_BRINGUP_ST7920_H
#define LCD_BRINGUP_ST7920_H

#include <stdint.h>
#include <stdbool.h>

#define LCD_WIDTH_PX   144
#define LCD_HEIGHT_PX  32
#define LCD_BYTES_PER_ROW (LCD_WIDTH_PX / 8)                     // 18
#define LCD_FB_SIZE       (LCD_BYTES_PER_ROW * LCD_HEIGHT_PX)    // 576

// Just configures the GPIOs (RS, E, DB0-7) as outputs in their idle
// state - no LCD commands sent. Safe to call repeatedly.
void st7920_gpio_init(void);

// Sends the actual ST7920 power-on init command sequence (function set,
// display on, clear, entry mode, extended/graphic mode) - per the real
// ST7920 datasheet's own init flowchart timing, not just the general
// instruction-exec-time table. Call st7920_gpio_init() first at least
// once.
void st7920_run_init_sequence(void);

// Fills the controller's GDRAM with a repeating byte pattern (e.g. 0x00
// for blank, 0xFF for solid-on) - simplest possible "did ANYTHING land on
// the glass" test, no framebuffer/font logic involved at all.
void st7920_fill(uint8_t pattern);

// Pushes `fb` (LCD_FB_SIZE bytes, MSB-first per row, row-major, 1bpp) to
// the controller's GDRAM.
void st7920_draw_frame(const uint8_t *fb);

#endif // LCD_BRINGUP_ST7920_H
