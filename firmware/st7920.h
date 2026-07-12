#ifndef SOYNUT_ST7920_H
#define SOYNUT_ST7920_H

#include <stdint.h>
#include <stddef.h>

// NHD-14432WG-BTFH-VT: 144 x 32 graphic LCD, ST7920 controller, wired
// 8-bit parallel (see pins.h / HANDOFF.md).

#define LCD_WIDTH_PX   144
#define LCD_HEIGHT_PX  32
#define LCD_BYTES_PER_ROW (LCD_WIDTH_PX / 8)                     // 18
#define LCD_FB_SIZE       (LCD_BYTES_PER_ROW * LCD_HEIGHT_PX)    // 576

void st7920_init(void);

// Clears the controller's GDRAM directly (not just the local framebuffer).
void st7920_clear(void);

// Pushes `fb` (LCD_FB_SIZE bytes, MSB-first per row, row-major, 1bpp) to
// the controller's GDRAM.
void st7920_draw_frame(const uint8_t *fb);

#endif // SOYNUT_ST7920_H
