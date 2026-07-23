/**
 * @file sim_keyboard.c
 * @brief SDL key events -> HP-41 wire-protocol bytes -> hp41_key_bridge_feed_byte().
 *        See sim_keyboard.h for the contract.
 */

#include "sim_keyboard.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "hp41_key_bridge.h"

/** Matches tools/hp41_keyboard_gui.py's HOLD_ENGAGE_MS exactly, for
 *  behavioral parity between the two tools - see that script's
 *  PressMode.THRESHOLD handling. */
#define HOLD_ENGAGE_MS 150u

/** Defensive upper bound on SDL_PollEvent() calls per sim_keyboard_poll()
 *  call (Power of 10, Rule 2), mirroring firmware/main.c's
 *  MAX_BYTES_PER_DRAIN pattern - well above any realistic single burst
 *  of window/keyboard events between two loop iterations. */
#define MAX_SDL_EVENTS_PER_POLL 64

/** Longest wire-protocol byte sequence this module ever builds
 *  ("[+ALPHA]" = 8 chars + NUL), rounded up with headroom. */
#define MAX_WIRE_BYTES 16

/**
 * @brief One mapped key: which physical (scancode) key, what a tap sends,
 *        and what a hold-engage sends ("[+<hold_name>]") - or NULL if
 *        this key can never be held (sent as an instant tap regardless
 *        of how long it's physically held).
 */
typedef struct {
    SDL_Scancode scancode;
    const char *tap_bytes;
    const char *hold_name;
} sim_key_map_entry_t;

/**
 * @brief PC-key -> HP-41 wire-protocol table.
 *
 * tap_bytes/hold_name values are transcribed directly from
 * firmware/hp41_key_bridge.c's tabcode[]/named_keys[] tables (the same
 * source tools/hp41_keyboard_gui.py's KEY_MAP draws from) - every byte
 * sequence here is byte-for-byte what a real terminal or that GUI would
 * send for the same logical key. ON and BST have a NULL hold_name: ON
 * because hp41_key_bridge.c's resolve_hold_code() rejects it outright
 * (confirmed on real hardware to spin the ROM for 100,000+ instructions
 * if held), BST because it's a two-code combo with no meaningful single
 * "held" state (also rejected by resolve_hold_code()) - both must be
 * sent as plain taps only.
 */
static const sim_key_map_entry_t key_map[] = {
    /* Digits - top row and numpad both map to the same HP-41 digit. */
    {SDL_SCANCODE_0, "0", "0"}, {SDL_SCANCODE_KP_0, "0", "0"},
    {SDL_SCANCODE_1, "1", "1"}, {SDL_SCANCODE_KP_1, "1", "1"},
    {SDL_SCANCODE_2, "2", "2"}, {SDL_SCANCODE_KP_2, "2", "2"},
    {SDL_SCANCODE_3, "3", "3"}, {SDL_SCANCODE_KP_3, "3", "3"},
    {SDL_SCANCODE_4, "4", "4"}, {SDL_SCANCODE_KP_4, "4", "4"},
    {SDL_SCANCODE_5, "5", "5"}, {SDL_SCANCODE_KP_5, "5", "5"},
    {SDL_SCANCODE_6, "6", "6"}, {SDL_SCANCODE_KP_6, "6", "6"},
    {SDL_SCANCODE_7, "7", "7"}, {SDL_SCANCODE_KP_7, "7", "7"},
    {SDL_SCANCODE_8, "8", "8"}, {SDL_SCANCODE_KP_8, "8", "8"},
    {SDL_SCANCODE_9, "9", "9"}, {SDL_SCANCODE_KP_9, "9", "9"},

    /* Operators. */
    {SDL_SCANCODE_MINUS, "-", "-"}, {SDL_SCANCODE_KP_MINUS, "-", "-"},
    {SDL_SCANCODE_EQUALS, "+", "+"}, {SDL_SCANCODE_KP_PLUS, "+", "+"},
    {SDL_SCANCODE_KP_MULTIPLY, "*", "*"},
    {SDL_SCANCODE_SLASH, "/", "/"}, {SDL_SCANCODE_KP_DIVIDE, "/", "/"},
    {SDL_SCANCODE_PERIOD, ".", "."}, {SDL_SCANCODE_KP_PERIOD, ".", "."},

    /* ENTER and CLX (backspace). */
    {SDL_SCANCODE_RETURN, "\r", "\r"}, {SDL_SCANCODE_KP_ENTER, "\r", "\r"},
    {SDL_SCANCODE_BACKSPACE, "[CLX]", "CLX"},

    /* ALPHA-mode letters - tabcode[] gives the same keycode for upper
     * and lower case, so a plain uppercase tap byte covers both. */
    {SDL_SCANCODE_A, "A", "A"}, {SDL_SCANCODE_B, "B", "B"},
    {SDL_SCANCODE_C, "C", "C"}, {SDL_SCANCODE_D, "D", "D"},
    {SDL_SCANCODE_E, "E", "E"}, {SDL_SCANCODE_F, "F", "F"},
    {SDL_SCANCODE_G, "G", "G"}, {SDL_SCANCODE_H, "H", "H"},
    {SDL_SCANCODE_I, "I", "I"}, {SDL_SCANCODE_J, "J", "J"},
    {SDL_SCANCODE_K, "K", "K"}, {SDL_SCANCODE_L, "L", "L"},
    {SDL_SCANCODE_M, "M", "M"}, {SDL_SCANCODE_N, "N", "N"},
    {SDL_SCANCODE_O, "O", "O"}, {SDL_SCANCODE_P, "P", "P"},
    {SDL_SCANCODE_Q, "Q", "Q"}, {SDL_SCANCODE_R, "R", "R"},
    {SDL_SCANCODE_S, "S", "S"}, {SDL_SCANCODE_T, "T", "T"},
    {SDL_SCANCODE_U, "U", "U"}, {SDL_SCANCODE_V, "V", "V"},
    {SDL_SCANCODE_W, "W", "W"}, {SDL_SCANCODE_X, "X", "X"},
    {SDL_SCANCODE_Y, "Y", "Y"}, {SDL_SCANCODE_Z, "Z", "Z"},

    /* Named keys with no ASCII equivalent - dedicated function keys, to
     * avoid clashing with the ALPHA-mode letters above. */
    {SDL_SCANCODE_F1, "[ON]", NULL},      /* ON - always a plain tap, see header doc */
    {SDL_SCANCODE_TAB, "[USER]", "USER"},
    {SDL_SCANCODE_F2, "[PRGM]", "PRGM"},
    {SDL_SCANCODE_GRAVE, "[ALPHA]", "ALPHA"},
    {SDL_SCANCODE_F3, "[SHIFT]", "SHIFT"},
    {SDL_SCANCODE_F4, "[SST]", "SST"},
    {SDL_SCANCODE_F5, "[BST]", NULL},     /* BST - two-code combo, no single held state */
    {SDL_SCANCODE_F6, "[XY]", "XY"},
    {SDL_SCANCODE_F7, "[RDN]", "RDN"},
    {SDL_SCANCODE_F8, "[RS]", "RS"}, {SDL_SCANCODE_SPACE, "[RS]", "RS"},
    {SDL_SCANCODE_F9, "[XEQ]", "XEQ"},
};
#define NUM_KEY_MAP_ENTRIES (sizeof(key_map) / sizeof(key_map[0]))

/** Currently-tracked key (at most one at a time - matches the wire
 *  protocol's own single-hold-slot design and
 *  tools/hp41_keyboard_gui.py's single-mouse-button assumption). A
 *  second key going down while one is already tracked is sent as an
 *  instant tap instead of starting a second, unsupported concurrent
 *  hold. */
static SDL_Scancode tracked_scancode = SDL_SCANCODE_UNKNOWN;
static uint32_t tracked_press_started_ms = 0;
static bool tracked_engaged = false;
static const sim_key_map_entry_t *tracked_entry = NULL;

/**
 * @brief Feed one wire-protocol byte string through the key bridge.
 *
 * @param bytes NUL-terminated ASCII byte sequence, e.g. "5" or "[XEQ]".
 */
static void send_bytes(const char *bytes)
{
    assert(bytes != NULL);
    size_t len = strnlen(bytes, MAX_WIRE_BYTES);
    assert(len < MAX_WIRE_BYTES);
    for (size_t i = 0; i < len; i++) {
        hp41_key_bridge_feed_byte((unsigned char)bytes[i]);
    }
}

/**
 * @brief Send a press-and-hold-begin ("[+<name>]") for a mapped key.
 *
 * @param hold_name The key's bare hold name/character (never NULL - the
 *                   always-tap keys are filtered out before this is
 *                   called; see sim_keyboard_poll()).
 */
static void send_hold_begin(const char *hold_name)
{
    assert(hold_name != NULL);
    char buf[MAX_WIRE_BYTES];
    int n = snprintf(buf, sizeof(buf), "[+%s]", hold_name);
    assert(n > 0 && (size_t)n < sizeof(buf));
    send_bytes(buf);
}

/**
 * @brief Find a mapped key's table entry by scancode.
 *
 * @param scancode SDL physical-key scancode.
 * @return Pointer into key_map[], or NULL if unmapped.
 */
static const sim_key_map_entry_t *find_entry(SDL_Scancode scancode)
{
    assert(NUM_KEY_MAP_ENTRIES > 0);
    assert(scancode >= 0); /* SDL_Scancode is a plain non-negative enum */
    for (size_t i = 0; i < NUM_KEY_MAP_ENTRIES; i++) {
        if (key_map[i].scancode == scancode)
            return &key_map[i];
    }
    return NULL;
}

/**
 * @brief Reset the tap/hold tracking state to idle; see the header.
 *
 * Power of 10, Rule 5 note: a handful of unconditional assignments with
 * no precondition to check and no postcondition beyond exactly what the
 * lines already state - same accepted exception as
 * hp41_key_bridge_reset()'s identical shape (see CLAUDE.md).
 */
void sim_keyboard_init(void)
{
    tracked_scancode = SDL_SCANCODE_UNKNOWN;
    tracked_engaged = false;
    tracked_entry = NULL;
}

/**
 * @brief Handle one SDL_KEYDOWN event.
 *
 * @param scancode Physical key that went down.
 * @param repeat Whether this is an OS key-repeat, not a fresh press.
 * @param now_ms Current sim time.
 */
static void handle_key_down(SDL_Scancode scancode, bool repeat, uint32_t now_ms)
{
    /* Invariant: whenever something is tracked, its entry pointer is
     * valid - checked here so a corrupted state is caught immediately
     * rather than dereferenced later in check_hold_deadline()/
     * handle_key_up(). */
    assert(tracked_scancode == SDL_SCANCODE_UNKNOWN || tracked_entry != NULL);

    if (repeat)
        return; /* OS key-repeat is not a fresh press - see header doc */

    const sim_key_map_entry_t *entry = find_entry(scancode);
    if (entry == NULL)
        return;

    if (tracked_scancode == SDL_SCANCODE_UNKNOWN) {
        /* No key currently tracked - start tracking this one's
         * tap-vs-hold threshold. */
        tracked_scancode = scancode;
        tracked_press_started_ms = now_ms;
        tracked_engaged = false;
        tracked_entry = entry;
        assert(tracked_entry != NULL);
    } else if (scancode != tracked_scancode) {
        /* A second, simultaneous key - only one hold slot exists (see
         * tracked_scancode's comment), so send this one as an instant
         * tap rather than starting an unsupported second hold. */
        send_bytes(entry->tap_bytes);
    }
}

/**
 * @brief Handle one SDL_KEYUP event.
 *
 * @param scancode Physical key that went up.
 */
static void handle_key_up(SDL_Scancode scancode)
{
    if (scancode != tracked_scancode)
        return; /* not the tracked key (e.g. already tap-resolved on keydown) */

    assert(tracked_scancode != SDL_SCANCODE_UNKNOWN);
    assert(tracked_entry != NULL);
    if (tracked_engaged) {
        send_bytes("[-]");
    } else {
        send_bytes(tracked_entry->tap_bytes);
    }
    tracked_scancode = SDL_SCANCODE_UNKNOWN;
    tracked_engaged = false;
    tracked_entry = NULL;
}

/**
 * @brief Check whether the tracked key has crossed the hold-engage threshold.
 *
 * @param now_ms Current sim time.
 */
static void check_hold_deadline(uint32_t now_ms)
{
    if (tracked_scancode == SDL_SCANCODE_UNKNOWN || tracked_engaged)
        return;
    assert(tracked_engaged == false); /* documents the guard just above */
    assert(tracked_entry != NULL);
    if (tracked_entry->hold_name == NULL)
        return; /* always-tap key (ON, BST) - never engages a hold */
    if (now_ms - tracked_press_started_ms >= HOLD_ENGAGE_MS) {
        send_hold_begin(tracked_entry->hold_name);
        tracked_engaged = true;
    }
}

/**
 * @brief Drain pending SDL events and advance the tap/hold state machine; see the header.
 *
 * @param now_ms Current sim time in milliseconds.
 * @return true if the window was asked to close.
 */
bool sim_keyboard_poll(uint32_t now_ms)
{
    bool quit_requested = false;
    SDL_Event event;
    int drained = 0;

    while (drained < MAX_SDL_EVENTS_PER_POLL && SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            quit_requested = true;
            break;
        case SDL_KEYDOWN:
            handle_key_down(event.key.keysym.scancode, event.key.repeat != 0, now_ms);
            break;
        case SDL_KEYUP:
            handle_key_up(event.key.keysym.scancode);
            break;
        default:
            break;
        }
        drained++;
    }
    assert(drained >= 0 && drained <= MAX_SDL_EVENTS_PER_POLL);

    check_hold_deadline(now_ms);

    assert(quit_requested == true || quit_requested == false);
    return quit_requested;
}
