/**
 * @file hp41_key_hold_bridge.c
 * @brief See hp41_key_hold_bridge.h for the design (and why the earlier
 *        dokey()-override approach was wrong and replaced).
 */

#include "hp41_key_hold_bridge.h"

#include <assert.h>

#define GLOBAL extern
#include "nutcpu.h"

static int held_keycode = 0;
static bool hold_active = false;

/**
 * @brief Push one HP-41 keycode onto keybuffer[], dropping it if full.
 *
 * Mirrors hp41_key_bridge.c's own push_key() - kept local rather than
 * shared, since exposing it would mean a header change to that file
 * for a single three-line helper.
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
 * @brief Begin tracking a real press-and-hold; see the header for the contract.
 *
 * @param keycode HP-41 keycode being held (0-255).
 */
void hp41_key_hold_press(int keycode)
{
    assert(keycode >= 0 && keycode <= 255); /* stored/pushed as unsigned char below */
    held_keycode = keycode;
    hold_active = true;
    push_key((unsigned char)keycode);
    assert(hold_active);
}

/**
 * @brief End the currently-tracked hold, if any.
 *
 * Power of 10, Rule 5 note: a single unconditional assignment - see
 * hp41_key_bridge.c's hp41_key_bridge_reset() for the same rationale.
 */
void hp41_key_hold_release(void)
{
    hold_active = false;
}

/**
 * @brief Whether a real hold is currently in progress.
 *
 * @return true if a hold is active, false otherwise.
 */
bool hp41_key_hold_active(void)
{
    return hold_active;
}

/**
 * @brief Re-assert flagKB=1/regK=<held keycode> if a hold is in progress.
 */
void hp41_key_hold_sustain(void)
{
    assert(held_keycode >= 0 && held_keycode <= 255);
    if (hold_active) {
        flagKB = 1;
        regK = (unsigned char)held_keycode;
        assert(flagKB == 1 && regK == (unsigned char)held_keycode);
    }
}
