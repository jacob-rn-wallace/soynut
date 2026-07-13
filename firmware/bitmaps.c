/**
 * @file bitmaps.c
 * @brief See bitmaps.h - static test frames for display bring-up.
 */

#include "bitmaps.h"

#include <assert.h>
#include <string.h>

uint8_t bitmap_all_on[LCD_FB_SIZE];
uint8_t bitmap_checkerboard[LCD_FB_SIZE];
uint8_t bitmap_border[LCD_FB_SIZE];

/**
 * @brief Set one pixel in a 1bpp, row-major, MSB-first framebuffer.
 * @param fb Framebuffer to modify, at least LCD_FB_SIZE bytes.
 * @param x  Absolute column, 0 to LCD_WIDTH_PX-1.
 * @param y  Absolute row, 0 to LCD_HEIGHT_PX-1.
 */
static inline void set_px(uint8_t *fb, int x, int y) {
    assert(fb != NULL);
    assert(x >= 0 && x < LCD_WIDTH_PX && y >= 0 && y < LCD_HEIGHT_PX);
    fb[y * LCD_BYTES_PER_ROW + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

/**
 * @brief Compute bitmap_all_on/bitmap_checkerboard/bitmap_border; see the header.
 */
void bitmaps_init(void) {
    assert(LCD_FB_SIZE == LCD_BYTES_PER_ROW * LCD_HEIGHT_PX);
    memset(bitmap_all_on, 0xFF, LCD_FB_SIZE);

    memset(bitmap_checkerboard, 0, LCD_FB_SIZE);
    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        for (int x = 0; x < LCD_WIDTH_PX; x++) {
            if (((x / 4) + (y / 4)) % 2 == 0) {
                set_px(bitmap_checkerboard, x, y);
            }
        }
    }

    memset(bitmap_border, 0, LCD_FB_SIZE);
    for (int x = 0; x < LCD_WIDTH_PX; x++) {
        set_px(bitmap_border, x, 0);
        set_px(bitmap_border, x, LCD_HEIGHT_PX - 1);
    }
    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        set_px(bitmap_border, 0, y);
        set_px(bitmap_border, LCD_WIDTH_PX - 1, y);
    }
    assert(bitmap_border[0] & 0x80); /* top-left corner (0,0) is lit */
}
