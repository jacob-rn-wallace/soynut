/**
 * @file main.c
 * @brief Full system integration for the QUAD-style display: boots the
 *        HP-41 ROM on the emulated Nut CPU core, drives the Sharp
 *        Memory LCD over SPI, and feeds USB-serial keypresses into the
 *        emulator's keyboard buffer.
 *
 * Adapted line-by-line from firmware/main.c (the 144x32 NHD14432
 * target's own main loop) - same precedent sim/sim_main.c already set
 * for a second main-loop variant of that file. Persist-state/CLRMEM
 * handling and the general executeNUT()/key-hold/throttle structure are
 * unchanged; what's different is entirely display-related: no Elite
 * Mode (this target only ever shows the QUAD-style Stack/classic-line
 * views - see hp41_quad_display_bridge.h), Sharp LCD SPI push instead
 * of ST7920 parallel, and a periodic forced-refresh heartbeat this
 * panel needs that the ST7920 never did (see CLAUDE.md's "Sharp Memory
 * LCD bring-up" section).
 */

#include "pico/stdlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "sharpdisp/sharpdisp.h"

#include "pins.h"
#include "nut_rom.h"
#include "hp41_quad_display_bridge.h"
#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"
#include "hp41_persist_state.h"
#include "hp41_persist_flash.h"

#define GLOBAL extern
#include "nutcpu.h"

/**
 * @brief printf()-style debug log line, prefixed with milliseconds since boot.
 *
 * Same idiom as firmware/main.c's own dbg() - see that file's comment.
 *
 * @param fmt printf()-style format string; must be non-NULL.
 * @param ... printf()-style arguments matching @p fmt.
 *
 * Power of 10, Rule 5 note: fmt!=NULL is the only real precondition
 * this function has, matching firmware/main.c's own dbg()'s identical
 * rationale for not adding a second assertion here.
 */
static void dbg(const char *fmt, ...) {
    assert(fmt != NULL);
    printf("[%lums] ", (unsigned long)to_ms_since_boot(get_absolute_time()));
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/** Throttle target for executeNUT() - see firmware/main.c's identical constant. */
#define TARGET_INSTRUCTIONS_PER_SEC 200000

/** Defensive upper bound on getchar_timeout_us() calls per drain_usb_bytes()
 *  call - see firmware/main.c's identical constant/function for the full
 *  Power of 10 Rule 2 rationale. */
#define MAX_BYTES_PER_DRAIN 256

/** How long the system must sit idle before a pending continuous-memory
 *  snapshot is actually written to flash - see firmware/main.c's
 *  identical constant for the full rationale (unchanged here). */
#define PERSIST_SAVE_DELAY_MS 1500u

/** How often to force a Sharp LCD refresh regardless of whether the
 *  display content actually changed - this panel's VCOM/DC-bias health
 *  requires periodic refreshing even when completely idle, unlike the
 *  ST7920 (which has its own GDRAM) - see CLAUDE.md's "Sharp Memory LCD
 *  bring-up" section. Already proven out in quad_bringup/main.c before
 *  this target existed. */
#define REFRESH_INTERVAL_MS 1000u

/**
 * @brief Drain any pending USB serial bytes into the key bridge, without blocking.
 *
 * Identical to firmware/main.c's own drain_usb_bytes() - see that
 * file's header comment for the full Power of 10 Rule 2 rationale
 * (factored out so it can be called both once per outer-loop iteration
 * and once per single-stepped instruction during a key hold).
 */
static void drain_usb_bytes(void) {
    int c;
    int drained = 0;
    while (drained < MAX_BYTES_PER_DRAIN
           && (c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        hp41_key_bridge_feed_byte(c);
        drained++;
    }
    assert(drained >= 0 && drained <= MAX_BYTES_PER_DRAIN);
    assert(lgkeybuf >= 0 && lgkeybuf <= 8);
}

/**
 * @brief Entry point: bring up hardware, boot the ROM, then run forever.
 *
 * Per main-loop iteration: drains pending USB keypresses (always, even
 * while asleep, since a key is what wakes it); handles "[CLRMEM]" and
 * "[DSP]" bridge-level commands; if asleep and a key is now queued,
 * resets regPC/flagKey and wakes; otherwise skips executeNUT() entirely
 * while asleep; else runs executeNUT(1000) (single-stepping instead,
 * sustaining the key-hold state, if a hold is active); throttles via
 * sleep_us(); on fdsp (or a view toggle), computes/pushes a new
 * framebuffer to the Sharp LCD; independently, refreshes the panel
 * unconditionally on a fixed interval for VCOM health; on POWOFF,
 * captures continuous-memory state, blanks the panel if the ROM says
 * display should be off, and goes to sleep; on an invalid opcode, halts.
 * See firmware/main.c's identical structure for the parts unchanged
 * here, and CLAUDE.md's "Sharp Memory LCD bring-up" section for what's
 * different.
 *
 * @return Never returns - this is bare-metal firmware with no OS to
 *         return control to.
 */
int main(void) {
    stdio_init_all();

    for (int i = 3; i > 0; i--) {
        dbg("quad: starting in %d...\n", i);
        sleep_ms(1000);
    }

    dbg("quad: sharpdisp_init()...\n");
    static uint8_t disp_buffer[HP41_QUAD_FB_SIZE];
    static struct SharpDisp disp;
    sharpdisp_init(&disp, disp_buffer, HP41_QUAD_DISP_WIDTH_PX, HP41_QUAD_DISP_HEIGHT_PX,
                   0xFF, PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_MOSI, spi0, 10000000u);

    dbg("quad: nut_boot()...\n");
    nut_boot();
    assert(regPC == 0); /* nut_boot()'s documented cold-start value */

    // See firmware/main.c's identical comment: real HP-41 hardware halts
    // the CPU clock entirely after POWOFF, resuming only via a hardware
    // keyboard-scan interrupt that restarts execution at address 0.
    bool asleep = false;

    // Which of the two QUAD-style views is currently showing - see
    // hp41_quad_display_bridge.h. Starts on Stack view (the primary
    // mode this whole display exists for); toggled by "[DSP]" (Phase 3c
    // of the Magellan/QUAD plan) - see hp41_key_bridge.h. Deliberately
    // a plain main()-local, same reasoning as firmware/main.c's
    // elite_mode_active: ephemeral display/UI state, not real HP-41
    // calculator memory, so it always starts fresh on every boot.
    hp41_quad_view_t view = HP41_QUAD_VIEW_STACK;

    // Deferred/debounced continuous-memory save state - see
    // firmware/main.c's identical fields for the full rationale.
    static hp41_persist_state_t pending_snapshot;
    bool persist_dirty = false;
    uint32_t persist_idle_since_ms = 0;

    hp41_persist_state_t saved_state;
    if (hp41_persist_flash_load(&saved_state)) {
        hp41_persist_apply(&saved_state);
        asleep = true;
        dbg("quad: restored continuous memory from flash\n");
    } else {
        dbg("quad: no valid persisted memory - MEMORY LOST cold start\n");
    }

    dbg("quad: entering main loop\n");

    static uint8_t framebuf[HP41_QUAD_FB_SIZE];
    int render_count = 0;
    uint32_t last_heartbeat_ms = to_ms_since_boot(get_absolute_time());
    uint32_t last_refresh_ms = last_heartbeat_ms;

    while (true) {
        drain_usb_bytes();

        // "[CLRMEM]" - identical handling to firmware/main.c's own; see
        // that file's comment for the full rationale.
        if (hp41_key_bridge_clear_memory_requested()) {
            dbg("quad: CLRMEM requested - erasing persisted memory\n");
            hp41_persist_flash_erase();
            nut_boot();
            memset(espaceRAM, 0, sizeof(espaceRAM));
            asleep = false;
            persist_dirty = false;
        }

        // "[DSP]" view toggle (Phase 3c) - see hp41_key_bridge.h.
        // redraw_needed forces an immediate re-render below rather than
        // waiting for the next fdsp, since toggling the view doesn't
        // itself touch the ROM's own display registers.
        bool redraw_needed = false;
        if (hp41_key_bridge_quad_view_toggle_requested()) {
            view = (view == HP41_QUAD_VIEW_STACK)
                       ? HP41_QUAD_VIEW_CLASSIC_LINE
                       : HP41_QUAD_VIEW_STACK;
            redraw_needed = true;
            dbg("quad: view toggled to %s\n",
                view == HP41_QUAD_VIEW_STACK ? "STACK" : "CLASSIC_LINE");
        }

        // Once/second liveness heartbeat - see firmware/main.c's
        // identical comment for why this runs even while asleep.
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_heartbeat_ms >= 1000) {
            last_heartbeat_ms = now_ms;
            dbg("quad: heartbeat PC=0x%04X instr=%d lgkeybuf=%d flagKey=%d regK=0x%02X asleep=%d view=%s\n",
                   regPC, cptinstr, lgkeybuf, flagKey, regK, asleep,
                   view == HP41_QUAD_VIEW_STACK ? "STACK" : "CLASSIC_LINE");
        }

        // Periodic forced refresh, independent of fdsp/redraw_needed -
        // this panel needs it even while asleep (see REFRESH_INTERVAL_MS's
        // comment above). Pushes whatever's already in disp_buffer -
        // harmless/no-op-looking if nothing changed since the last push,
        // but that's exactly the point: the SPI push itself is what
        // keeps VCOM alive, not just the buffer contents.
        if (now_ms - last_refresh_ms >= REFRESH_INTERVAL_MS) {
            last_refresh_ms = now_ms;
            sharpdisp_refresh(&disp);
        }

        if (asleep) {
            if (lgkeybuf > 0) {
                dbg("quad: waking from POWOFF (key queued), resetting PC to 0\n");
                flagKey = 0;
                regPC = 0;
                asleep = false;
            } else if (!redraw_needed) {
                if (persist_dirty && (now_ms - persist_idle_since_ms) >= PERSIST_SAVE_DELAY_MS) {
                    dbg("quad: idle - flushing deferred continuous-memory save\n");
                    hp41_persist_flash_save(&pending_snapshot);
                    persist_dirty = false;
                }
                continue; // keep waiting - do not call executeNUT() while asleep
            }
            // else: a view toggle landed this iteration - fall through
            // to the render block below with asleep still true, same as
            // firmware/main.c's identical Elite Mode case.
        }

        // Key-hold single-stepping - identical to firmware/main.c's own,
        // see that file's extensive comment for the full history/rationale.
        int cptinstr_before = cptinstr;
        int ret = 0;
        if (!asleep) {
            if (hp41_key_hold_active()) {
                for (int i = 0; i < 1000 && hp41_key_hold_active(); i++) {
                    hp41_key_hold_sustain();
                    ret = executeNUT(1);
                    drain_usb_bytes();
                    if (ret != 0 || fdsp) break;
                }
            } else {
                ret = executeNUT(1000);
            }
        }
        assert(ret >= 0 && ret <= 3);

        int instructions_ran = cptinstr - cptinstr_before;
        assert(instructions_ran >= 0);
        if (instructions_ran > 0) {
            sleep_us((instructions_ran * 1000000ULL) / TARGET_INSTRUCTIONS_PER_SEC);
        }

        if (fdsp || redraw_needed) {
            render_count++;
            hp41_quad_display_compute_framebuffer(framebuf, view);
            memcpy(disp_buffer, framebuf, HP41_QUAD_FB_SIZE);
            dbg("quad: rendering display state #%d (PC=0x%04X, instr=%d, view=%s)\n",
                   render_count, regPC, cptinstr,
                   view == HP41_QUAD_VIEW_STACK ? "STACK" : "CLASSIC_LINE");
            sharpdisp_refresh(&disp);
            last_refresh_ms = now_ms; // this push already covers this tick's refresh
            fdsp = 0;
        }

        if (ret == 1) {
            // POWOFF - see firmware/main.c's extensive comment for the
            // full rationale (unchanged here). Blank the panel (all-
            // white, matching this display's own polarity - see
            // CLAUDE.md) only when the ROM itself says display should be
            // off right now, not on every POWOFF (which fires after
            // essentially every keystroke).
            dbg("quad: POWOFF (Carry=%d) - sleeping until next key\n", Carry);

            if (dspon == 0) {
                memset(disp_buffer, 0xFF, HP41_QUAD_FB_SIZE);
                sharpdisp_refresh(&disp);
                last_refresh_ms = now_ms;
                dbg("quad: panel cleared for power-off (dspon=0)\n");
            }

            hp41_persist_capture(&pending_snapshot);
            persist_dirty = true;
            persist_idle_since_ms = now_ms;
            asleep = true;
        }

        if (ret == 2) {
            // Invalid opcode - see firmware/main.c's identical handling
            // and DEVIATIONS.md's Rule 2 entry for why this halt loop is
            // a documented, intentional exception.
            dbg("quad: invalid opcode at PC=0x%04X, halting\n", regPC);
            while (true) {
                tight_loop_contents();
            }
        }
    }
}
