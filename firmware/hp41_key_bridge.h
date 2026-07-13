/**
 * @file hp41_key_bridge.h
 * @brief Translates incoming USB-serial protocol bytes into presses on
 *        the Nut CPU's keybuffer[].
 */
#ifndef SOYNUT_HP41_KEY_BRIDGE_H
#define SOYNUT_HP41_KEY_BRIDGE_H

#include <stdbool.h>

/**
 * @brief Feed one incoming protocol byte from the USB serial keypress stream.
 *
 * Pure logic (no hardware/USB access) - safe to call/test on a host
 * build.
 *
 * Protocol: most bytes are looked up directly against the HP-41 keycode
 * table (tabcode[] below, sourced unchanged from emu41gcc/emu41.c's
 * traite_touche()) and pushed to keybuffer[] immediately - digits,
 * operators, letters (for ALPHA mode), Enter, Backspace (CLX), and
 * ctrl-A/ctrl-R/ctrl-X (ALPHA/R-S/XEQ) all work this way, exactly as
 * they did in the reference emu41.c. A handful of physical keys have no
 * ASCII equivalent at all (ON, SHIFT, USER, PRGM, SST, BST, X<>Y, RDN) -
 * '[' is unused in tabcode[] (no real HP-41 key maps to it), so it's
 * repurposed as an escape character: send "[NAME]" (case-insensitive)
 * to press one of those. Malformed or unrecognized bracket sequences
 * are silently dropped, same as an unmapped ASCII byte.
 *
 * Real key hold/release (see hp41_key_hold_bridge.h): "[+X]" begins a
 * real press-and-hold of X - either a named key (as above) or a single
 * character resolved via tabcode[], so any regular key can be held too,
 * not just named ones. "[-]" releases whatever is currently held. This
 * is what lets the ROM's own hold-duration logic (USER-mode "hold to
 * see label, hold too long nullifies", HP-41C Owner's Handbook p.11)
 * work correctly - see CLAUDE.md's "Known limitation: real key
 * hold-duration is not modeled at all" section. Two-code named keys
 * (currently just BST, a SHIFT+SST chord) have no meaningful single
 * "held" state and are ignored for "[+X]" (silently, same policy as an
 * unrecognized name). ON is excluded too, for a different reason:
 * confirmed on real hardware that holding it via "[+ON]" makes the ROM
 * spin for 100,000+ instructions before finally toggling power on
 * release, since ON is a power toggle rather than a USER-mode-
 * assignable function key and was never exercised against the sustained
 * hold mechanism the way the function keys were - see
 * hp41_key_bridge.c's resolve_hold_code() for the full explanation.
 * Callers should always send ON as a plain instant tap.
 *
 * "[CLRMEM]" is a bridge-level command, not a real HP-41 key: it doesn't
 * push anything to keybuffer[]. It sets a one-shot flag, consumed via
 * hp41_key_bridge_clear_memory_requested() below - deliberately not
 * handled inside this file, which stays pure/host-testable and has no
 * business touching flash or resetting CPU state itself (see
 * firmware/main.c, which polls the flag and does the actual reset).
 *
 * @param c The incoming byte, as an int (e.g. from getchar_timeout_us()).
 */
void hp41_key_bridge_feed_byte(int c);

/**
 * @brief Reset the bridge's internal "[NAME]" escape-sequence state to idle.
 *
 * Not needed in normal operation (the state starts idle), but useful at
 * startup for a deterministic initial state, and for test isolation.
 * Also clears any pending "[CLRMEM]" request.
 */
void hp41_key_bridge_reset(void);

/**
 * @brief Check for, and consume, a pending "[CLRMEM]" request.
 *
 * One-shot: returns true at most once per "[CLRMEM]" received - a
 * second call immediately after returns false again, exactly like the
 * request had never come in, until another "[CLRMEM]" arrives.
 *
 * @return true if "[CLRMEM]" was received since the last call to this
 *         function (or since hp41_key_bridge_reset()).
 */
bool hp41_key_bridge_clear_memory_requested(void);

#endif // SOYNUT_HP41_KEY_BRIDGE_H
