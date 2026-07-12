/* Native (host) test for firmware/hp41_key_bridge.c.
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
 * Build (from repo root):
 *   cc -std=gnu11 -fcommon -Iemu41gcc -Ifirmware/emu41gcc_compat -Ifirmware \
 *      -o tests/build/key_bridge_test tests/key_bridge_test.c \
 *      firmware/emu41gcc_compat/nut_globals.c \
 *      firmware/hp41_key_bridge.c \
 *      firmware/hp41_key_hold_bridge.c
 *   ./tests/build/key_bridge_test
 */

#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_key_bridge.h"

static int failures = 0;

static void reset(void)
{
    lgkeybuf = 0;
    memset(keybuffer, 0, sizeof(keybuffer));
    hp41_key_bridge_reset();
}

static void feed_string(const char *s)
{
    for (; *s; s++)
        hp41_key_bridge_feed_byte((unsigned char)*s);
}

static void check(const char *label, const unsigned char *want, int want_len)
{
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
        failures++;
    }
}

int main(void)
{
    /* Direct ASCII keys - digits, operators, Enter, ctrl-chars. */
    reset();
    feed_string("5");
    check("digit '5'", (unsigned char[]){0x75}, 1);

    reset();
    feed_string("+");
    check("operator '+'", (unsigned char[]){0x15}, 1);

    reset();
    feed_string("\r"); /* Enter/CR */
    check("Enter (CR)", (unsigned char[]){0x13}, 1);

    reset();
    feed_string("\b"); /* Backspace -> CLX */
    check("Backspace (CLX)", (unsigned char[]){0xc3}, 1);

    reset();
    hp41_key_bridge_feed_byte(1); /* ctrl-A -> ALPHA */
    check("ctrl-A (ALPHA)", (unsigned char[]){0xc4}, 1);

    reset();
    hp41_key_bridge_feed_byte(18); /* ctrl-R -> R/S */
    check("ctrl-R (R/S)", (unsigned char[]){0x87}, 1);

    reset();
    hp41_key_bridge_feed_byte(24); /* ctrl-X -> XEQ */
    check("ctrl-X (XEQ)", (unsigned char[]){0x32}, 1);

    /* Unmapped ASCII (e.g. '@') should push nothing. */
    reset();
    feed_string("@");
    check("unmapped '@'", (unsigned char[]){}, 0);

    /* Named-key protocol. */
    reset();
    feed_string("[ON]");
    check("[ON]", (unsigned char[]){0x18}, 1);

    reset();
    feed_string("[shift]"); /* lowercase - must be case-insensitive */
    check("[shift] (lowercase)", (unsigned char[]){0x12}, 1);

    reset();
    feed_string("[BST]");
    check("[BST] (SHIFT+SST chord)", (unsigned char[]){0x12, 0xc2}, 2);

    reset();
    feed_string("[XY]");
    check("[XY] (X<>Y)", (unsigned char[]){0x11}, 1);

    /* Malformed sequences should drop cleanly, not corrupt subsequent input. */
    reset();
    feed_string("[NOTAREALKEY]5");
    check("unrecognized name then '5'", (unsigned char[]){0x75}, 1);

    reset();
    feed_string("[ON[USER]");  /* nested '[' abandons the first, starts fresh */
    check("nested '[' recovery", (unsigned char[]){0xc6}, 1);

    reset();
    feed_string("[ON");  /* never closed */
    check("unterminated '['", (unsigned char[]){}, 0);

    /* Multi-key sequence, as if typing "123" then ON then Enter. */
    reset();
    feed_string("123[ON]\r");
    check("\"123[ON]\\r\"", (unsigned char[]){0x36, 0x76, 0x86, 0x18, 0x13}, 5);

    /* Buffer cap: keybuffer[] holds 8, extra presses should be dropped
     * (matches emu41gcc's own push_key()). */
    reset();
    feed_string("123456789");
    check("9 digits -> capped at 8", (unsigned char[]){
        0x36, 0x76, 0x86, 0x35, 0x75, 0x85, 0x34, 0x74 /* '9' dropped */
    }, 8);

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
