/* Native (host) test for firmware/hp41_key_hold_bridge.c - the
 * deterministic, fully-provable half of this session's key-hold-
 * duration fix (see CLAUDE.md's "Known limitation: real key
 * hold-duration is not modeled at all" section).
 *
 * Doesn't need the Nut CPU running at all - just nutcpu.h's
 * flagKB/regK/keybuffer[]/lgkeybuf storage (via
 * emu41gcc_compat/nut_globals.c), hp41_key_bridge.c (for the
 * "[+X]"/"[-]" wire protocol), and hp41_key_hold_bridge.c itself.
 * Doesn't call dokey()/executeNUT() at all - this file proves the one
 * thing hp41_key_hold_bridge.c is actually responsible for: that
 * hp41_key_hold_sustain() correctly re-asserts flagKB/regK for as long
 * as a hold is in progress, and stops the instant it's released. (See
 * tests/hold_trace_test.c for the separate, real-ROM-level empirical
 * exploration of what the ROM does with that signal.)
 *
 * Build (from repo root):
 *   cc -std=gnu11 -fcommon -Iemu41gcc -Ifirmware/emu41gcc_compat -Ifirmware \
 *      -o tests/build/key_hold_test tests/key_hold_test.c \
 *      firmware/emu41gcc_compat/nut_globals.c \
 *      firmware/hp41_key_bridge.c \
 *      firmware/hp41_key_hold_bridge.c
 *   ./tests/build/key_hold_test
 */

#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"

static int failures = 0;

static void reset(void)
{
    lgkeybuf = 0;
    memset(keybuffer, 0, sizeof(keybuffer));
    hp41_key_bridge_reset();
    hp41_key_hold_release();
    flagKB = 0;
    regK = 0;
}

static void feed(const char *s)
{
    for (; *s; s++)
        hp41_key_bridge_feed_byte((unsigned char)*s);
}

static void check(const char *label, int cond)
{
    printf("%-58s %s\n", label, cond ? "OK" : "MISMATCH");
    if (!cond) failures++;
}

int main(void)
{
    /* "[+A]" should push keycode 0x10 (Sigma+) into keybuffer[] exactly
     * once, same as a plain 'A' would, and begin a real hold. */
    reset();
    feed("[+A]");
    check("\"[+A]\" queues exactly one key (0x10)",
          lgkeybuf == 1 && (unsigned char)keybuffer[0] == 0x10);
    check("\"[+A]\" begins a real hold", hp41_key_hold_active());

    /* Simulate the ROM clearing flagKB/regK on its own (exactly what
     * the RSTKB opcode does once its internal FSM decides the "press"
     * window has elapsed - see hp41_key_hold_bridge.h's design note),
     * then confirm hp41_key_hold_sustain() re-asserts them - this is
     * the actual mechanism main.c relies on, called once per
     * single-stepped instruction while holding. */
    flagKB = 0;
    regK = 0;
    hp41_key_hold_sustain();
    check("sustain() re-asserts flagKB=1/regK=0x10 after a simulated clear",
          flagKB == 1 && regK == 0x10);

    /* Repeat many times, simulating many single-stepped instructions -
     * must stay sustained for arbitrarily long, unlike emu41gcc's
     * unmodified dokey(), which auto-releases after a fixed ~200
     * instructions regardless of real duration. */
    int all_sustained = 1;
    for (int i = 0; i < 1000; i++) {
        flagKB = 0; /* simulate the ROM trying to clear it again */
        regK = 0;
        hp41_key_hold_sustain();
        if (flagKB != 1 || regK != 0x10) all_sustained = 0;
    }
    check("sustained across 1000 simulated clear/reassert cycles", all_sustained);

    /* Release: sustain() must stop re-asserting immediately, letting
     * whatever the ROM itself set stand. */
    feed("[-]");
    check("\"[-]\" ends the hold", !hp41_key_hold_active());
    flagKB = 0;
    regK = 0;
    hp41_key_hold_sustain();
    check("sustain() is a no-op after release", flagKB == 0 && regK == 0);

    /* A second press/hold cycle should work identically - the bridge
     * isn't a one-shot. */
    reset();
    feed("[+5]"); /* single-char fallback: '5' resolved via tabcode[], not named_keys[] */
    check("\"[+5]\" queues tabcode['5'] (0x75)",
          lgkeybuf == 1 && (unsigned char)keybuffer[0] == 0x75);
    flagKB = 0;
    regK = 0;
    hp41_key_hold_sustain();
    check("second cycle: sustain() asserts flagKB=1/regK=0x75",
          flagKB == 1 && regK == 0x75);
    feed("[-]");
    check("second cycle: released cleanly", !hp41_key_hold_active());

    /* Two-code named keys (BST) have no single "held" state - "[+BST]"
     * should be silently ignored (no hold begins), same policy as an
     * unrecognized name. */
    reset();
    feed("[+BST]");
    check("\"[+BST]\" is ignored (two-code combo, no meaningful hold)",
          lgkeybuf == 0 && !hp41_key_hold_active());

    /* Existing "[NAME]" (no +/-) behavior must be completely unaffected. */
    reset();
    feed("[ON]");
    check("\"[ON]\" still works unchanged (no hold semantics)",
          lgkeybuf == 1 && (unsigned char)keybuffer[0] == 0x18 && !hp41_key_hold_active());

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
