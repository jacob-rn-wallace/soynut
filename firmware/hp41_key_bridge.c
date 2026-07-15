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
 * buffer's 8 slots are full - a bounded FIFO, nothing exotic. Genuinely
 * unconditional, unlike push_key_tracked() below (its one caller) -
 * kept as a separate function so the Elite Mode trigger-detection logic
 * has an unconditional primitive to fall back on once it decides a
 * keycode really should reach keybuffer[].
 *
 * @param code HP-41 keycode to push.
 */
static void push_key_raw(unsigned char code)
{
    assert(lgkeybuf >= 0 && lgkeybuf <= 8);
    if (lgkeybuf < 8)
        keybuffer[lgkeybuf++] = (char)code;
    assert(lgkeybuf >= 0 && lgkeybuf <= 8);
}

/* Elite User Mode trigger state - see hp41_key_bridge.h for the full
 * protocol description. leet_progress tracks how many *keycodes* of
 * the XEQ-ALPHA-LEET-ALPHA sequence have matched so far (0..7).
 *
 * Deliberately keycode-level, not raw-byte-level: an earlier version
 * of this watched the incoming byte stream directly, which missed
 * every press that arrives as a "[NAME]" bracket escape (e.g.
 * tools/hp41_keyboard_gui.py's XEQ/ALPHA buttons send "[XEQ]"/
 * "[ALPHA]", never the raw ctrl-X/ctrl-A bytes) - confirmed as a real
 * bug on real hardware (the trigger silently never fired via the GUI,
 * every attempt just fell through to the ROM as a real XEQ ALPHA LEET
 * ALPHA attempt, showing NONEXISTENT). Tracking at push_key_tracked()
 * instead - the one choke point both the plain-byte and "[NAME]" paths
 * already funnel through - fixes both uniformly, and as a bonus makes
 * a sequence typed partly as raw bytes and partly via bracket escapes
 * (or GUI clicks) work correctly too, not just each path in isolation. */
static const unsigned char leet_keycode_seq[7] = {0x32, 0xc4, 0x72, 0xc0, 0xc0, 0x84, 0xc4};
static int leet_progress = 0;
static bool elite_mode_toggle_requested = false;
static bool alpha_row_toggle_requested = false;
static bool elite_mode_active_internal = false; /* mirrors main.c's own state, see the setter below */

/* Elite Mode found real, not-yet-diagnosed bugs on first real-hardware
 * use after the "[NAME]" bracket-escape fix above (the ALPHA
 * annunciator getting stuck lit, and the elite grid always showing all
 * zeros regardless of which stack value was actually keyed in) - see
 * CLAUDE.md's "Elite User Mode" section. Deactivated here rather than
 * ripped out, matching this project's established pattern for dormant
 * features (the Arduino display bridge, the direct serial LCD link):
 * every line of detection/rendering code stays intact and still
 * exercised by tests/key_bridge_test.c's
 * hp41_key_bridge_set_elite_mode_feature_enabled(true) call, ready to
 * debug and re-enable later - just this one flag needs to flip back to
 * true in production. Deliberately gates push_key_tracked() itself, not
 * just whether main.c acts on the resulting flags: gating only in
 * main.c would still let the closing ALPHA get silently swallowed here
 * with nothing to show for it - a real regression in its own right
 * (a keystroke vanishing with zero visible effect), worse than simply
 * not having the feature at all. */
static bool elite_mode_feature_enabled = false;

/**
 * @brief Push one real keycode, watching for the Elite Mode trigger sequence.
 *
 * The single choke point every real keypress (plain ASCII via
 * tabcode[], or a resolved "[NAME]" escape) goes through - see
 * leet_progress's comment above for why this has to live here rather
 * than at the raw-byte level. Swallows the final ALPHA of a completed
 * XEQ-ALPHA-LEET-ALPHA sequence (never reaches keybuffer[]), and - if
 * Elite Mode is currently active - a bare ALPHA that isn't part of a
 * completing sequence (toggles the alpha row instead). Every other
 * keycode reaches keybuffer[] exactly as it always did. A complete,
 * unconditional no-op (falls straight through to push_key_raw()) while
 * elite_mode_feature_enabled is false - see that flag's comment above.
 *
 * @param code HP-41 keycode to push.
 */
static void push_key_tracked(unsigned char code)
{
    if (!elite_mode_feature_enabled) {
        push_key_raw(code);
        return;
    }

    assert(leet_progress >= 0 && leet_progress < 7);
    if (code == leet_keycode_seq[leet_progress]) {
        leet_progress++;
        if (leet_progress == 7) {
            elite_mode_toggle_requested = true;
            leet_progress = 0;
            return; /* swallow the closing ALPHA - see hp41_key_bridge.h */
        }
    } else {
        leet_progress = (code == leet_keycode_seq[0]) ? 1 : 0;
        if (elite_mode_active_internal && code == 0xc4) {
            /* Bare ALPHA while Elite Mode is active, not part of a
             * completing trigger sequence - see hp41_key_bridge.h. */
            alpha_row_toggle_requested = true;
            return;
        }
    }
    push_key_raw(code);
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
            push_key_tracked(named_keys[i].code1);
            if (named_keys[i].code2)
                push_key_tracked(named_keys[i].code2);
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
 * be held too, not just named ones. ON is unconditionally rejected
 * before either lookup: real-hardware testing showed driving it through
 * the sustained hold protocol (continuously re-asserting flagKB/regK,
 * the same mechanism validated against USER-mode function-key label
 * hold/nullify - see hp41_key_hold_bridge.h) makes the ROM spin for
 * 100,000+ instructions before it finally toggles power on release -
 * ON is a power toggle, not a USER-mode-assignable label, and was never
 * exercised against this mechanism the way the function keys were.
 * "[+ON]" is now a documented no-op; callers (e.g. the keyboard GUI)
 * should send ON as a plain instant tap instead, matching how a real
 * power button reacts to the press itself, not how long it's held.
 *
 * @param name Uppercase key name, or a single character, NUL-terminated.
 * @return The resolved keycode, or 0 if unresolved (same "silently
 *         ignored" policy as elsewhere in this file).
 */
static unsigned char resolve_hold_code(const char *name)
{
    assert(name != NULL);
    assert(NUM_NAMED_KEYS > 0);
    if (strcmp(name, "ON") == 0)
        return 0; /* ON can't be meaningfully held - see header doc */
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

/* Set by "[CLRMEM]", consumed (and cleared) by
 * hp41_key_bridge_clear_memory_requested() - see hp41_key_bridge.h for
 * why this is a one-shot flag rather than an immediate action here. */
static bool clear_memory_requested = false;

/**
 * @brief Reset the "[NAME]" escape-sequence state to idle.
 *
 * Power of 10, Rule 5 note: this is a handful of unconditional
 * assignments with no precondition to check and no postcondition beyond
 * exactly what the lines above already state, so an assertion here
 * would just restate them rather than catch a real anomaly (same
 * rationale as nut_stubs.c's no-op stubs).
 */
void hp41_key_bridge_reset(void)
{
    name_len = STATE_NORMAL;
    clear_memory_requested = false;
    leet_progress = 0;
    elite_mode_toggle_requested = false;
    alpha_row_toggle_requested = false;
    elite_mode_active_internal = false;
    /* Deliberately does NOT touch elite_mode_feature_enabled - that's a
     * production/test-mode switch (see its own comment above), not
     * per-sequence protocol state a fresh "boot" should reset. Tests
     * that need it on call hp41_key_bridge_set_elite_mode_feature_enabled()
     * once, and it stays enabled across any number of reset() calls
     * within that test. */
}

/**
 * @brief Check for, and consume, a pending "[CLRMEM]" request; see the header.
 *
 * @return true if "[CLRMEM]" was received since the last call.
 */
bool hp41_key_bridge_clear_memory_requested(void)
{
    bool requested = clear_memory_requested;
    assert(requested == true || requested == false);
    clear_memory_requested = false;
    assert(clear_memory_requested == false);
    return requested;
}

/**
 * @brief Check for, and consume, a pending Elite Mode toggle request; see the header.
 *
 * @return true if the XEQ-ALPHA-LEET-ALPHA sequence (or "[LEET]") was
 *         completed since the last call.
 */
bool hp41_key_bridge_elite_mode_toggle_requested(void)
{
    bool requested = elite_mode_toggle_requested;
    elite_mode_toggle_requested = false;
    assert(elite_mode_toggle_requested == false);
    return requested;
}

/**
 * @brief Check for, and consume, a pending alpha-row toggle request; see the header.
 *
 * @return true if a bare ALPHA press was intercepted since the last call.
 */
bool hp41_key_bridge_alpha_row_toggle_requested(void)
{
    bool requested = alpha_row_toggle_requested;
    alpha_row_toggle_requested = false;
    assert(alpha_row_toggle_requested == false);
    return requested;
}

/**
 * @brief Tell the bridge whether Elite Mode is currently active; see the header.
 *
 * @param active Whether Elite Mode is currently on.
 */
void hp41_key_bridge_set_elite_mode_active(bool active)
{
    elite_mode_active_internal = active;
}

/**
 * @brief Enable or disable the Elite Mode feature entirely; see the header.
 *
 * @param enabled Whether the trigger sequence/interception should do
 *                anything at all.
 */
void hp41_key_bridge_set_elite_mode_feature_enabled(bool enabled)
{
    elite_mode_feature_enabled = enabled;
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

        /* Elite Mode trigger detection happens inside push_key_tracked()
         * itself, not here - see its header doc for why (the "[NAME]"
         * path below needs the exact same detection, so it lives at the
         * one choke point both paths share, not duplicated per-path). */
        unsigned char code = tabcode[(unsigned char)c];
        if (code)
            push_key_tracked(code);
        return;
    }

    /* Inside a "[...]" sequence (collecting or overflowed). A fresh '['
     * always restarts it - simplest recovery for a mistyped sequence.
     * Deliberately does NOT touch leet_progress: detection lives at
     * push_key_tracked() now (keycode-level), so entering/abandoning a
     * bracket sequence has no effect on it either way - only an
     * actually-pushed, mismatching keycode resets progress. */
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
            } else if (strcmp(name_buf, "CLRMEM") == 0) {
                /* Bridge-level command, not a real key - see
                 * hp41_key_bridge_clear_memory_requested()'s header doc
                 * for why this only sets a flag rather than acting here. */
                clear_memory_requested = true;
            } else if (strcmp(name_buf, "LEET") == 0) {
                /* Convenience alias for the real XEQ-ALPHA-LEET-ALPHA
                 * key sequence - see hp41_key_bridge.h. */
                elite_mode_toggle_requested = true;
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
