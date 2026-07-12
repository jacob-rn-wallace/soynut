/* See hp41_key_hold_bridge.h for the design (and why the earlier
 * dokey()-override approach was wrong and replaced). */

#include "hp41_key_hold_bridge.h"

#define GLOBAL extern
#include "nutcpu.h"

static int held_keycode = 0;
static bool hold_active = false;

/* Mirrors hp41_key_bridge.c's own push_key() - kept local rather than
 * shared, since exposing it would mean a header change to that file
 * for a single three-line helper. */
static void push_key(unsigned char code)
{
    if (lgkeybuf < 8)
        keybuffer[lgkeybuf++] = (char)code;
}

void hp41_key_hold_press(int keycode)
{
    held_keycode = keycode;
    hold_active = true;
    push_key((unsigned char)keycode);
}

void hp41_key_hold_release(void)
{
    hold_active = false;
}

bool hp41_key_hold_active(void)
{
    return hold_active;
}

void hp41_key_hold_sustain(void)
{
    if (hold_active) {
        flagKB = 1;
        regK = (unsigned char)held_keycode;
    }
}
