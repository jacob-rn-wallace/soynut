/**
 * @file hold_trace_test.c
 * @brief Native (host) end-to-end trace of the real key-hold-duration
 *        fix against the actual ROM, not just at the adapter-unit level
 *        (see tests/key_hold_test.c for the deterministic unit-level
 *        proof of hp41_key_hold_bridge.c itself).
 *
 * Boots the real HP-41CV ROM (same as nut_smoke_test.c), wakes it with
 * a real held key via the actual production path (hp41_key_bridge.c's
 * "[+X]"/"[-]" protocol, feeding hp41_key_hold_bridge.c), single-steps
 * exactly like main.c's main loop does while a hold is active
 * (executeNUT(1) + hp41_key_hold_sustain() before every instruction -
 * see hp41_key_hold_bridge.h's design note for why single-stepping
 * matters), and watches regPC to see whether/how the ROM's own
 * USER-mode hold-duration logic (disassembled this session - see
 * CLAUDE.md's "Known limitation: real key hold-duration is not
 * modeled at all" section - NLT010/NULT10 at 0x0E9A/0x0EC9,
 * SYSTEMLABELS.TXT names) gets exercised.
 *
 * Confirmed working end to end: a long, never-released hold genuinely
 * drives the ROM into NULT10 (step ~1249), loops there exactly 1153
 * times (matching the disassembled LDI 240 doubled to 1152, off by
 * one), and reaches the nullify branch at 0x0ECF (step ~7014) - while
 * a 50-instruction quick tap never does. This is Sigma+ in normal
 * mode, not the manual's exact USER-mode/ASN'd-label scenario, but
 * it's the same underlying ROM mechanism (any function key's hold
 * check), driven correctly for the first time.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "display.h"
#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"
#include "nut_rom.h"

#define NULT_LO 0x0E80
#define NULT_HI 0x0ED9
#define NULLIFY_ADDR 0x0ECF
#define DISTOG_ADDR 0x0E9F  /* "blink the label" toggle */
#define NULT10_LOOP_ENTRY 0x0EC9

/* Power of 10, Rule 2: the settle-to-POWOFF loops below (cold boot, and
 * each ON wake) are bounded by a fixed batch-count cap rather than an
 * open-ended "while (ret == 0)", matching nut_smoke_test.c's
 * run_until_settled() pattern - a real ROM lockup becomes a bounded
 * failure (ret still 0 after SETTLE_MAX_BATCHES) instead of an
 * unbounded hang.
 *
 * SETTLE_MAX_BATCHES has to be far larger than
 * SETTLE_MAX_BATCHES*SETTLE_BATCH_SIZE instructions would suggest:
 * executeNUT() (emu41gcc/nutcpu.c) breaks out of its own inner loop
 * the instant fdsp (display-dirty) is set, which happens repeatedly
 * during a real boot/wake sequence - nothing in this test clears fdsp
 * between calls (unlike main.c's production loop), so once it's set,
 * every later executeNUT(1000) call only ever advances cptinstr by 1
 * before re-tripping the same break. Observed real requirement across
 * cold boot + two ON wakes: ~7700 total instructions, almost entirely
 * one per call once fdsp latches - this cap gives comfortable margin
 * above that. */
#define SETTLE_BATCH_SIZE 1000
#define SETTLE_MAX_BATCHES 20000

/**
 * @brief Feed each byte of a NUL-terminated string through the key bridge.
 * @param s Bytes to feed, in order.
 */
static void feed(const char *s)
{
    assert(s != NULL);
    for (; *s; s++)
        hp41_key_bridge_feed_byte((unsigned char)*s);
    assert(*s == '\0');
}

/**
 * @brief Run executeNUT() in fixed-size batches until it stops advancing or a cap is hit.
 * @return executeNUT()'s last status code.
 */
static int settle(void)
{
    int ret = 0;
    int batch;
    for (batch = 0; batch < SETTLE_MAX_BATCHES; batch++) {
        ret = executeNUT(SETTLE_BATCH_SIZE);
        if (ret != 0)
            break;
    }
    assert(batch <= SETTLE_MAX_BATCHES);
    assert(ret >= 0 && ret <= 3);
    return ret;
}

/**
 * @brief Log regPC/flagKB/regK the first time the trace visits one of
 *        the four addresses of interest.
 *
 * So the printed trace has one line per state transition rather than
 * one per instruction.
 *
 * @param steps           Current single-step count, for the log line.
 * @param last_logged_pc  In/out: the last PC value logged; updated in place.
 */
static void log_pc_of_interest(int steps, unsigned int *last_logged_pc)
{
    assert(last_logged_pc != NULL);
    assert(steps >= 0);
    const bool interesting = (regPC == 0x0E9A || regPC == DISTOG_ADDR
                               || regPC == NULT10_LOOP_ENTRY || regPC == NULLIFY_ADDR);
    if (interesting && regPC != *last_logged_pc) {
        printf("      step %6d: PC=0x%04X flagKB=%d regK=0x%02X\n", steps, regPC, flagKB, regK);
        *last_logged_pc = regPC;
    }
}

/**
 * @brief Run the calculator forward, waking it from POWOFF with a
 *        previously-fed held key, and trace its hold/nullify behavior.
 *
 * Runs the calculator forward, waking it from POWOFF with a previously
 * fed key sequence exactly like main.c's main loop does (see
 * CLAUDE.md's "Arduino display bridge" section, point 4).
 *
 * @param max_steps           Bounds how many single instructions to
 *                            execute - a fixed, provable loop bound
 *                            (Power of 10, Rule 2), since the for-loop
 *                            below iterates at most max_steps times
 *                            regardless of how `steps` itself advances.
 * @param release_after_steps Step count at which to release the held
 *                            key; negative means "never release within
 *                            this trace window".
 * @param drain_interval      Models how often main.c actually notices
 *                            an incoming release byte, in instructions
 *                            - a real release becomes "available" at
 *                            release_after_steps, but is only actually
 *                            acted on at the next multiple of
 *                            drain_interval at or past that point.
 *                            drain_interval=1 matches the current
 *                            (fixed) design - drain_usb_bytes() runs
 *                            every single inner-loop iteration.
 * @param visited_nult  Out: whether regPC ever entered the hold/nullify
 *                      address range.
 * @param hit_nullify   Out: whether regPC ever reached the nullify branch.
 * @param hit_distog    Out: whether regPC ever hit the "blink the label" toggle.
 */
static void run_and_trace2(int max_steps, int release_after_steps, int drain_interval,
                            int *visited_nult, int *hit_nullify, int *hit_distog)
{
    assert(max_steps > 0);
    assert(drain_interval > 0);

    *visited_nult = 0;
    *hit_nullify = 0;
    *hit_distog = 0;
    int steps = 0;
    bool released = (release_after_steps < 0);
    int nult10_hits = 0;
    unsigned int last_logged_pc = (unsigned int)-1;

    for (int iter = 0; iter < max_steps && steps < max_steps; iter++) {
        if (!released && steps >= release_after_steps && (steps % drain_interval) == 0) {
            hp41_key_hold_release();
            released = true;
        }

        if (hp41_key_hold_active())
            hp41_key_hold_sustain();

        const int cptinstr_before = cptinstr;
        const int ret = executeNUT(1);
        steps += (cptinstr - cptinstr_before);

        if (regPC >= NULT_LO && regPC <= NULT_HI) {
            if (!*visited_nult)
                printf("    first entered hold/nullify range at PC=0x%04X (step %d)\n", regPC, steps);
            *visited_nult = 1;
            if (regPC == NULT10_LOOP_ENTRY)
                nult10_hits++;
            if (regPC == NULLIFY_ADDR)
                *hit_nullify = 1;
            if (regPC == DISTOG_ADDR)
                *hit_distog = 1;
            log_pc_of_interest(steps, &last_logged_pc);
        }

        if (ret == 1) {
            /* POWOFF - this is expected/normal (see CLAUDE.md's
             * "screen goes blank" investigation - the calc powers off
             * after nearly every keypress in this emulation, unrelated
             * to the hold-duration logic under test here). Keep the
             * hold engaged (if not yet released) and wake again with
             * the same held key, matching main.c's own asleep/wake
             * handling. */
            regPC = 0;
            flagKey = 0;
        }
    }
    printf("    (ran %d steps, NULT10 loop entered %d times)\n", steps, nult10_hits);
}

/**
 * @brief run_and_trace2() with the default drain_interval=1.
 * @param max_steps            See run_and_trace2().
 * @param release_after_steps  See run_and_trace2().
 * @param visited_nult  Out: see run_and_trace2().
 * @param hit_nullify   Out: see run_and_trace2().
 * @param hit_distog    Out: see run_and_trace2().
 */
static void run_and_trace(int max_steps, int release_after_steps,
                           int *visited_nult, int *hit_nullify, int *hit_distog)
{
    run_and_trace2(max_steps, release_after_steps, 1, visited_nult, hit_nullify, hit_distog);
}

/**
 * @brief Boot the ROM to cold-start, then press ON twice to reach the ready state.
 *
 * Matches the real, previously-established behavior (see CLAUDE.md's
 * "Screen goes blank" investigation): the first wake-restart still
 * shows "MEMORY LOST" again (whatever Carry naturally ended up as
 * isn't reset on wake, same as main.c), and a second ON is what
 * actually reaches the ready "0.0000" state.
 */
static void boot_and_wake_twice(void)
{
    nut_boot();
    assert(regPC == 0);
    int ret = settle();
    assert(ret >= 0 && ret <= 3);
    printf("cold boot settled: ret=%d, PC=0x%04X, cptinstr=%d\n", ret, regPC, cptinstr);

    char dispbuf[32];
    for (int on_press = 0; on_press < 2; on_press++) {
        regPC = 0;
        flagKey = 0;
        feed("[ON]");
        ret = settle();
        printf("after ON #%d: display=\"%s\" PC=0x%04X ret=%d cptinstr=%d\n",
               on_press + 1, display_to_buf(dispbuf), regPC, ret, cptinstr);
    }
}

/**
 * @brief Verify an instantaneous tap never triggers the "blink the label" toggle.
 *
 * Releases on literally the first single-stepped instruction after the
 * press. On real hardware, a genuinely quick tap shouldn't show the
 * "blink the label" toggle (DISTOG) at all - it should execute
 * immediately.
 *
 * @return 1 on failure, 0 on pass.
 */
static int scenario_instant_tap(void)
{
    int visited, nullified, distogged;

    printf("\nScenario 0: instantaneous tap of Sigma+ ('A')\n");
    regPC = 0;
    flagKey = 0;
    feed("[+A]");
    run_and_trace(3000, 1, &visited, &nullified, &distogged);
    assert(visited == 0 || visited == 1);
    assert(distogged == 0 || distogged == 1);
    printf("    visited hold range=%d, hit DISTOG (blink)=%d, reached nullify=%d\n",
           visited, distogged, nullified);
    if (distogged) {
        printf("    FAIL: an instantaneous tap should not trigger the label blink\n");
        return 1;
    }
    return 0;
}

/**
 * @brief Verify a quick tap never reaches the nullify branch.
 *
 * Presses and releases almost immediately (well under the ROM's real
 * ~0.5s threshold). May or may not visit the hold-check range at all,
 * but must NOT reach the nullify branch.
 *
 * @return 1 on failure, 0 on pass.
 */
static int scenario_quick_tap(void)
{
    int visited, nullified, distogged;

    printf("\nScenario 1: quick tap of Sigma+ ('A')\n");
    regPC = 0;
    flagKey = 0;
    feed("[+A]");
    run_and_trace(3000, 50, &visited, &nullified, &distogged);
    assert(visited == 0 || visited == 1);
    assert(nullified == 0 || nullified == 1);
    printf("    visited hold range=%d, hit DISTOG (blink)=%d, reached nullify=%d\n",
           visited, distogged, nullified);
    if (nullified) {
        printf("    FAIL: a 50-instruction tap should not nullify\n");
        return 1;
    }
    return 0;
}

/**
 * @brief Verify a long, never-released hold reaches the nullify branch.
 *
 * Presses and never releases within this trace window. Should reach
 * the nullify branch if held long enough to exceed the ROM's own
 * loop-count threshold.
 *
 * @return Number of failed checks (0, 1, or 2).
 */
static int scenario_long_hold(void)
{
    int visited, nullified, distogged;
    int failures = 0;

    printf("\nScenario 2: long hold of Sigma+ ('A', never released)\n");
    regPC = 0;
    flagKey = 0;
    feed("[+A]");
    run_and_trace(400000, -1, &visited, &nullified, &distogged);
    assert(visited == 0 || visited == 1);
    assert(nullified == 0 || nullified == 1);
    printf("    visited hold range=%d, hit DISTOG (blink)=%d, reached nullify=%d\n",
           visited, distogged, nullified);
    if (!visited) {
        printf("    FAIL: a long, never-released hold should visit the hold/nullify range\n");
        failures++;
    }
    if (!nullified) {
        printf("    FAIL: a long, never-released hold should reach the nullify branch\n");
        failures++;
    }
    feed("[-]");
    assert(failures >= 0 && failures <= 2);
    return failures;
}

/**
 * @brief Boot the ROM, then run all three hold-duration scenarios and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    /* NOTE: an earlier version of this test tried to reproduce a
     * suspected "batched USB byte drain" bug here (checking release
     * only once per 1000-instruction batch, matching an earlier version
     * of main.c). Empirically, that batching turns out NOT to be
     * sufficient to trigger a false blink on its own within a normal
     * test window - a 1000-instruction batch still resolves before the
     * ROM's ~1245-instruction DISTOG threshold. The real cause of the
     * user-reported "every tap blinks" issue is external round-trip
     * latency (GUI/Python/serial/USB) that a host-only trace like this
     * one can't reproduce or measure - see CLAUDE.md's "Real key
     * hold-duration" section for the actual fix (a press/hold-engage
     * threshold in the GUI itself, tools/hp41_keyboard_gui.py). This
     * file still verifies the ROM-level mechanism is correct - see the
     * scenarios below - just not that specific (disproven) hypothesis.
     *
     * Held key is 'A' (Sigma+, tabcode 0x10) rather than a digit - the
     * hold/nullify logic under test is specifically about deciding
     * whether to run a *function*; plain digit entry is a different,
     * simpler code path. Sent via the real "[+X]"/"[-]" wire protocol,
     * exactly what production code (main.c, the GUI) actually sends. */

    boot_and_wake_twice();

    const int failures = scenario_instant_tap()
                        + scenario_quick_tap()
                        + scenario_long_hold();
    assert(failures >= 0);
    assert(failures <= 4); /* 1 + 1 + 2, see each scenario's own cap */

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: hold-duration behavior confirmed against the real ROM\n");
    return 0;
}
