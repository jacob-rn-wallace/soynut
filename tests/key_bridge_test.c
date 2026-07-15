/**
 * @file key_bridge_test.c
 * @brief Native (host) test for firmware/hp41_key_bridge.c.
 *
 * Doesn't need the Nut CPU running at all - just nutcpu.h's
 * keybuffer[]/lgkeybuf storage (via emu41gcc_compat/nut_globals.c) and
 * the key bridge itself. Feeds bytes one at a time through
 * hp41_key_bridge_feed_byte() and checks keybuffer[] against what
 * emu41gcc/emu41.c's own traite_touche()/tabcode[] would produce for
 * the same input, plus the bracketed named-key protocol this project
 * added for keys with no ASCII equivalent (ON, SHIFT, USER, PRGM, SST,
 * BST, X<>Y, RDN). Doesn't cover the newer "[+X]"/"[-]" hold/release
 * escapes - see tests/key_hold_test.c for those.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_key_bridge.h"

/* keybuffer[]'s real capacity (see hp41_key_bridge.h / CLAUDE.md's "Key
 * bridge" section) - the fixed bound every check() call below is
 * provably within. */
#define KEYBUFFER_CAP 8

/**
 * @brief Reset keybuffer[]/lgkeybuf and the key bridge's escape state to idle.
 */
static void reset(void)
{
    assert(sizeof(keybuffer) >= KEYBUFFER_CAP);
    lgkeybuf = 0;
    memset(keybuffer, 0, sizeof(keybuffer));
    hp41_key_bridge_reset();
    assert(lgkeybuf == 0);
}

/**
 * @brief Feed each byte of a NUL-terminated string through the key bridge.
 * @param s Bytes to feed, in order.
 */
static void feed_string(const char *s)
{
    assert(s != NULL);
    for (; *s; s++)
        hp41_key_bridge_feed_byte((unsigned char)*s);
    assert(*s == '\0');
}

/**
 * @brief Compare keybuffer[0..lgkeybuf) against an expected keycode sequence.
 *
 * Prints a diagnostic (got vs. want) on mismatch.
 *
 * @param label     Human-readable name for this check, for the printed report.
 * @param want      Expected keycodes, or NULL only when want_len == 0.
 * @param want_len  Expected keybuffer length.
 * @return 1 on match, 0 on mismatch.
 */
static int check(const char *label, const unsigned char *want, int want_len)
{
    assert(label != NULL);
    assert(want_len >= 0 && want_len <= KEYBUFFER_CAP);

    int ok = (lgkeybuf == want_len);
    if (ok) {
        for (int i = 0; i < want_len; i++)
            if ((unsigned char)keybuffer[i] != want[i]) ok = 0;
    }
    printf("%-28s lgkeybuf=%d want=%d  %s\n", label, lgkeybuf, want_len, ok ? "OK" : "MISMATCH");
    if (!ok) {
        printf("   got: ");
        for (int i = 0; i < lgkeybuf; i++) printf("0x%02X ", (unsigned char)keybuffer[i]);
        printf("\n   want:");
        for (int i = 0; i < want_len; i++) printf(" 0x%02X", want[i]);
        printf("\n");
    }
    return ok;
}

/** Direct ASCII keys - digits, operators, Enter, ctrl-chars, and an
 *  unmapped byte that should push nothing. */
#define DIRECT_ASCII_CHECK_COUNT 8

/**
 * @brief Verify plain ASCII bytes map to their documented HP-41 keycodes.
 * @return Number of failed checks (0 = all pass).
 */
static int test_direct_ascii_keys(void)
{
    int failures = 0;
    assert(failures == 0);

    reset();
    feed_string("5");
    failures += !check("digit '5'", (unsigned char[]){0x75}, 1);

    reset();
    feed_string("+");
    failures += !check("operator '+'", (unsigned char[]){0x15}, 1);

    reset();
    feed_string("\r"); /* Enter/CR */
    failures += !check("Enter (CR)", (unsigned char[]){0x13}, 1);

    reset();
    feed_string("\b"); /* Backspace -> CLX */
    failures += !check("Backspace (CLX)", (unsigned char[]){0xc3}, 1);

    reset();
    hp41_key_bridge_feed_byte(1); /* ctrl-A -> ALPHA */
    failures += !check("ctrl-A (ALPHA)", (unsigned char[]){0xc4}, 1);

    reset();
    hp41_key_bridge_feed_byte(18); /* ctrl-R -> R/S */
    failures += !check("ctrl-R (R/S)", (unsigned char[]){0x87}, 1);

    reset();
    hp41_key_bridge_feed_byte(24); /* ctrl-X -> XEQ */
    failures += !check("ctrl-X (XEQ)", (unsigned char[]){0x32}, 1);

    reset();
    feed_string("@"); /* unmapped ASCII should push nothing */
    failures += !check("unmapped '@'", NULL, 0);

    assert(failures >= 0 && failures <= DIRECT_ASCII_CHECK_COUNT);
    return failures;
}

/** Named-key escape protocol ("[NAME]"), including the one two-code
 *  chord (BST) and case-insensitivity. */
#define NAMED_KEY_CHECK_COUNT 4

/**
 * @brief Verify the "[NAME]" escape protocol for keys with no ASCII equivalent.
 * @return Number of failed checks (0 = all pass).
 */
static int test_named_key_protocol(void)
{
    int failures = 0;
    assert(failures == 0);

    reset();
    feed_string("[ON]");
    failures += !check("[ON]", (unsigned char[]){0x18}, 1);

    reset();
    feed_string("[shift]"); /* lowercase - must be case-insensitive */
    failures += !check("[shift] (lowercase)", (unsigned char[]){0x12}, 1);

    reset();
    feed_string("[BST]");
    failures += !check("[BST] (SHIFT+SST chord)", (unsigned char[]){0x12, 0xc2}, 2);

    reset();
    feed_string("[XY]");
    failures += !check("[XY] (X<>Y)", (unsigned char[]){0x11}, 1);

    assert(failures >= 0 && failures <= NAMED_KEY_CHECK_COUNT);
    return failures;
}

/** Malformed bracket sequences must drop cleanly, without corrupting
 *  whatever input follows them. */
#define MALFORMED_CHECK_COUNT 3

/**
 * @brief Verify malformed "[...]" sequences recover cleanly.
 * @return Number of failed checks (0 = all pass).
 */
static int test_malformed_sequences(void)
{
    int failures = 0;
    assert(failures == 0);

    reset();
    feed_string("[NOTAREALKEY]5");
    failures += !check("unrecognized name then '5'", (unsigned char[]){0x75}, 1);

    reset();
    feed_string("[ON[USER]"); /* nested '[' abandons the first, starts fresh */
    failures += !check("nested '[' recovery", (unsigned char[]){0xc6}, 1);

    reset();
    feed_string("[ON"); /* never closed */
    failures += !check("unterminated '['", NULL, 0);

    assert(failures >= 0 && failures <= MALFORMED_CHECK_COUNT);
    return failures;
}

/** A realistic multi-key sequence, plus keybuffer[]'s fixed 8-slot cap
 *  (extra presses beyond 8 pending are silently dropped, matching
 *  emu41gcc's own push_key()). */
#define MULTI_KEY_CHECK_COUNT 2

/**
 * @brief Verify a realistic multi-key sequence and the 8-slot buffer cap.
 * @return Number of failed checks (0 = all pass).
 */
static int test_multi_key_and_cap(void)
{
    int failures = 0;
    assert(failures == 0);

    reset();
    feed_string("123[ON]\r"); /* as if typing "123" then ON then Enter */
    failures += !check("\"123[ON]\\r\"",
                        (unsigned char[]){0x36, 0x76, 0x86, 0x18, 0x13}, 5);

    reset();
    feed_string("123456789"); /* 9 digits, but only 8 fit */
    failures += !check("9 digits -> capped at 8", (unsigned char[]){
        0x36, 0x76, 0x86, 0x35, 0x75, 0x85, 0x34, 0x74 /* '9' dropped */
    }, KEYBUFFER_CAP);

    assert(failures >= 0 && failures <= MULTI_KEY_CHECK_COUNT);
    return failures;
}

/**
 * @brief Compare a boolean accessor's result against an expected value.
 *
 * Same reporting style as check() above, for the one-shot toggle
 * accessors (hp41_key_bridge_elite_mode_toggle_requested() and
 * hp41_key_bridge_alpha_row_toggle_requested()), which don't produce a
 * keybuffer[] result to compare.
 *
 * @param label Human-readable name for this check.
 * @param got   Actual value.
 * @param want  Expected value.
 * @return 1 on match, 0 on mismatch.
 */
static int check_bool(const char *label, bool got, bool want)
{
    assert(label != NULL);
    int ok = (got == want);
    printf("%-28s got=%d want=%d  %s\n", label, got, want, ok ? "OK" : "MISMATCH");
    return ok;
}

/** Elite User Mode's default-disabled production behavior, its
 *  XEQ-ALPHA-LEET-ALPHA trigger sequence (once explicitly enabled), its
 *  case-insensitivity, near-miss/broken-sequence non-interference with
 *  real key sequences, the "[LEET]" alias, the bare-ALPHA alpha-row
 *  sub-toggle, and the "[NAME]" bracket-escape form regression (the
 *  real bug the on-screen keyboard GUI hit on real hardware). */
#define ELITE_MODE_CHECK_COUNT 23

/**
 * @brief Verify the Elite User Mode trigger sequence and alpha-row sub-toggle.
 * @return Number of failed checks (0 = all pass).
 */
static int test_elite_mode_trigger(void)
{
    int failures = 0;
    assert(failures == 0);

    /* Production default: the feature is currently disabled (real
     * display bugs found on hardware, see CLAUDE.md's "Elite User Mode"
     * section) - confirm the full trigger sequence is a complete no-op,
     * pushing every keycode (including the would-be-swallowed closing
     * ALPHA) exactly like any other unrelated sequence, before enabling
     * it for the rest of this test to keep exercising the underlying
     * logic. */
    reset();
    feed_string("\x18\x01LEET\x01");
    failures += !check("feature disabled by default: keybuffer unaffected",
                        (unsigned char[]){0x32, 0xc4, 0x72, 0xc0, 0xc0, 0x84, 0xc4}, 7);
    failures += !check_bool("  toggle_requested() while disabled",
                             hp41_key_bridge_elite_mode_toggle_requested(), false);

    hp41_key_bridge_set_elite_mode_feature_enabled(true);

    /* Full sequence: XEQ, ALPHA, L, E, E, T, ALPHA - the closing ALPHA
     * is swallowed (not pushed), and the toggle flag fires exactly once. */
    reset();
    feed_string("\x18\x01LEET\x01");
    failures += !check("XEQ ALPHA LEET ALPHA -> keybuffer",
                        (unsigned char[]){0x32, 0xc4, 0x72, 0xc0, 0xc0, 0x84}, 6);
    failures += !check_bool("  toggle_requested() first call",
                             hp41_key_bridge_elite_mode_toggle_requested(), true);
    failures += !check_bool("  toggle_requested() second call (one-shot)",
                             hp41_key_bridge_elite_mode_toggle_requested(), false);

    /* Near-miss: a real, different program name ("LEER" not "LEET") -
     * every byte, including the closing ALPHA, must reach keybuffer[]
     * exactly as if this trigger didn't exist at all. */
    reset();
    feed_string("\x18\x01LEER\x01");
    failures += !check("XEQ ALPHA LEER ALPHA -> unaffected",
                        (unsigned char[]){0x32, 0xc4, 0x72, 0xc0, 0xc0, 0x34, 0xc4}, 7);
    failures += !check_bool("  toggle_requested() after near-miss",
                             hp41_key_bridge_elite_mode_toggle_requested(), false);

    /* Case-insensitive on the letters. */
    reset();
    feed_string("\x18\x01leet\x01");
    failures += !check_bool("lowercase 'leet' triggers",
                             hp41_key_bridge_elite_mode_toggle_requested(), true);

    /* Broken mid-sequence by an unrelated byte - resets shadow tracking;
     * every byte still reaches keybuffer[] normally, no trigger. */
    reset();
    feed_string("\x18\x01L5\x01");
    failures += !check("XEQ ALPHA L '5' ALPHA -> unaffected",
                        (unsigned char[]){0x32, 0xc4, 0x72, 0x75, 0xc4}, 5);
    failures += !check_bool("  toggle_requested() after broken sequence",
                             hp41_key_bridge_elite_mode_toggle_requested(), false);

    /* Re-triggerable: two full sequences in a row. */
    reset();
    feed_string("\x18\x01LEET\x01");
    failures += !check_bool("first of two toggles",
                             hp41_key_bridge_elite_mode_toggle_requested(), true);
    feed_string("\x18\x01LEET\x01");
    failures += !check_bool("second of two toggles",
                             hp41_key_bridge_elite_mode_toggle_requested(), true);

    /* "[LEET]" bracket-escape alias - nothing pushed to keybuffer[]. */
    reset();
    feed_string("[LEET]");
    failures += !check("[LEET] alias -> nothing pushed", NULL, 0);
    failures += !check_bool("  toggle_requested() after [LEET]",
                             hp41_key_bridge_elite_mode_toggle_requested(), true);

    /* Bare ALPHA while Elite Mode is active is intercepted (swallowed,
     * sets the alpha-row toggle) instead of reaching keybuffer[]; while
     * inactive, it's a completely normal keypress. */
    reset();
    hp41_key_bridge_set_elite_mode_active(true);
    hp41_key_bridge_feed_byte(0x01);
    failures += !check("bare ALPHA while active -> nothing pushed", NULL, 0);
    failures += !check_bool("  alpha_row_toggle_requested() after bare ALPHA",
                             hp41_key_bridge_alpha_row_toggle_requested(), true);
    hp41_key_bridge_set_elite_mode_active(false);
    reset(); /* also exercises hp41_key_bridge_reset() clearing elite_mode_active_internal */
    hp41_key_bridge_feed_byte(0x01);
    failures += !check("bare ALPHA while inactive -> normal ALPHA keypress",
                        (unsigned char[]){0xc4}, 1);

    /* Regression test for a real bug found on real hardware: the
     * on-screen keyboard GUI (tools/hp41_keyboard_gui.py) sends XEQ and
     * ALPHA as "[NAME]" bracket escapes, never the raw ctrl-X/ctrl-A
     * bytes - an earlier, byte-level-only version of this trigger never
     * saw them, so it silently never fired via the GUI at all. Must
     * work identically to the raw-byte form above. */
    reset();
    feed_string("[XEQ][ALPHA]LEET[ALPHA]");
    failures += !check("[XEQ][ALPHA]LEET[ALPHA] -> keybuffer",
                        (unsigned char[]){0x32, 0xc4, 0x72, 0xc0, 0xc0, 0x84}, 6);
    failures += !check_bool("  toggle_requested() after bracket-escape form",
                             hp41_key_bridge_elite_mode_toggle_requested(), true);

    /* Same regression, for the bare-ALPHA alpha-row sub-toggle: the
     * GUI's ALPHA button always sends "[ALPHA]", so this must also be
     * intercepted while Elite Mode is active, not just a raw 0x01 byte. */
    reset();
    hp41_key_bridge_set_elite_mode_active(true);
    feed_string("[ALPHA]");
    failures += !check("[ALPHA] while active -> nothing pushed", NULL, 0);
    failures += !check_bool("  alpha_row_toggle_requested() after [ALPHA]",
                             hp41_key_bridge_alpha_row_toggle_requested(), true);

    assert(failures >= 0 && failures <= ELITE_MODE_CHECK_COUNT);
    return failures;
}

#define TOTAL_CHECK_COUNT (DIRECT_ASCII_CHECK_COUNT + NAMED_KEY_CHECK_COUNT \
                           + MALFORMED_CHECK_COUNT + MULTI_KEY_CHECK_COUNT \
                           + ELITE_MODE_CHECK_COUNT)

/**
 * @brief Run all key bridge check groups and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_direct_ascii_keys()
                        + test_named_key_protocol()
                        + test_malformed_sequences()
                        + test_multi_key_and_cap()
                        + test_elite_mode_trigger();
    assert(failures >= 0);
    assert(failures <= TOTAL_CHECK_COUNT);

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
