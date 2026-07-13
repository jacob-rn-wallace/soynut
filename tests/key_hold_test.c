/**
 * @file key_hold_test.c
 * @brief Native (host) test for firmware/hp41_key_hold_bridge.c - the
 *        deterministic, fully-provable half of this session's
 *        key-hold-duration fix (see CLAUDE.md's "Known limitation: real
 *        key hold-duration is not modeled at all" section).
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
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"

/* Power of 10, Rule 2: fixed, provable bound for the "stays sustained
 * indefinitely" loop below - large enough to distinguish "sustained
 * forever" from emu41gcc's unmodified dokey(), which auto-releases
 * after a fixed ~200 instructions regardless of real duration. */
#define SUSTAIN_CYCLES 1000

/**
 * @brief Reset keybuffer[]/lgkeybuf, the key bridge, and hold state to idle.
 */
static void reset(void)
{
    lgkeybuf = 0;
    memset(keybuffer, 0, sizeof(keybuffer));
    hp41_key_bridge_reset();
    hp41_key_hold_release();
    flagKB = 0;
    regK = 0;
    assert(lgkeybuf == 0);
    assert(!hp41_key_hold_active());
}

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
 * @brief Print a labeled pass/fail line for one boolean check.
 * @param label Human-readable name for this check.
 * @param cond  The check's result (0 or 1).
 * @return @p cond, unchanged.
 */
static int check(const char *label, int cond)
{
    assert(label != NULL);
    assert(cond == 0 || cond == 1); /* C's logical/relational operators always yield 0 or 1 */
    printf("%-58s %s\n", label, cond ? "OK" : "MISMATCH");
    return cond;
}

/** "[+A]" should push keycode 0x10 (Sigma+) into keybuffer[] exactly
 *  once, same as a plain 'A' would, and begin a real hold; sustain()
 *  must then keep re-asserting flagKB/regK across any number of
 *  simulated ROM clears, and stop the instant it's released. */
#define PRESS_SUSTAIN_RELEASE_CHECK_COUNT 6

/**
 * @brief Verify a full press/sustain/release cycle via the "[+X]"/"[-]" protocol.
 * @return Number of failed checks (0 = all pass).
 */
static int test_press_sustain_release(void)
{
    int failures = 0;
    assert(failures == 0);

    reset();
    feed("[+A]");
    failures += !check("\"[+A]\" queues exactly one key (0x10)",
                        lgkeybuf == 1 && (unsigned char)keybuffer[0] == 0x10);
    failures += !check("\"[+A]\" begins a real hold", hp41_key_hold_active());

    /* Simulate the ROM clearing flagKB/regK on its own (exactly what
     * the RSTKB opcode does once its internal FSM decides the "press"
     * window has elapsed - see hp41_key_hold_bridge.h's design note),
     * then confirm sustain() re-asserts them - the actual mechanism
     * main.c relies on, called once per single-stepped instruction
     * while holding. */
    flagKB = 0;
    regK = 0;
    hp41_key_hold_sustain();
    failures += !check("sustain() re-asserts flagKB=1/regK=0x10 after a simulated clear",
                        flagKB == 1 && regK == 0x10);

    int all_sustained = 1;
    for (int i = 0; i < SUSTAIN_CYCLES; i++) {
        flagKB = 0; /* simulate the ROM trying to clear it again */
        regK = 0;
        hp41_key_hold_sustain();
        if (flagKB != 1 || regK != 0x10) all_sustained = 0;
    }
    failures += !check("sustained across 1000 simulated clear/reassert cycles", all_sustained);

    feed("[-]");
    failures += !check("\"[-]\" ends the hold", !hp41_key_hold_active());
    flagKB = 0;
    regK = 0;
    hp41_key_hold_sustain();
    failures += !check("sustain() is a no-op after release", flagKB == 0 && regK == 0);

    assert(failures >= 0 && failures <= PRESS_SUSTAIN_RELEASE_CHECK_COUNT);
    return failures;
}

/** A second, independent press/hold/release cycle - the bridge isn't a
 *  one-shot - using the single-character tabcode[] fallback ('5')
 *  rather than a named_keys[] entry. */
#define SECOND_CYCLE_CHECK_COUNT 3

/**
 * @brief Verify a second, independent press/hold/release cycle works.
 * @return Number of failed checks (0 = all pass).
 */
static int test_second_cycle(void)
{
    int failures = 0;
    assert(failures == 0);

    reset();
    feed("[+5]");
    failures += !check("\"[+5]\" queues tabcode['5'] (0x75)",
                        lgkeybuf == 1 && (unsigned char)keybuffer[0] == 0x75);
    flagKB = 0;
    regK = 0;
    hp41_key_hold_sustain();
    failures += !check("second cycle: sustain() asserts flagKB=1/regK=0x75",
                        flagKB == 1 && regK == 0x75);
    feed("[-]");
    failures += !check("second cycle: released cleanly", !hp41_key_hold_active());

    assert(failures >= 0 && failures <= SECOND_CYCLE_CHECK_COUNT);
    return failures;
}

/** Edge cases: a two-code chord can't be meaningfully held, and plain
 *  "[NAME]" (no +/-) must keep working exactly as before. */
#define EDGE_CASE_CHECK_COUNT 2

/**
 * @brief Verify two-code-chord hold requests are ignored, and plain
 *        "[NAME]" presses are unaffected by the hold protocol.
 * @return Number of failed checks (0 = all pass).
 */
static int test_edge_cases(void)
{
    int failures = 0;
    assert(failures == 0);

    reset();
    feed("[+BST]");
    failures += !check("\"[+BST]\" is ignored (two-code combo, no meaningful hold)",
                        lgkeybuf == 0 && !hp41_key_hold_active());

    reset();
    feed("[ON]");
    failures += !check("\"[ON]\" still works unchanged (no hold semantics)",
                        lgkeybuf == 1 && (unsigned char)keybuffer[0] == 0x18
                            && !hp41_key_hold_active());

    assert(failures >= 0 && failures <= EDGE_CASE_CHECK_COUNT);
    return failures;
}

#define TOTAL_CHECK_COUNT (PRESS_SUSTAIN_RELEASE_CHECK_COUNT + SECOND_CYCLE_CHECK_COUNT \
                           + EDGE_CASE_CHECK_COUNT)

/**
 * @brief Run all key-hold check groups and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_press_sustain_release()
                        + test_second_cycle()
                        + test_edge_cases();
    assert(failures >= 0);
    assert(failures <= TOTAL_CHECK_COUNT);

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
