/**
 * @file sim_main.c
 * @brief Host-native simulator entry point: boots the real HP-41 ROM on
 *        the same emulated Nut CPU core real firmware uses, renders to
 *        a virtual SDL2 LCD (sim_display.c) instead of real GPIO, reads
 *        keypresses from the SDL window (sim_keyboard.c) and/or an
 *        optional virtual serial port (sim_pty.c, for
 *        tools/hp41_keyboard_gui.py) instead of USB serial, and
 *        persists continuous memory to a local file instead of flash
 *        (sim_persist_file.c).
 *
 * Adapted line-by-line from firmware/main.c - see that file for the
 * hardware-driven original this mirrors. Every piece of *logic* here
 * (CLRMEM handling, Elite Mode toggles, the asleep/wake transition, the
 * hold-vs-batch executeNUT() split, the fdsp-gated render, POWOFF's
 * dspon-conditional clear, deferred continuous-memory save) is
 * unchanged from real firmware; only the hardware-facing calls
 * (stdio/GPIO/flash/timing) are replaced with sim_*.c equivalents.
 *
 * Unlike firmware/main.c's main() (an explicit, file-specific CLAUDE.md
 * Power-of-10 Rule 4 exception - see that file's own note), this file's
 * loop body is factored into named helpers, since that exception does
 * not extend to new code.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "st7920.h"
#include "nut_rom.h"
#include "hp41_display_bridge.h"
#include "hp41_elite_display_bridge.h"
#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"
#include "hp41_persist_state.h"
#include "sim_persist_file.h"
#include "sim_keyboard.h"
#include "sim_clock.h"
#include "sim_pty.h"

#define GLOBAL extern
#include "nutcpu.h"

/** See firmware/main.c's identical constant: an approximate,
 *  commonly-cited real-Nut-CPU-speed figure, not cycle-exact - keeps
 *  ROM-side timing assumptions (auto-power-off, key debounce) in a
 *  human-usable range. */
#define TARGET_INSTRUCTIONS_PER_SEC 200000

/** See firmware/main.c's identical constant and its "deferred/debounced
 *  continuous-memory save" rationale - flushing the persist file
 *  synchronously on every POWOFF would be a needless per-keystroke
 *  file-write stall here too, even though a host file write is much
 *  cheaper than a real flash erase/program cycle. */
#define PERSIST_SAVE_DELAY_MS 1500u

/** Fixed line-buffer size for sim_dbg() - long enough for every log line
 *  this file emits (the longest include a device path), with headroom;
 *  a line that would overflow is silently truncated by vsnprintf(),
 *  same "best-effort logging" tradeoff real firmware's own dbg() makes
 *  implicitly via a bare vprintf(). */
#define SIM_DBG_LINE_MAX 256

/**
 * @brief printf()-style debug log line, prefixed with milliseconds since
 *        sim start, and mirrored to the virtual serial port if one is
 *        open (see sim_pty.h) - so a connected
 *        tools/hp41_keyboard_gui.py's log pane shows the same trace a
 *        real Pico's shared USB-CDC connection would.
 *
 * @param fmt printf()-style format string; must be non-NULL.
 * @param ... printf()-style arguments matching @p fmt.
 */
static void sim_dbg(const char *fmt, ...)
{
    assert(fmt != NULL);

    char line[SIM_DBG_LINE_MAX];
    int prefix_len = snprintf(line, sizeof(line), "[%ums] ", (unsigned)sim_clock_now_ms());
    assert(prefix_len > 0 && (size_t)prefix_len < sizeof(line));

    va_list args;
    va_start(args, fmt);
    int body_len = vsnprintf(line + prefix_len, sizeof(line) - (size_t)prefix_len, fmt, args);
    va_end(args);
    assert(body_len >= 0);

    fputs(line, stdout);
    sim_pty_write(line, strnlen(line, sizeof(line)));
}

/** All per-iteration simulator state that used to be main()-local in
 *  firmware/main.c, grouped so the helper functions below can share it
 *  without long parameter lists. A local static (not global) inside
 *  main() - see main()'s own declaration for why pending_snapshot/
 *  framebuf specifically need static storage. */
typedef struct {
    bool asleep;
    bool elite_mode_active;
    bool alpha_row_active;
    hp41_persist_state_t pending_snapshot;
    bool persist_dirty;
    uint32_t persist_idle_since_ms;
    uint8_t framebuf[LCD_FB_SIZE];
    int render_count;
    uint32_t last_heartbeat_ms;
} sim_state_t;

/** Defensive upper bound on sim_pty_read_byte() calls per
 *  sim_drain_pty_bytes() call (Power of 10, Rule 2), mirroring
 *  firmware/main.c's MAX_BYTES_PER_DRAIN pattern. */
#define MAX_PTY_BYTES_PER_DRAIN 256

/**
 * @brief Drain any bytes waiting on the virtual serial port into the key bridge.
 *
 * Mirrors firmware/main.c's drain_usb_bytes(): a bounded, non-blocking
 * drain loop feeding hp41_key_bridge_feed_byte(), the exact same sink
 * SDL-derived keypresses already use (see sim_keyboard_poll()) - so a
 * connected tools/hp41_keyboard_gui.py drives the sim with zero new
 * key-protocol logic. A no-op for the whole run if sim_pty_open() never
 * succeeded (sim_pty_read_byte() always returns -1 in that case).
 */
static void sim_drain_pty_bytes(void)
{
    int drained = 0;
    int c;
    while (drained < MAX_PTY_BYTES_PER_DRAIN && (c = sim_pty_read_byte()) != -1) {
        hp41_key_bridge_feed_byte(c);
        drained++;
    }
    assert(drained >= 0 && drained <= MAX_PTY_BYTES_PER_DRAIN);
    assert(lgkeybuf >= 0 && lgkeybuf <= 8);
}

/**
 * @brief Handle a pending "[CLRMEM]" request, if any; see firmware/main.c.
 *
 * @param state Simulator state to update.
 */
static void sim_check_clrmem(sim_state_t *state)
{
    assert(state != NULL);
    if (!hp41_key_bridge_clear_memory_requested())
        return;

    sim_dbg("soynut sim: CLRMEM requested - erasing persisted memory\n");
    hp41_persist_flash_erase();
    nut_boot();
    memset(espaceRAM, 0, sizeof(espaceRAM));
    state->asleep = false;
    state->persist_dirty = false; /* discard any not-yet-flushed stale save */
    assert(state->asleep == false);
    assert(state->persist_dirty == false);
}

/**
 * @brief Handle pending Elite Mode toggle requests, if any; see firmware/main.c.
 *
 * @param state Simulator state to update.
 * @return true if either toggle fired and an immediate redraw is needed.
 */
static bool sim_check_elite_toggles(sim_state_t *state)
{
    assert(state != NULL);
    bool redraw_needed = false;
    if (hp41_key_bridge_elite_mode_toggle_requested()) {
        state->elite_mode_active = !state->elite_mode_active;
        state->alpha_row_active = false;
        hp41_key_bridge_set_elite_mode_active(state->elite_mode_active);
        redraw_needed = true;
        sim_dbg("soynut sim: Elite Mode %s\n", state->elite_mode_active ? "ON" : "OFF");
    }
    if (hp41_key_bridge_alpha_row_toggle_requested()) {
        state->alpha_row_active = !state->alpha_row_active;
        redraw_needed = true;
        sim_dbg("soynut sim: Elite Mode alpha row %s\n", state->alpha_row_active ? "ON" : "OFF");
    }
    assert(redraw_needed == true || redraw_needed == false);
    return redraw_needed;
}

/**
 * @brief Once-per-second liveness log; see firmware/main.c.
 *
 * @param state Simulator state (read-only except last_heartbeat_ms).
 * @param now_ms Current sim time.
 */
static void sim_heartbeat(sim_state_t *state, uint32_t now_ms)
{
    assert(state != NULL);
    assert(now_ms >= state->last_heartbeat_ms); /* sim_clock_now_ms() is monotonic */
    if (now_ms - state->last_heartbeat_ms < 1000)
        return;
    state->last_heartbeat_ms = now_ms;
    sim_dbg("soynut sim: heartbeat PC=0x%04X instr=%d lgkeybuf=%d flagKey=%d regK=0x%02X asleep=%d\n",
            regPC, cptinstr, lgkeybuf, flagKey, regK, state->asleep);
}

/** What the main loop should do next, decided by sim_handle_sleep_state(). */
typedef enum {
    SIM_LOOP_CONTINUE,  /**< Genuinely idle - skip straight to the next iteration. */
    SIM_LOOP_SKIP_EXEC, /**< Still asleep, but a redraw is needed - skip executeNUT(), still render. */
    SIM_LOOP_RUN,       /**< Not asleep - run executeNUT() normally. */
} sim_loop_action_t;

/**
 * @brief Decide what the current iteration should do given asleep/wake state.
 *
 * Mirrors firmware/main.c's `if (asleep) { ... }` block exactly,
 * including the deferred continuous-memory flush on genuine idle.
 *
 * @param state Simulator state to update (asleep, persist fields).
 * @param now_ms Current sim time.
 * @param redraw_needed Whether an Elite Mode toggle landed this iteration.
 * @return What the caller should do next.
 */
static sim_loop_action_t sim_handle_sleep_state(sim_state_t *state, uint32_t now_ms, bool redraw_needed)
{
    assert(state != NULL);
    if (!state->asleep)
        return SIM_LOOP_RUN;

    if (lgkeybuf > 0) {
        sim_dbg("soynut sim: waking from POWOFF (key queued), resetting PC to 0\n");
        flagKey = 0;
        regPC = 0;
        state->asleep = false;
        assert(state->asleep == false);
        return SIM_LOOP_RUN;
    }

    if (!redraw_needed) {
        if (state->persist_dirty && (now_ms - state->persist_idle_since_ms) >= PERSIST_SAVE_DELAY_MS) {
            sim_dbg("soynut sim: idle - flushing deferred continuous-memory save\n");
            hp41_persist_flash_save(&state->pending_snapshot);
            state->persist_dirty = false;
        }
        return SIM_LOOP_CONTINUE;
    }

    return SIM_LOOP_SKIP_EXEC; /* still asleep, but must reach the render block below */
}

/**
 * @brief Run the Nut CPU core for this iteration, single-stepping while a hold is active.
 *
 * Mirrors firmware/main.c's hold-vs-batch executeNUT() split exactly,
 * including polling for new keyboard events between single steps so a
 * real release isn't missed for up to 1000 instructions - see
 * firmware/main.c's own comment for why that specific bug shape matters.
 *
 * @param now_ms Current sim time, passed through to sim_keyboard_poll().
 * @param quit_requested Set to true if the window was closed mid-hold.
 * @return executeNUT()'s return value (0-3).
 */
static int sim_run_cpu(uint32_t now_ms, bool *quit_requested)
{
    assert(quit_requested != NULL);
    int ret = 0;
    if (hp41_key_hold_active()) {
        for (int i = 0; i < 1000 && hp41_key_hold_active(); i++) {
            hp41_key_hold_sustain();
            ret = executeNUT(1);
            if (sim_keyboard_poll(now_ms))
                *quit_requested = true;
            sim_drain_pty_bytes();
            if (ret != 0 || fdsp)
                break;
        }
    } else {
        ret = executeNUT(1000);
    }
    assert(ret >= 0 && ret <= 3);
    return ret;
}

/**
 * @brief Compute and present a new frame if the ROM (or an Elite Mode
 *        toggle) marked the display dirty; see firmware/main.c.
 *
 * @param state Simulator state (framebuf, render_count, elite fields).
 * @param redraw_needed Whether an Elite Mode toggle forced this redraw.
 */
static void sim_render_if_needed(sim_state_t *state, bool redraw_needed)
{
    assert(state != NULL);
    if (!(fdsp || redraw_needed))
        return;

    state->render_count++;
    if (state->elite_mode_active) {
        if (state->alpha_row_active)
            hp41_elite_display_compute_framebuffer_alpha(state->framebuf);
        else
            hp41_elite_display_compute_framebuffer(state->framebuf);
    } else {
        hp41_display_compute_framebuffer(state->framebuf);
    }

    uint8_t checksum = 0;
    for (int i = 0; i < LCD_FB_SIZE; i++) {
        checksum ^= state->framebuf[i];
    }
    sim_dbg("soynut sim: rendering display state #%d (PC=0x%04X, instr=%d, checksum=0x%02X)\n",
            state->render_count, regPC, cptinstr, checksum);

    st7920_draw_frame(state->framebuf);
    fdsp = 0;
    assert(state->render_count > 0);
}

/**
 * @brief Handle a POWOFF return from executeNUT(); see firmware/main.c.
 *
 * @param state Simulator state to update.
 * @param now_ms Current sim time.
 */
static void sim_handle_powoff(sim_state_t *state, uint32_t now_ms)
{
    assert(state != NULL);
    sim_dbg("soynut sim: POWOFF (Carry=%d) - sleeping until next key\n", Carry);

    if (dspon == 0) {
        st7920_clear();
        sim_dbg("soynut sim: LCD cleared for power-off (dspon=0)\n");
    }

    hp41_persist_capture(&state->pending_snapshot);
    state->persist_dirty = true;
    state->persist_idle_since_ms = now_ms;
    state->asleep = true;
    assert(state->asleep == true);
}

/**
 * @brief Handle an invalid-opcode return from executeNUT(); see firmware/main.c.
 *
 * Real firmware halts in a tight loop forever - the only "explicit
 * recovery" bare-metal code with no OS can do (see CLAUDE.md/
 * DEVIATIONS.md's Rule 2 entry). This is host software with an OS: a
 * clean process exit is the equivalent recovery action, and avoids
 * introducing a new unbounded loop for no benefit.
 */
static void sim_handle_invalid_opcode(void)
{
    sim_dbg("soynut sim: invalid opcode at PC=0x%04X, exiting\n", regPC);
    exit(EXIT_FAILURE);
}

/** Path (relative to the working directory - always sim/, since `make
 *  -C sim` cd's there first) the virtual serial port's slave path is
 *  also written to, so a wrapper script (sim/run_with_gui.sh, `make -C
 *  sim run-gui`) can discover it directly instead of scraping log
 *  output. Lives under build/ so it needs no separate .gitignore entry
 *  - that whole directory is already ignored. */
#define SIM_PORT_FILE_PATH "build/soynut_sim.port"

/**
 * @brief Best-effort write of the PTY slave path to a small discovery file.
 *
 * Non-fatal on failure (e.g. build/ missing for some reason) - GUI
 * auto-launch is a convenience, not a hard requirement; sim_dbg()
 * already announced the path to a human either way.
 *
 * @param path Slave device path to record.
 */
static void sim_write_port_file(const char *path)
{
    assert(path != NULL);
    FILE *f = fopen(SIM_PORT_FILE_PATH, "w");
    if (f == NULL) {
        sim_dbg("soynut sim: could not write %s (non-fatal)\n", SIM_PORT_FILE_PATH);
        return;
    }
    fprintf(f, "%s\n", path);
    int close_ok = (fclose(f) == 0);
    assert(close_ok == true || close_ok == false);
}

/**
 * @brief Entry point: bring up the virtual LCD/keyboard, boot the ROM, then run forever.
 *
 * @return Never returns via normal control flow - exits via the window
 *         being closed (falls through to a normal `return 0`) or
 *         sim_handle_invalid_opcode()'s exit(EXIT_FAILURE).
 */
int main(void)
{
    /* Unbuffered, unlike real firmware's USB-CDC stdio (which is
     * effectively unbuffered per line anyway) - stdout is fully
     * buffered by default when not attached to a terminal (e.g.
     * redirected to a file/pipe for log capture), which would otherwise
     * delay every sim_dbg() line until program exit. */
    setvbuf(stdout, NULL, _IONBF, 0);

    char pty_slave_path[128];
    bool pty_ok = sim_pty_open(pty_slave_path, sizeof(pty_slave_path));

    st7920_init();
    st7920_clear();
    sim_keyboard_init();

    if (pty_ok) {
        sim_dbg("soynut sim: virtual serial port at %s - point "
                "tools/hp41_keyboard_gui.py --port %s at it\n",
                pty_slave_path, pty_slave_path);
        sim_write_port_file(pty_slave_path);
    } else {
        sim_dbg("soynut sim: could not open a virtual serial port - "
                "GUI keyboard integration unavailable this run\n");
    }

    sim_dbg("soynut sim: nut_boot()...\n");
    nut_boot();
    assert(regPC == 0);

    static sim_state_t state; /* static: pending_snapshot alone is ~8.5KB - see firmware/main.c's identical reasoning */

    hp41_persist_state_t saved_state;
    if (hp41_persist_flash_load(&saved_state)) {
        hp41_persist_apply(&saved_state);
        state.asleep = true;
        sim_dbg("soynut sim: restored continuous memory from file\n");
    } else {
        sim_dbg("soynut sim: no valid persisted memory - MEMORY LOST cold start\n");
    }

    state.last_heartbeat_ms = sim_clock_now_ms();
    sim_dbg("soynut sim: entering main loop\n");

    bool quit = false;
    while (!quit) {
        uint32_t now_ms = sim_clock_now_ms();
        if (sim_keyboard_poll(now_ms))
            break;
        sim_drain_pty_bytes();

        sim_check_clrmem(&state);
        bool redraw_needed = sim_check_elite_toggles(&state);
        sim_heartbeat(&state, now_ms);

        sim_loop_action_t action = sim_handle_sleep_state(&state, now_ms, redraw_needed);
        if (action == SIM_LOOP_CONTINUE)
            continue;

        int cptinstr_before = cptinstr;
        int ret = 0;
        if (action == SIM_LOOP_RUN) {
            ret = sim_run_cpu(now_ms, &quit);
            if (quit)
                break;
        }

        int instructions_ran = cptinstr - cptinstr_before;
        assert(instructions_ran >= 0);
        if (instructions_ran > 0) {
            sim_clock_sleep_us((uint32_t)((instructions_ran * 1000000ULL) / TARGET_INSTRUCTIONS_PER_SEC));
        }

        sim_render_if_needed(&state, redraw_needed);

        if (ret == 1)
            sim_handle_powoff(&state, now_ms);
        if (ret == 2)
            sim_handle_invalid_opcode();
    }

    /* Unlike real hardware (which genuinely loses up to
     * PERSIST_SAVE_DELAY_MS of state on a real power yank - see
     * firmware/main.c's documented gap), the sim has a clean exit path:
     * flush any not-yet-idle-flushed save now rather than losing it. */
    if (state.persist_dirty) {
        sim_dbg("soynut sim: exiting - flushing pending continuous-memory save\n");
        hp41_persist_flash_save(&state.pending_snapshot);
    }

    /* Remove the discovery file so a subsequent run's wrapper script
     * (sim/run_with_gui.sh) never races against a stale leftover from
     * this run instead of waiting for a fresh one. */
    if (pty_ok) {
        remove(SIM_PORT_FILE_PATH);
    }

    SDL_Quit();
    return 0;
}
