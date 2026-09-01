/**
 * @file hp41_hpil_video_bridge.c
 * @brief Implementation - see hp41_hpil_video_bridge.h for the full
 *        contract and why this exists as a separate file from
 *        hp41_hpil_controller.c.
 */

#include "hp41_hpil_video_bridge.h"

#include <assert.h>

#include "ilvideo_device.h"

#include "hp41_hpil_controller.h"

/** The one HP-IL peripheral currently on soynut's loop. A plain static
 * (not heap-allocated - see this project's Power of 10 Rule 3), owned
 * entirely by this translation unit; every other file reaches it only
 * through this header's accessor functions. */
static ilvideo_device_t g_video_device;

void hp41_hpil_video_bridge_init(void) {
    ilvideo_device_init(&g_video_device);
}

bool hp41_hpil_video_bridge_is_dirty(void) {
    return g_video_device.term.dirty;
}

void hp41_hpil_video_bridge_clear_dirty(void) {
    g_video_device.term.dirty = false;
}

uint8_t hp41_hpil_video_bridge_char_at(int x, int y) {
    assert(x >= 0 && x < ILVIDEO_COLS);
    assert(y >= 0 && y < ILVIDEO_VISIBLE_ROWS);
    return ilvideo_term_char_at(&g_video_device.term, x, y);
}

int hp41_hpil_loop_transmit(uint16_t frame) {
    assert(frame <= 0x7FFu); /* a real 11-bit HP-IL frame, hp41_hpil_controller.c's own contract */

    /* soynut wires exactly one device onto the loop today (this one) -
     * see this file's own header comment for why that makes a real
     * multi-device chain-walk (matching emu41gcc's tabdev[]/nbdev)
     * unnecessary for now. ilvideo_device_process_frame() always
     * returns a valid frame (never the "loop severed" -1 sentinel
     * hp41_hpil_controller.h's contract allows for), since a single
     * HP-IL device transforming and echoing a frame can't sever a
     * loop it's the only member of. */
    int reply = (int)ilvideo_device_process_frame(&g_video_device, frame);

    assert(reply >= 0 && reply <= 0x7FF);
    return reply;
}
