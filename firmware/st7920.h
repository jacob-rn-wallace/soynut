/**
 * @file st7920.h
 * @brief Low-level 8-bit parallel driver for the NHD-14432WG-BTFH-VT
 *        (ST7920 controller) 144x32 graphic LCD - the active direct
 *        Pico->LCD display path.
 */
#ifndef SOYNUT_ST7920_H
#define SOYNUT_ST7920_H

#include <stdint.h>
#include <stddef.h>

// NHD-14432WG-BTFH-VT: 144 x 32 graphic LCD, ST7920 controller, wired
// 8-bit parallel (see pins.h / CLAUDE.md).

#define LCD_WIDTH_PX   144
#define LCD_HEIGHT_PX  32
#define LCD_BYTES_PER_ROW (LCD_WIDTH_PX / 8)                     // 18
#define LCD_FB_SIZE       (LCD_BYTES_PER_ROW * LCD_HEIGHT_PX)    // 576

/**
 * @brief Configure GPIOs and run the ST7920 power-on init sequence.
 *
 * Must be called once before st7920_clear()/st7920_draw_frame().
 */
void st7920_init(void);

/**
 * @brief Clear the controller's GDRAM directly (not just a local framebuffer).
 */
void st7920_clear(void);

/**
 * @brief Push a full framebuffer to the controller's GDRAM.
 *
 * @param fb LCD_FB_SIZE bytes, MSB-first per row, row-major, 1bpp.
 */
void st7920_draw_frame(const uint8_t *fb);

#endif // SOYNUT_ST7920_H
