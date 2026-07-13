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

#define TOTAL_CHECK_COUNT (DIRECT_ASCII_CHECK_COUNT + NAMED_KEY_CHECK_COUNT \
                           + MALFORMED_CHECK_COUNT + MULTI_KEY_CHECK_COUNT)

/**
 * @brief Run all key bridge check groups and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_direct_ascii_keys()
                        + test_named_key_protocol()
                        + test_malformed_sequences()
                        + test_multi_key_and_cap();
    assert(failures >= 0);
    assert(failures <= TOTAL_CHECK_COUNT);

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
