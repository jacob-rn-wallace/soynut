/**
 * @file hp41_hpil_video_bridge.h
 * @brief Owns the one HP-IL peripheral currently wired onto soynut's
 *        loop - an HP-82163A Video Interface, via the ilvideo-native
 *        submodule (../ilvideo-native/, a separate repo: see that
 *        repo's own README for what it is and its full provenance).
 *
 * hp41_hpil_controller.c's hpil_wr()/hpil_rd() implement the generic
 * 1LB3 chip register protocol without knowing or caring what devices
 * exist on the loop; this file is the other half - it's what actually
 * exists on the loop. hp41_hpil_loop_transmit() (declared in
 * hp41_hpil_controller.h, defined here) is the fixed link between the
 * two: every frame the controller transmits reaches
 * ilvideo_device_process_frame() through this one function call, and
 * whatever it returns goes straight back - fully synchronous, same
 * call stack, no threads/cores/queues needed (see
 * hp41_hpil_controller.h's own header comment on why that's
 * sufficient: real HP-IL frame transfer in the Nut CPU has no timing
 * model of its own either).
 *
 * soynut only ever wires exactly one device today - a real multi-
 * device loop (matching emu41gcc's own tabdev[]/nbdev table) isn't
 * needed yet and isn't built here; if a second HP-IL peripheral is
 * ever added, hp41_hpil_loop_transmit() is where that chain-walk would
 * go; see that function's own header comment for the current
 * single-device shortcut.
 */
#ifndef SOYNUT_HP41_HPIL_VIDEO_BRIDGE_H
#define SOYNUT_HP41_HPIL_VIDEO_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Reset the video interface device to its power-on state.
 *
 * Call once at boot (mirrors hp41_hpil_controller_init()) and whenever
 * the whole HP-IL bus is master-cleared.
 */
void hp41_hpil_video_bridge_init(void);

/**
 * @brief Whether the video interface's on-screen content changed since
 *        the last call to hp41_hpil_video_bridge_clear_dirty().
 *
 * A thin wrapper over the owned ilvideo_term_t's own `dirty` flag - see
 * ilvideo-native/core/ilvideo_term.h - so main.c's render-gating logic
 * doesn't need to reach into this module's internals directly.
 *
 * @return true if a redraw is needed.
 */
bool hp41_hpil_video_bridge_is_dirty(void);

/** @brief Clear the dirty flag hp41_hpil_video_bridge_is_dirty() reports. */
void hp41_hpil_video_bridge_clear_dirty(void);

/**
 * @brief The character currently shown at visible grid position (x, y).
 *
 * @param x Column, 0-31 (ILVIDEO_COLS - 1).
 * @param y Row, 0-15 (ILVIDEO_VISIBLE_ROWS - 1).
 * @return Printable ASCII (32-126), or ' ' for a cell never written to.
 */
uint8_t hp41_hpil_video_bridge_char_at(int x, int y);

#endif /* SOYNUT_HP41_HPIL_VIDEO_BRIDGE_H */
