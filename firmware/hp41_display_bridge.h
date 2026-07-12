#ifndef SOYNUT_HP41_DISPLAY_BRIDGE_H
#define SOYNUT_HP41_DISPLAY_BRIDGE_H

#include <stdint.h>

// Renders the Nut CPU's LCD shift registers (lcd_a/b/c/lcd_ann, owned by
// emu41gcc/display.c) into an ST7920-format framebuffer (see st7920.h:
// LCD_FB_SIZE bytes, 1bpp, row-major, MSB-first per row). Pure logic, no
// hardware access - safe to call/test on a host build.
void hp41_display_compute_framebuffer(uint8_t *fb);

// hp41_display_compute_framebuffer() followed by st7920_draw_frame().
// Call whenever the emulator's `fdsp` flag is set (see nutcpu.h) - the
// caller is responsible for clearing fdsp afterwards, same as
// emu41gcc's own reference main loop does (see CLAUDE.md "ROM wiring" /
// emu41.c's traite_display() callers).
void hp41_display_render(void);

#endif // SOYNUT_HP41_DISPLAY_BRIDGE_H
