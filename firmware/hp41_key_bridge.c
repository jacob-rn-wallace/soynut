/**
 * @file hp41_key_bridge.c
 * @brief Bridges USB serial keypress bytes to emu41gcc's
 *        keybuffer[]/lgkeybuf (see nutcpu.h - dokey(), in nutcpu.c,
 *        drains this exactly like the real HP-41's keyboard scan state
 *        machine).
 */

#include "hp41_key_bridge.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_key_hold_bridge.h"

/**
 * @brief ASCII (0-127) -> HP-41 keycode lookup table.
 *
 * Sourced unchanged from emu41gcc's emu41.c, traite_touche()'s
 * `tabcode[128]` - the same table the reference DOS emulator uses to
 * translate PC keyboard input, not re-derived here. 0 means "no HP-41
 * key for this character".
 */
static const unsigned char tabcode[128] = {
/*  0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15 */
    0,0xc4,   0,   0,   0,   0,   0,   0,0xc3,   0,0x13,   0,   0,0x13,   0,   0,
/* 16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31 */
    0,   0,0x87,   0,   0,   0,   0,   0,0x32,   0,   0,   0,   0,   0,   0,   0,
/*       !    "    #    $    %    &    '    (    )    *    +    ,    -    .    / */
 0x37,   0,   0,0x71,0x83,0x31,   0,   0,   0,   0,0x16,0x15,0x77,0x14,0x77,0x17,
/*  0    1    2    3    4    5    6    7    8    9    :    ;    <    =    >    ? */
 0x37,0x36,0x76,0x86,0x35,0x75,0x85,0x34,0x74,0x84,0x17,   0,0x81,0x76,0xc1,0x86,
/*  @    A    B    C    D    E    F    G    H    I    J    K    L    M    N    O */
    0,0x10,0x30,0x70,0x80,0xc0,0x11,0x31,0x71,0x81,0xc1,0x32,0x72,0x82,0x13,0x73,
/*  P    Q    R    S    T    U    V    W    X    Y    Z    [    \    ]    ^    _ */
 0x83,0x14,0x34,0x74,0x84,0x15,0x35,0x75,0x85,0x16,0x36,   0,   0,   0,0x13,   0,
/*  `    a    b    c    d    e    f    g    h    i    j    k    l    m    n    o */
    0,0x10,0x30,0x70,0x80,0xc0,0x11,0x31,0x71,0x81,0xc1,0x32,0x72,0x82,0x13,0x73,
/*  p    q    r    s    t    u    v    w    x    y    z    {    |    }    ~     */
 0x83,0x14,0x34,0x74,0x84,0x15,0x35,0x75,0x85,0x16,0x36,   0,   0,   0,   0,   0
};

/**
 * @brief One entry in the named_keys[] table below: a "[NAME]" string
 *        mapped to one or two HP-41 keycodes.
 */
typedef struct {
    const char *name;
    unsigned char code1;
    unsigned char code2; /* 0 if this key is a single press */
} named_key_t;

/**
 * @brief Physical keys with no ASCII equivalent, sent as "[NAME]" over serial.
 *
 * Codes sourced from the same emu41.c, traite_touche()'s handling of
 * raw PC function/arrow-key scancodes (the c==0 branch), which a plain
 * ASCII byte stream has no equivalent of. Names are compared uppercase
 * (see hp41_key_bridge_feed_byte()).
 *
 * BST has no dedicated HP-41 keycode - on the real keyboard it's
 * SHIFT+SST, so it's sent as that same two-key sequence here (matches
 * emu41.c's own `if (!fshift) push_key(0x12);` before SST's code for
 * its BST-producing scancodes).
 */

static const named_key_t named_keys[] = {
    {"ON",    0x18, 0},
    {"USER",  0xc6, 0},
    {"PRGM",  0xc5, 0},
    {"ALPHA", 0xc4, 0},
    {"SHIFT", 0x12, 0},
    {"SST",   0xc2, 0},
    {"BST",   0x12, 0xc2},
    {"RS",    0x87, 0},
    {"XEQ",   0x32, 0},
    {"CLX",   0xc3, 0},
    {"XY",    0x11, 0}, /* X<>Y */
    {"RDN",   0x31, 0},
};
#define NUM_NAMED_KEYS (sizeof(named_keys) / sizeof(named_keys[0]))

/**
 * @brief Push one HP-41 keycode onto keybuffer[], dropping it if full.
 *
 * Mirrors emu41gcc's own push_key() (emu41.c): silently drop once the
 * buffer's 8 slots are full - a bounded FIFO, nothing exotic.
 *
 * @param code HP-41 keycode to push.
 */
static void push_key(unsigned char code)
{
    assert(lgkeybuf >= 0 && lgkeybuf <= 8);
    if (lgkeybuf < 8)
        keybuffer[lgkeybuf++] = (char)code;
    assert(lgkeybuf >= 0 && lgkeybuf <= 8);
}

/**
 * @brief Look up a "[NAME]" string and push its keycode(s), if recognized.
 *
 * @param name Uppercase key name (no brackets), NUL-terminated.
 */
static void handle_named_key(const char *name)
{
    assert(name != NULL);
    assert(NUM_NAMED_KEYS > 0);
    for (size_t i = 0; i < NUM_NAMED_KEYS; i++) {
        if (strcmp(name, named_keys[i].name) == 0) {
            push_key(named_keys[i].code1);
            if (named_keys[i].code2)
                push_key(named_keys[i].code2);
            return;
        }
    }
    /* unrecognized name - silently ignored, same policy as an unmapped
     * ASCII byte falling through tabcode[] to 0. */
}

/**
 * @brief Resolve a name to a single HP-41 keycode for "[+X]" hold-press purposes.
 *
 * Either a named_keys[] entry with no second code (two-code combos
 * like BST don't have a meaningful single "held" state, so they're
 * treated as unresolved here), or - if exactly one character - a plain
 * tabcode[] lookup, so any regular key (letters, digits, operators) can
 * be held too, not just named ones.
 *
 * @param name Uppercase key name, or a single character, NUL-terminated.
 * @return The resolved keycode, or 0 if unresolved (same "silently
 *         ignored" policy as elsewhere in this file).
 */
static unsigned char resolve_hold_code(const char *name)
{
    assert(name != NULL);
    assert(NUM_NAMED_KEYS > 0);
    for (size_t i = 0; i < NUM_NAMED_KEYS; i++) {
        if (strcmp(name, named_keys[i].name) == 0)
            return named_keys[i].code2 ? 0 : named_keys[i].code1;
    }
    if (name[0] != '\0' && name[1] == '\0') {
        unsigned char c = (unsigned char)name[0];
        if (c < 128)
            return tabcode[c];
    }
    return 0;
}

/* Escape-sequence state:
 *   STATE_NORMAL   - not inside a "[...]" sequence, bytes are direct keys
 *   STATE_OVERFLOW - inside one, but it's already too long to fit
 *                    name_buf; skipping bytes until ']' or a fresh '['
 *   0..NAME_BUF_SIZE-2 - inside one, this many chars buffered so far
 */
#define NAME_BUF_SIZE 8
#define STATE_NORMAL   (-1)
#define STATE_OVERFLOW (-2)
static char name_buf[NAME_BUF_SIZE];
static int name_len = STATE_NORMAL;

/**
 * @brief Reset the "[NAME]" escape-sequence state to idle.
 *
 * Power of 10, Rule 5 note: this is a single, unconditional assignment -
 * there's no precondition to check and the postcondition is exactly
 * the line above it, so an assertion here would just restate it rather
 * than catch a real anomaly (same rationale as nut_stubs.c's no-op
 * stubs).
 */
void hp41_key_bridge_reset(void)
{
    name_len = STATE_NORMAL;
}

/**
 * @brief Feed one incoming protocol byte; see hp41_key_bridge.h for the
 *        full protocol description.
 *
 * @param c The incoming byte, as an int.
 */
void hp41_key_bridge_feed_byte(int c)
{
    assert(name_len >= STATE_OVERFLOW && name_len < NAME_BUF_SIZE);

    if (name_len == STATE_NORMAL) {
        if (c == '[') {
            name_len = 0;
            return;
        }
        if (c < 0 || c > 127)
            return;
        unsigned char code = tabcode[c];
        if (code)
            push_key(code);
        return;
    }

    /* Inside a "[...]" sequence (collecting or overflowed). A fresh '['
     * always restarts it - simplest recovery for a mistyped sequence. */
    if (c == '[') {
        name_len = 0;
        return;
    }
    if (c == ']') {
        if (name_len >= 0) {
            name_buf[name_len] = '\0';
            if (name_buf[0] == '-') {
                /* "[-...]" - release whatever's currently held. Content
                 * after the '-' (if any) is ignored - only one key is
                 * ever held at a time, so no name is needed. */
                hp41_key_hold_release();
            } else if (name_buf[0] == '+') {
                /* "[+X]" - begin a real press-and-hold of X. */
                unsigned char code = resolve_hold_code(name_buf + 1);
                if (code)
                    hp41_key_hold_press(code);
            } else {
                handle_named_key(name_buf);
            }
        }
        /* else STATE_OVERFLOW: too long, already known unrecognizable -
         * abandoned silently, same policy as an unmapped ASCII byte. */
        name_len = STATE_NORMAL;
        return;
    }
    if (name_len == STATE_OVERFLOW)
        return; /* keep skipping until ']' or a fresh '[' */
    if (name_len >= NAME_BUF_SIZE - 1) {
        name_len = STATE_OVERFLOW;
        return;
    }
    name_buf[name_len++] = (char)toupper(c);
    assert(name_len <= NAME_BUF_SIZE - 1); /* never overruns name_buf[] */
}
