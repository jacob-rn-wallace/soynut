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
 * Mode (144x32/NHD14432-specific, stays exclusive to firmware/), Sharp
 * LCD SPI push instead of ST7920 parallel, and a periodic forced-
 * refresh heartbeat this panel needs that the ST7920 never did (see
 * CLAUDE.md's "Sharp Memory LCD bring-up" section).
 *
 * Three views instead of the original two: Stack and Classic-line (see
 * hp41_quad_display_bridge.h) plus the HP-IL video interface's own
 * screen (hp41_hpil_video_render.h, backed by hp41_hpil_controller.c's
 * real HP-IL protocol logic and the ilvideo-native submodule) - see
 * CLAUDE.md's "HP-IL video interface" section. All three are cycled by
 * the same "[DSP]" one-shot bridge command (hp41_key_bridge.h).
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
#include "hp41_hpil_controller.h"
#include "hp41_hpil_video_bridge.h"
#include "hp41_hpil_video_render.h"
#include "nut_rom_hpil.h"

#define GLOBAL extern
#include "nutcpu.h"

/**
 * @brief Which of this target's three views is currently showing.
 *
 * A superset of hp41_quad_display_bridge.h's own hp41_quad_view_t (that
 * header stays scoped to the classic HP-41 display alone - see
 * hp41_hpil_video_render.h's own header comment for why the HP-IL
 * video interface's screen is rendered by a separate module instead of
 * being folded into that enum). This is a plain main()-local concept,
 * same as firmware/main.c's elite_mode_active: ephemeral UI state, not
 * real HP-41 calculator memory, so it always starts fresh on every boot.
 */
typedef enum {
    QUAD_MAIN_VIEW_STACK,
    QUAD_MAIN_VIEW_CLASSIC_LINE,
    QUAD_MAIN_VIEW_HPIL_VIDEO,
} quad_main_view_t;

/** @brief Short name for a view, for dbg() lines. @param view Any quad_main_view_t. @return A static string. */
static const char *quad_main_view_name(quad_main_view_t view) {
    switch (view) {
    case QUAD_MAIN_VIEW_STACK:
        return "STACK";
    case QUAD_MAIN_VIEW_CLASSIC_LINE:
        return "CLASSIC_LINE";
    case QUAD_MAIN_VIEW_HPIL_VIDEO:
        return "HPIL_VIDEO";
    default:
        assert(false); /* every quad_main_view_t value is handled above */
        return "?";
    }
}

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
 * "[DSP]" bridge-level commands (the latter now cycling three views);
 * if asleep and a key is now queued, resets regPC/flagKey and wakes;
 * otherwise skips executeNUT() entirely while asleep; else runs
 * executeNUT(1000) (single-stepping instead, sustaining the key-hold
 * state, if a hold is active); throttles via sleep_us(); drains fdsp
 * unconditionally (it must never be left set, regardless of which view
 * is showing - see the fdsp-draining block's own comment); redraws
 * when a view toggle happened, or when the currently-showing view's own
 * dirty signal says so (fdsp-derived staleness for Stack/Classic-line,
 * hp41_hpil_video_bridge_is_dirty() for the HP-IL video interface) -
 * computing/pushing a new framebuffer to the Sharp LCD; independently,
 * refreshes the panel unconditionally on a fixed interval for VCOM
 * health; on POWOFF, captures continuous-memory state, blanks the panel
 * if the ROM says display should be off (and a classic/stack view is
 * showing), and goes to sleep; on an invalid opcode, halts. See
 * firmware/main.c's identical structure for the parts unchanged here,
 * and CLAUDE.md's "Sharp Memory LCD bring-up" and "HP-IL video
 * interface" sections for what's different.
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

    // Real HP-IL controller state plus the one peripheral wired onto
    // the loop (the video interface) - see hp41_hpil_controller.h/
    // hp41_hpil_video_bridge.h and CLAUDE.md's "HP-IL video interface"
    // section. Same boot-time reset point firmware/main.c uses.
    dbg("quad: hp41_hpil_controller_init()...\n");
    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    // The HP-IL module ROM (roms/HPIL.MOD) is genuinely optional - see
    // CMakeLists.txt's own HPIL_MODULE_AVAILABLE comment. Without it,
    // the base OS alone never touches HP-IL opcodes (confirmed
    // empirically - see CLAUDE.md's "HP-IL video interface" section),
    // so the HPIL_VIDEO view would just stay permanently blank; wiring
    // the module in when present is what actually makes it live.
#ifdef HPIL_MODULE_AVAILABLE
    dbg("quad: nut_rom_wire_hpil_module()...\n");
    nut_rom_wire_hpil_module();
#else
    dbg("quad: HP-IL module ROM not built in - HPIL_VIDEO view will stay blank\n");
#endif

    // See firmware/main.c's identical comment: real HP-41 hardware halts
    // the CPU clock entirely after POWOFF, resuming only via a hardware
    // keyboard-scan interrupt that restarts execution at address 0.
    bool asleep = false;

    // Which of this target's three views is currently showing - see
    // quad_main_view_t above. Starts on Stack view (the primary mode
    // this whole display exists for); toggled by "[DSP]" (Phase 3c of
    // the Magellan/QUAD plan, now cycling three views instead of the
    // original two - see hp41_key_bridge.h). Deliberately a plain
    // main()-local, same reasoning as firmware/main.c's
    // elite_mode_active: ephemeral display/UI state, not real HP-41
    // calculator memory, so it always starts fresh on every boot.
    quad_main_view_t view = QUAD_MAIN_VIEW_STACK;

    // Set whenever fdsp fires while a view other than Stack/Classic-line
    // is showing (see the fdsp-draining comment in the main loop below
    // for why fdsp itself can't be left set that long) - remembers that
    // those two views have newer content to show than what's currently
    // rendered, so switching back to either of them later still redraws
    // fresh content instead of something stale. Cleared once that
    // redraw actually happens.
    bool classic_view_stale = false;

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

        // "[DSP]" view toggle (Phase 3c, now cycling three views instead
        // of the original two) - see hp41_key_bridge.h. redraw_needed
        // forces an immediate re-render below rather than waiting for
        // the next fdsp, since toggling the view doesn't itself touch
        // the ROM's own display registers (and the HP-IL video
        // interface's own screen isn't driven by fdsp at all).
        bool redraw_needed = false;
        if (hp41_key_bridge_quad_view_toggle_requested()) {
            switch (view) {
            case QUAD_MAIN_VIEW_STACK:
                view = QUAD_MAIN_VIEW_CLASSIC_LINE;
                break;
            case QUAD_MAIN_VIEW_CLASSIC_LINE:
                view = QUAD_MAIN_VIEW_HPIL_VIDEO;
                break;
            case QUAD_MAIN_VIEW_HPIL_VIDEO:
            default:
                view = QUAD_MAIN_VIEW_STACK;
                break;
            }
            redraw_needed = true;
            dbg("quad: view toggled to %s\n", quad_main_view_name(view));
        }

        // Once/second liveness heartbeat - see firmware/main.c's
        // identical comment for why this runs even while asleep.
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_heartbeat_ms >= 1000) {
            last_heartbeat_ms = now_ms;
            dbg("quad: heartbeat PC=0x%04X instr=%d lgkeybuf=%d flagKey=%d regK=0x%02X asleep=%d view=%s\n",
                   regPC, cptinstr, lgkeybuf, flagKey, regK, asleep,
                   quad_main_view_name(view));
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

        // executeNUT() returns early as soon as fdsp is set (see its own
        // comment in emu41gcc/nutcpu.c) - it must be drained every time
        // it fires, regardless of which view is currently showing, or
        // the CPU would stall at effectively zero throughput while the
        // HP-IL video interface view is selected (fdsp would stay set
        // forever, and every executeNUT() call would immediately return
        // without making progress). classic_view_stale (declared above
        // the main loop) remembers there's newer classic/stack content
        // to show even when fdsp gets drained while a different view is
        // active.
        if (fdsp) {
            classic_view_stale = true;
            fdsp = 0;
        }

        // The HP-IL video interface's screen has its own independent
        // dirty signal (hp41_hpil_video_bridge_is_dirty()) - it isn't
        // driven by fdsp at all, so it needs its own redraw condition
        // rather than reusing the classic display's.
        bool video_view_needs_redraw =
            (view == QUAD_MAIN_VIEW_HPIL_VIDEO) && hp41_hpil_video_bridge_is_dirty();
        bool classic_view_needs_redraw =
            (view != QUAD_MAIN_VIEW_HPIL_VIDEO) && classic_view_stale;

        if (redraw_needed || video_view_needs_redraw || classic_view_needs_redraw) {
            render_count++;
            switch (view) {
            case QUAD_MAIN_VIEW_STACK:
                hp41_quad_display_compute_framebuffer(framebuf, HP41_QUAD_VIEW_STACK);
                classic_view_stale = false;
                break;
            case QUAD_MAIN_VIEW_CLASSIC_LINE:
                hp41_quad_display_compute_framebuffer(framebuf, HP41_QUAD_VIEW_CLASSIC_LINE);
                classic_view_stale = false;
                break;
            case QUAD_MAIN_VIEW_HPIL_VIDEO:
            default:
                hp41_hpil_video_render_into(framebuf);
                hp41_hpil_video_bridge_clear_dirty();
                break;
            }
            memcpy(disp_buffer, framebuf, HP41_QUAD_FB_SIZE);
            dbg("quad: rendering display state #%d (PC=0x%04X, instr=%d, view=%s)\n",
                   render_count, regPC, cptinstr, quad_main_view_name(view));
            sharpdisp_refresh(&disp);
            last_refresh_ms = now_ms; // this push already covers this tick's refresh
        }

        if (ret == 1) {
            // POWOFF - see firmware/main.c's extensive comment for the
            // full rationale (unchanged here). Blank the panel (all-
            // white, matching this display's own polarity - see
            // CLAUDE.md) only when the ROM itself says display should be
            // off right now, not on every POWOFF (which fires after
            // essentially every keystroke) - AND only while showing a
            // classic/stack view. dspon reflects the calculator's own
            // internal display-on state; the HP-IL video interface is a
            // separate peripheral on the loop with its own screen
            // content, not driven by dspon at all on real hardware (a
            // real HP-82163A module doesn't go blank just because the
            // calculator's own LCD does) - not independently confirmed
            // against real hardware/protocol behavior (see CLAUDE.md's
            // "HP-IL video interface" section, "What's unconfirmed"),
            // but blanking an unrelated peripheral's screen because the
            // *calculator's* display turned off would clearly be wrong,
            // so this is the conservative choice.
            dbg("quad: POWOFF (Carry=%d) - sleeping until next key\n", Carry);

            if (dspon == 0 && view != QUAD_MAIN_VIEW_HPIL_VIDEO) {
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
