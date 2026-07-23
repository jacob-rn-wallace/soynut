/**
 * @file sim_clock.c
 * @brief Host timing replacements for the pico-sdk calls firmware/main.c
 *        relies on. See sim_clock.h for the contract.
 */

#include "sim_clock.h"

#include <assert.h>
#include <errno.h>
#include <time.h>

#include <SDL.h>

/** Bounded retry cap for sim_clock_sleep_us()'s nanosleep() loop (Power
 *  of 10, Rule 2) - see that function for why an EINTR retry loop needs
 *  one at all. */
#define MAX_NANOSLEEP_RETRIES 16

/**
 * @brief Milliseconds elapsed since sim start; see the header.
 *
 * @return Milliseconds since first use, monotonic.
 */
static uint32_t last_now_ms = 0;

uint32_t sim_clock_now_ms(void)
{
    /* SDL_GetTicks() itself is already "milliseconds since SDL_Init()",
     * which for this program is effectively "since sim start" - no
     * separate epoch bookkeeping needed. Millisecond granularity is
     * plenty for the heartbeat/idle-timer logic that consumes this. */
    Uint32 ticks = SDL_GetTicks();
    uint32_t now_ms = (uint32_t)ticks;
    assert(now_ms == ticks); /* documents the Uint32->uint32_t width match on every real target */
    /* SDL_GetTicks() is monotonic non-decreasing for any realistic sim
     * session (the Uint32 tick counter only wraps after ~49 days of
     * continuous runtime) - catches a real clock-source bug rather than
     * restating a triviality. */
    assert(now_ms >= last_now_ms);
    last_now_ms = now_ms;
    return now_ms;
}

/**
 * @brief Sleep for approximately the given number of microseconds; see the header.
 *
 * @param us Microseconds to sleep; 0 is a valid no-op.
 */
void sim_clock_sleep_us(uint32_t us)
{
    struct timespec req;
    req.tv_sec = us / 1000000u;
    req.tv_nsec = (long)(us % 1000000u) * 1000L;
    assert(req.tv_nsec >= 0 && req.tv_nsec < 1000000000L);

    /* nanosleep() can return early (e.g. on a signal) with the
     * remaining time in a second out-param - retry with that remainder
     * a bounded number of times (Power of 10, Rule 2) rather than
     * looping until it succeeds; a real signal storm dropping every
     * retry is not a case worth spinning forever over for a throttle
     * sleep this short. */
    struct timespec remaining = req;
    int attempt;
    for (attempt = 0; attempt < MAX_NANOSLEEP_RETRIES; attempt++) {
        if (nanosleep(&remaining, &remaining) == 0 || errno != EINTR)
            break;
    }
    assert(attempt <= MAX_NANOSLEEP_RETRIES);
}
