/**
 * @file bitmaps.h
 * @brief Static test frames for display hardware/wiring bring-up.
 *
 * Not called from the normal firmware flow (see main.c) - kept as a
 * hardware-debugging aid to isolate a wiring problem from a software
 * one if the real display output is ever dark or wrong.
 */
#ifndef SOYNUT_BITMAPS_H
#define SOYNUT_BITMAPS_H

#include <stdint.h>
#include "st7920.h"

/** All 576 bytes 0xFF - full-screen solid fill. Populated by bitmaps_init(). */
extern uint8_t bitmap_all_on[LCD_FB_SIZE];
/** 4x4px checkerboard pattern. Populated by bitmaps_init(). */
extern uint8_t bitmap_checkerboard[LCD_FB_SIZE];
/** 1px border around the full 144x32 frame. Populated by bitmaps_init(). */
extern uint8_t bitmap_border[LCD_FB_SIZE];

/**
 * @brief Compute the three test bitmaps above. Call once before using them.
 */
void bitmaps_init(void);

#endif // SOYNUT_BITMAPS_H
