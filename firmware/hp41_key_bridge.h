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
 * Elite User Mode: **currently deactivated by default** (see
 * hp41_key_bridge_set_elite_mode_feature_enabled() below - real, not-
 * yet-diagnosed display bugs found on first real-hardware use), kept
 * fully described here since every line of the underlying protocol
 * still exists and still runs in tests, just never in production for
 * now. When enabled: typing the real key sequence XEQ, ALPHA, L, E, E, T,
 * ALPHA toggles it on/off - a shadow state machine watches for the
 * matching *keycode* sequence (0x32, 0xc4, 0x72, 0xc0, 0xc0, 0x84,
 * 0xc4), not raw incoming bytes. This matters: it's tracked at
 * push_key_tracked() (hp41_key_bridge.c), the one choke point both the
 * plain-ASCII path (tabcode[] lookups, straight from this function) and
 * the "[NAME]" bracket-escape path (handle_named_key(), below) funnel
 * through - a byte-level tracker would (and, in an earlier version of
 * this feature, genuinely did on real hardware) miss every press that
 * arrives as "[XEQ]"/"[ALPHA]" rather than raw ctrl-X/ctrl-A, which is
 * exactly how tools/hp41_keyboard_gui.py's on-screen buttons send them.
 * Every keycode of the sequence is still pushed to keybuffer[] normally
 * *except* the final ALPHA, which is swallowed only once the full
 * sequence completes - so a real "XEQ ALPHA <other name> ALPHA"
 * sequence is completely unaffected, and no buffering/replay of
 * already-sent keystrokes is ever needed; this also means a sequence
 * typed partly via raw bytes and partly via GUI clicks (or "[NAME]"
 * escapes) still completes correctly, since both paths update the same
 * tracker. Completion sets a one-shot flag, consumed via
 * hp41_key_bridge_elite_mode_toggle_requested() below. "[LEET]" is also
 * accepted as a bracket-escape alias, for testing/tooling convenience,
 * mirroring "[CLRMEM]". Known caveat: since the closing ALPHA is
 * swallowed, the ROM is left mid-alpha-entry (with "LEET" in its own
 * alpha buffer) after every toggle - a real, accepted tradeoff, not a
 * bug (see CLAUDE.md's "Elite User Mode" section). Any keycode that
 * doesn't continue the sequence just resets the shadow tracking to
 * idle; real ROM-side editing (including backspace) is unaffected
 * either way, since it's not this tracker's business what the ROM does
 * with a keycode once it's decided to let it through.
 *
 * While Elite Mode is active (see
 * hp41_key_bridge_set_elite_mode_active() below), a *bare* ALPHA press
 * (0x01, not part of a completing XEQ-ALPHA-LEET-ALPHA sequence) is
 * also intercepted: it's swallowed and instead sets a one-shot flag,
 * consumed via hp41_key_bridge_alpha_row_toggle_requested() below, that
 * toggles the alpha-content row on the display. This means real
 * ALPHA-mode text entry is unavailable while Elite Mode is showing -
 * an accepted tradeoff of the same kind as the closing-ALPHA swallow
 * above, not an oversight.
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

/**
 * @brief Check for, and consume, a pending Elite Mode on/off toggle request.
 *
 * One-shot, same semantics as hp41_key_bridge_clear_memory_requested().
 * Set by completing the real XEQ-ALPHA-LEET-ALPHA key sequence (or the
 * "[LEET]" bracket-escape alias) - see hp41_key_bridge_feed_byte()'s
 * header doc.
 *
 * @return true if the toggle sequence was completed since the last call.
 */
bool hp41_key_bridge_elite_mode_toggle_requested(void);

/**
 * @brief Check for, and consume, a pending Elite Mode alpha-row toggle request.
 *
 * One-shot, same semantics as hp41_key_bridge_clear_memory_requested().
 * Set by a bare ALPHA keypress while Elite Mode is active (see
 * hp41_key_bridge_set_elite_mode_active() below and
 * hp41_key_bridge_feed_byte()'s header doc).
 *
 * @return true if a bare ALPHA press was intercepted since the last call.
 */
bool hp41_key_bridge_alpha_row_toggle_requested(void);

/**
 * @brief Tell the bridge whether Elite Mode is currently active.
 *
 * The bridge needs this to decide whether a bare ALPHA press should be
 * intercepted (see hp41_key_bridge_feed_byte()'s header doc) - it has
 * no other way to know, since Elite Mode's own on/off state is owned by
 * firmware/main.c, not this file. Call this every time main.c flips its
 * own elite-mode state, in either direction.
 *
 * @param active Whether Elite Mode is currently on.
 */
void hp41_key_bridge_set_elite_mode_active(bool active);

/**
 * @brief Enable or disable the Elite Mode feature entirely.
 *
 * Defaults to disabled (false) - production code never needs to call
 * this. Real, not-yet-diagnosed bugs were found on first real-hardware
 * use (see CLAUDE.md's "Elite User Mode" section: the ALPHA annunciator
 * getting stuck lit, and the elite grid always showing all zeros
 * regardless of the actual stack contents), so the feature is
 * deactivated by default rather than deleted - every line of detection/
 * rendering code is still present and still exercised by
 * tests/key_bridge_test.c, which calls this with `true` so the
 * underlying trigger logic keeps being verified even while the
 * production default stays off. Flip the default in
 * hp41_key_bridge.c's own `elite_mode_feature_enabled` declaration
 * (not here) once the display bugs are actually understood.
 *
 * @param enabled Whether the trigger sequence/interception should do
 *                anything at all.
 */
void hp41_key_bridge_set_elite_mode_feature_enabled(bool enabled);

#endif // SOYNUT_HP41_KEY_BRIDGE_H
