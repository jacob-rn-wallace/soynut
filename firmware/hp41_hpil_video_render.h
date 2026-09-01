/**
 * @file hp41_hpil_video_render.h
 * @brief Renders the HP-IL video interface's 32x16 character grid
 *        (hp41_hpil_video_bridge.h) into a Sharp Memory LCD
 *        framebuffer, for the `quad/` firmware target.
 *
 * Deliberately its own small module, not folded into
 * hp41_quad_display_bridge.c/h: that file's own scope is the classic
 * HP-41 display (stack/classic-line views, hp41_quad_font_table.h's
 * 12-column font) - this one renders an unrelated peripheral's own
 * screen (ilvideo-native's 32x16 grid, its own 8x14 font, its own
 * geometry) into the same physical panel. `quad/main.c` is what
 * chooses which one to call for the currently-selected view; neither
 * bridge needs to know the other exists.
 */
#ifndef SOYNUT_HP41_HPIL_VIDEO_RENDER_H
#define SOYNUT_HP41_HPIL_VIDEO_RENDER_H

#include <stdint.h>

/**
 * @brief Render the video interface's current screen into `fb`.
 *
 * @param fb At least HP41_QUAD_FB_SIZE (hp41_quad_display_bridge.h)
 *           bytes; fully overwritten (cleared to all-white, 0xFF, then
 *           lit pixels plotted by clearing bits - the same polarity
 *           every other quad/ display path uses for this panel).
 */
void hp41_hpil_video_render_into(uint8_t *fb);

#endif /* SOYNUT_HP41_HPIL_VIDEO_RENDER_H */
