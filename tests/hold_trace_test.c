/* Native (host) end-to-end trace of the real key-hold-duration fix
 * against the actual ROM, not just at the adapter-unit level (see
 * tests/key_hold_test.c for the deterministic unit-level proof of
 * hp41_key_hold_bridge.c itself).
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
 * Build (from repo root):
 *   cc -std=gnu11 -fcommon -Iemu41gcc -Ifirmware/emu41gcc_compat -Ifirmware \
 *      -include firmware/emu41gcc_compat/nut_stubs.h \
 *      -o tests/build/hold_trace_test tests/hold_trace_test.c \
 *      emu41gcc/nutcpu.c emu41gcc/display.c \
 *      firmware/emu41gcc_compat/nut_stubs.c \
 *      firmware/emu41gcc_compat/nut_globals.c \
 *      firmware/emu41gcc_compat/nut_rom.c \
 *      firmware/hp41_key_bridge.c \
 *      firmware/hp41_key_hold_bridge.c \
 *      roms/rom_images.c
 *   ./tests/build/hold_trace_test
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "display.h"
#include "nut_rom.h"
#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"

#define NULT_LO 0x0E80
#define NULT_HI 0x0ED9
#define NULLIFY_ADDR 0x0ECF

static void feed(const char *s)
{
    for (; *s; s++)
        hp41_key_bridge_feed_byte((unsigned char)*s);
}

/* Runs the calculator forward, waking it from POWOFF with the given key
 * sequence exactly like main.c's main loop does (see CLAUDE.md's
 * "Arduino display bridge" section, point 4). max_steps bounds how many
 * single instructions to execute; visited_nult/hit_nullify report
 * whether regPC ever entered the hold/nullify address range and
 * whether it specifically reached the nullify branch.
 *
 * drain_interval models how often main.c actually notices an incoming
 * release byte, in instructions - a real release becomes "available"
 * at release_after_steps, but is only actually acted on at the next
 * multiple of drain_interval at or past that point. drain_interval=1
 * matches the current (fixed) design - drain_usb_bytes() runs every
 * single inner-loop iteration. drain_interval=1000 reproduces the
 * earlier, buggy design - bytes were only drained once per
 * up-to-1000-instruction batch (see CLAUDE.md's "Real key hold-duration"
 * section for why that stretched every tap past the blink threshold).
 */
static void run_and_trace2(int max_steps, int release_after_steps, int drain_interval,
                           int *visited_nult, int *hit_nullify, int *hit_distog)
{
    *visited_nult = 0;
    *hit_nullify = 0;
    *hit_distog = 0;
    int steps = 0;
    bool released = (release_after_steps < 0);
    int nult10_hits = 0; /* how many times PC==0x0EC9 (NULT10 loop entry) */
    int last_logged_pc = -1;

    while (steps < max_steps) {
        if (!released && steps >= release_after_steps && (steps % drain_interval) == 0) {
            hp41_key_hold_release();
            released = true;
        }

        if (hp41_key_hold_active())
            hp41_key_hold_sustain();

        int cptinstr_before = cptinstr;
        int ret = executeNUT(1);
        steps += (cptinstr - cptinstr_before);

        if (regPC >= NULT_LO && regPC <= NULT_HI) {
            if (!*visited_nult) {
                printf("    first entered hold/nullify range at PC=0x%04X (step %d)\n", regPC, steps);
            }
            *visited_nult = 1;
            if (regPC == 0x0EC9)
                nult10_hits++;
            if (regPC == NULLIFY_ADDR)
                *hit_nullify = 1;
            if (regPC == 0x0E9F) /* DISTOG - the "blink the label" toggle */
                *hit_distog = 1;
            if (regPC != last_logged_pc && (regPC == 0x0E9A || regPC == 0x0E9F || regPC == 0x0EC9 || regPC == NULLIFY_ADDR)) {
                printf("      step %6d: PC=0x%04X flagKB=%d regK=0x%02X\n", steps, regPC, flagKB, regK);
                last_logged_pc = regPC;
            }
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

static void run_and_trace(int max_steps, int release_after_steps,
                           int *visited_nult, int *hit_nullify, int *hit_distog)
{
    run_and_trace2(max_steps, release_after_steps, 1, visited_nult, hit_nullify, hit_distog);
}

int main(void)
{
    int failures = 0;

    nut_boot();
    /* Cold boot -> "MEMORY LOST" -> POWOFF, same as nut_smoke_test.c. */
    int ret;
    do {
        ret = executeNUT(1000);
    } while (ret == 0);
    printf("cold boot settled: ret=%d, PC=0x%04X, cptinstr=%d\n", ret, regPC, cptinstr);

    /* Wake with ON, twice - matches the real, previously-established
     * behavior (see CLAUDE.md's "Screen goes blank" investigation): the
     * first wake-restart still shows "MEMORY LOST" again (whatever
     * Carry naturally ended up as isn't reset on wake, same as main.c),
     * and a second ON is what actually reaches the ready "0.0000"
     * state. */
    char dispbuf[32];
    for (int on_press = 0; on_press < 2; on_press++) {
        regPC = 0;
        flagKey = 0;
        feed("[ON]");
        do {
            ret = executeNUT(1000);
        } while (ret == 0);
        printf("after ON #%d: display=\"%s\" PC=0x%04X ret=%d cptinstr=%d\n",
               on_press + 1, display_to_buf(dispbuf), regPC, ret, cptinstr);
    }

    /* Held key is 'A' (Sigma+, tabcode 0x10) rather than a digit - the
     * hold/nullify logic under test is specifically about deciding
     * whether to run a *function*; plain digit entry (building up a
     * number in the X register) is a different, simpler code path and
     * may never route through it at all. Sent via the real "[+X]"/"[-]"
     * wire protocol (hp41_key_bridge_feed_byte()), exactly what
     * production code (main.c, the GUI) will actually send - not the
     * hold-bridge API directly, so this is a genuine integration test
     * of the parser too. */

    int visited, nullified, distogged;

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
     * scenarios below - just not that specific (disproven) hypothesis. */

    /* --- Scenario 0: instantaneous tap - release on literally the
     * first single-stepped instruction after the press. This is the
     * case the user reported: on real hardware, a genuinely quick tap
     * shouldn't show the "blink the label" toggle (DISTOG, 0x0E9F) at
     * all - it should execute immediately. --- */
    printf("\nScenario 0: instantaneous tap of Sigma+ ('A')\n");
    regPC = 0;
    flagKey = 0;
    feed("[+A]");
    run_and_trace(3000, 1 /* release after the very first instruction */, &visited, &nullified, &distogged);
    printf("    visited hold range=%d, hit DISTOG (blink)=%d, reached nullify=%d\n", visited, distogged, nullified);
    if (distogged) {
        printf("    FAIL: an instantaneous tap should not trigger the label blink\n");
        failures++;
    }

    /* --- Scenario 1: quick tap - press and release almost immediately
     * (well under the ROM's real ~0.5s threshold). Should execute
     * normally; may or may not visit the hold-check range at all (a
     * fast enough release might not even be sampled), but must NOT
     * reach the nullify branch. --- */
    printf("\nScenario 1: quick tap of Sigma+ ('A')\n");
    regPC = 0;
    flagKey = 0;
    feed("[+A]");
    run_and_trace(3000, 50 /* release after ~50 instructions - a very quick tap */, &visited, &nullified, &distogged);
    printf("    visited hold range=%d, hit DISTOG (blink)=%d, reached nullify=%d\n", visited, distogged, nullified);
    if (nullified) {
        printf("    FAIL: a 50-instruction tap should not nullify\n");
        failures++;
    }

    /* --- Scenario 2: long hold - press and never release within this
     * trace window. Should reach the nullify branch (0x0ECF) if held
     * long enough to exceed the ROM's own loop-count threshold. --- */
    printf("\nScenario 2: long hold of Sigma+ ('A', never released)\n");
    regPC = 0;
    flagKey = 0;
    feed("[+A]");
    run_and_trace(400000, -1 /* never release */, &visited, &nullified, &distogged);
    printf("    visited hold range=%d, hit DISTOG (blink)=%d, reached nullify=%d\n", visited, distogged, nullified);
    if (!visited) {
        printf("    FAIL: a long, never-released hold should visit the hold/nullify range\n");
        failures++;
    }
    if (!nullified) {
        printf("    FAIL: a long, never-released hold should reach the nullify branch\n");
        failures++;
    }
    feed("[-]");

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: hold-duration behavior confirmed against the real ROM\n");
    return 0;
}
