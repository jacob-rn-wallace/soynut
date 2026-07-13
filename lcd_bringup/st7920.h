/**
 * @file st7920.h
 * @brief Standalone ST7920 parallel driver for LCD bring-up/wiring
 *        verification - no dependency on emu41gcc/the ROM/the Arduino
 *        bridge. See firmware/st7920.h for the equivalent driver used
 *        by the full firmware.
 */
#ifndef LCD_BRINGUP_ST7920_H
#define LCD_BRINGUP_ST7920_H

#include <stdint.h>
#include <stdbool.h>

#define LCD_WIDTH_PX   144
#define LCD_HEIGHT_PX  32
#define LCD_BYTES_PER_ROW (LCD_WIDTH_PX / 8)                     // 18
#define LCD_FB_SIZE       (LCD_BYTES_PER_ROW * LCD_HEIGHT_PX)    // 576

/**
 * @brief Configure the GPIOs (RS, E, DB0-7) as outputs in their idle state.
 *
 * No LCD commands sent. Safe to call repeatedly.
 */
void st7920_gpio_init(void);

/**
 * @brief Run the ST7920 power-on init command sequence.
 *
 * Function set, display on, clear, entry mode, extended/graphic mode -
 * per the real ST7920 datasheet's own init flowchart timing, not just
 * the general instruction-exec-time table. Call st7920_gpio_init()
 * first at least once.
 */
void st7920_run_init_sequence(void);

/**
 * @brief Fill the controller's GDRAM with a repeating byte pattern.
 *
 * Simplest possible "did ANYTHING land on the glass" test, no
 * framebuffer/font logic involved at all.
 *
 * @param pattern Byte to repeat across all of GDRAM (e.g. 0x00 for
 *                blank, 0xFF for solid-on).
 */
void st7920_fill(uint8_t pattern);

/**
 * @brief Push a full framebuffer to the controller's GDRAM.
 *
 * @param fb LCD_FB_SIZE bytes, MSB-first per row, row-major, 1bpp.
 */
void st7920_draw_frame(const uint8_t *fb);

#endif // LCD_BRINGUP_ST7920_H
