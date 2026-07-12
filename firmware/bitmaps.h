#ifndef SOYNUT_BITMAPS_H
#define SOYNUT_BITMAPS_H

#include <stdint.h>
#include "st7920.h"

// Static test frames for display bring-up (hardware/wiring verification,
// before the Nut CPU emulator's display_wr() is wired up).
// Call bitmaps_init() once before using these.
extern uint8_t bitmap_all_on[LCD_FB_SIZE];
extern uint8_t bitmap_checkerboard[LCD_FB_SIZE];
extern uint8_t bitmap_border[LCD_FB_SIZE];

void bitmaps_init(void);

#endif // SOYNUT_BITMAPS_H
