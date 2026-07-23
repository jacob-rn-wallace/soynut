/**
 * @file sim_keyboard.h
 * @brief Maps SDL keyboard events to the exact same USB-serial wire
 *        protocol bytes tools/hp41_keyboard_gui.py sends, and feeds them
 *        through the unmodified firmware/hp41_key_bridge.c parser -
 *        this file only ever decides *which* bytes to send, never
 *        touches keybuffer[]/hold state directly.
 */
#ifndef SOYNUT_SIM_KEYBOARD_H
#define SOYNUT_SIM_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Reset the tap/hold tracking state to idle.
 *
 * Call once at startup.
 */
void sim_keyboard_init(void);

/**
 * @brief Drain pending SDL events and advance the tap/hold state machine.
 *
 * Drains SDL's event queue (bounded - see sim_keyboard.c), routing key
 * presses/releases through the same threshold-based tap-vs-hold
 * decision tools/hp41_keyboard_gui.py's PressMode.THRESHOLD makes
 * (HOLD_ENGAGE_MS, matching that tool's constant) and sending the
 * resulting bytes through hp41_key_bridge_feed_byte(). Also checks
 * whether the currently-tracked key's hold-engage deadline has been
 * reached, since that can happen with no new event at all. Call once
 * per sim_main.c outer-loop iteration, every iteration, not just when
 * something happened.
 *
 * @param now_ms Current sim time in milliseconds (see sim_clock.h).
 * @return true if the window was asked to close (SDL_QUIT or the window
 *         manager's close button).
 */
bool sim_keyboard_poll(uint32_t now_ms);

#endif // SOYNUT_SIM_KEYBOARD_H
