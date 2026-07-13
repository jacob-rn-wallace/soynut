/**
 * @file hp41_key_bridge.h
 * @brief Translates incoming USB-serial protocol bytes into presses on
 *        the Nut CPU's keybuffer[].
 */
#ifndef SOYNUT_HP41_KEY_BRIDGE_H
#define SOYNUT_HP41_KEY_BRIDGE_H

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
 * unrecognized name).
 *
 * @param c The incoming byte, as an int (e.g. from getchar_timeout_us()).
 */
void hp41_key_bridge_feed_byte(int c);

/**
 * @brief Reset the bridge's internal "[NAME]" escape-sequence state to idle.
 *
 * Not needed in normal operation (the state starts idle), but useful at
 * startup for a deterministic initial state, and for test isolation.
 */
void hp41_key_bridge_reset(void);

#endif // SOYNUT_HP41_KEY_BRIDGE_H
