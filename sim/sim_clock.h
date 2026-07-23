/**
 * @file sim_clock.h
 * @brief Host timing replacements for the pico-sdk calls firmware/main.c
 *        relies on (to_ms_since_boot(get_absolute_time()), sleep_us()).
 *
 * Isolated to one small module so the SDL-vs-POSIX timing choice is a
 * one-file decision - see sim_clock.c for why each function picks the
 * API it does.
 */
#ifndef SOYNUT_SIM_CLOCK_H
#define SOYNUT_SIM_CLOCK_H

#include <stdint.h>

/**
 * @brief Milliseconds elapsed since this process's first call to this
 *        module's functions (effectively "since sim start").
 *
 * Replaces to_ms_since_boot(get_absolute_time()) - real firmware
 * measures since the Pico's power-on/reset, this measures since the
 * simulator started, which is the same kind of monotonic reference
 * point for the heartbeat/idle-timer logic that uses it.
 *
 * @return Milliseconds since first use, monotonic.
 */
uint32_t sim_clock_now_ms(void);

/**
 * @brief Sleep for approximately the given number of microseconds.
 *
 * Replaces sleep_us() - used to throttle the emulated CPU to
 * TARGET_INSTRUCTIONS_PER_SEC. Implemented with nanosleep() rather than
 * SDL_Delay() specifically because SDL_Delay()'s 1ms granularity is too
 * coarse for the throttle's legitimately sub-millisecond values at high
 * instruction counts.
 *
 * @param us Microseconds to sleep; 0 is a valid no-op.
 */
void sim_clock_sleep_us(uint32_t us);

#endif // SOYNUT_SIM_CLOCK_H
