/**
 * @file hp41_key_hold_bridge.h
 * @brief Real key press/hold/release tracking, sustained from outside
 *        emu41gcc's dokey() rather than by modifying it.
 */
#ifndef SOYNUT_HP41_KEY_HOLD_BRIDGE_H
#define SOYNUT_HP41_KEY_HOLD_BRIDGE_H

#include <stdbool.h>

/* Real key press/hold/release tracking. This is what lets the ROM's
 * own hold-duration logic (the USER-mode "hold briefly to see the
 * assigned label, hold longer than ~0.5s to nullify the keypress"
 * feature, HP-41C Owner's Handbook p.11) actually work, instead of
 * every keypress looking like an instantaneous tap to the ROM - see
 * CLAUDE.md's "Known limitation: real key hold-duration is not
 * modeled at all" section for the full investigation, including the
 * ROM disassembly (0x0E97-0x0ED7, labels NLT010/NULT10 from
 * SYSTEMLABELS.TXT) that confirmed the ROM measures "how long has this
 * been held" by polling CHKKB in a decrementing-counter loop.
 *
 * Design note: an earlier version of this file tried to intercept
 * emu41gcc's dokey() itself via a compile-time #define rename (force-
 * included for nutcpu.c only). That doesn't work: dokey() has exactly
 * 3 call sites (the RSTKB, CHKKB, and C=KEYS opcode handlers), all
 * inside nutcpu.c itself, so a single textual rename affects the
 * function's own definition AND all 3 of its own internal callers
 * identically - there's no way to make nutcpu.c's own opcode handlers
 * keep calling an externally-overridden "dokey" while only the
 * definition gets renamed, using plain token substitution. The result
 * was dead code: nutcpu.c ended up calling the renamed original
 * directly, never reaching the override, caught by this project's own
 * "verify empirically, don't just assume" pattern (a unit test on the
 * override itself, not just an ARM build succeeding).
 *
 * Actual design (this version): flagKB/regK are plain external globals
 * (nutcpu.h's GLOBAL macro), exactly like keybuffer[]/lgkeybuf that
 * hp41_key_bridge.c already pokes directly - no interception needed.
 * hp41_key_hold_sustain() just re-asserts them from OUTSIDE, called by
 * main.c between single-stepped executeNUT(1) calls while a hold is in
 * progress. Single-stepping matters: the ROM's real hold-check loop
 * clears flagKB (via the RSTKB opcode) and re-reads it (via CHKKB)
 * within the same handful of instructions, so main.c must re-assert at
 * least that often - a coarser batch (e.g. executeNUT(1000)) would let
 * the ROM observe several spurious "released" reads before the next
 * reassertion, exactly reproducing the original bug.
 */

/**
 * @brief Begin tracking a real press-and-hold of the given HP-41 keycode.
 *
 * Pushes @p keycode onto keybuffer[] once (same as a normal tap) and
 * marks a hold as active so hp41_key_hold_sustain() starts re-asserting
 * it.
 *
 * @param keycode HP-41 keycode being held (0-255).
 */
void hp41_key_hold_press(int keycode);

/**
 * @brief End the currently-tracked hold, if any.
 */
void hp41_key_hold_release(void);

/**
 * @brief Whether a real hold is currently in progress.
 *
 * main.c uses this to decide whether to single-step (executeNUT(1),
 * reasserting between every instruction) or run its normal larger
 * batches.
 *
 * @return true if a hold is active, false otherwise.
 */
bool hp41_key_hold_active(void);

/**
 * @brief Re-assert flagKB=1/regK=<held keycode> if a hold is in progress.
 *
 * A no-op if no hold is active. Call this before every single
 * executeNUT(1) step while hp41_key_hold_active() is true - see the
 * file header's single-stepping rationale.
 */
void hp41_key_hold_sustain(void);

#endif /* SOYNUT_HP41_KEY_HOLD_BRIDGE_H */
