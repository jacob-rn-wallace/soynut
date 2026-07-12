#include "bitmaps.h"

#include <string.h>

uint8_t bitmap_all_on[LCD_FB_SIZE];
uint8_t bitmap_checkerboard[LCD_FB_SIZE];
uint8_t bitmap_border[LCD_FB_SIZE];

static inline void set_px(uint8_t *fb, int x, int y) {
    fb[y * LCD_BYTES_PER_ROW + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

void bitmaps_init(void) {
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
}
