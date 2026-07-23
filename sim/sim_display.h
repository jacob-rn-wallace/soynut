/**
 * @file sim_display.h
 * @brief SDL2-backed virtual LCD - implements firmware/st7920.h's
 *        st7920_init()/st7920_clear()/st7920_draw_frame() contract
 *        (declared there, defined in sim_display.c) as a drop-in
 *        replacement for the real GPIO driver.
 */
#ifndef SOYNUT_SIM_DISPLAY_H
#define SOYNUT_SIM_DISPLAY_H

/** Integer pixel-scale factor the 144x32 LCD is rendered at on screen -
 *  the panel is illegibly small at 1:1 on a modern display. One place
 *  to tweak if a different window size is wanted. */
#define SIM_DISPLAY_SCALE 6

#endif // SOYNUT_SIM_DISPLAY_H
