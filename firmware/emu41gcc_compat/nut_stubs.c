/**
 * @file nut_stubs.c
 * @brief No-op stand-ins for the peripheral modules emu41gcc expects
 *        (timer.h, hpil.h, and the HP82143 printer entry points declared
 *        in nut_stubs.h) so nutcpu.c's execp()/storeData()/recallData()
 *        have something to link against.
 *
 * None of these peripherals are being emulated yet - a base HP-41CV has
 * no clock, HP-IL, or printer module plugged in, so real behavior here
 * is "there's nothing there." Real timer/HP-IL/printer emulation
 * (emu41gcc's ignore/timer.c, ignore/hpil.c, printer.c) can replace
 * these later without touching nutcpu.c.
 *
 * This file also instantiates storage for timer.h's and hpil.h's
 * GLOBAL-declared state (clock_reg, hpil_reg, etc.) - exactly one
 * translation unit needs to do that, same pattern emu41gcc's own
 * ignore/timer.c and ignore/hpil.c use.
 *
 * Power of 10, Rule 5 note: these bodies deliberately carry no
 * assertions. Each one's entire behavior is "discard the parameter,
 * return a fixed constant" - there is no precondition to check and no
 * postcondition beyond what the return statement already states, so an
 * assertion here would just restate the line above it rather than
 * catch a real anomaly.
 */

#define GLOBAL
#include "timer.h"
#undef GLOBAL

#define GLOBAL
#include "hpil.h"
#undef GLOBAL

#include "nut_stubs.h"

/** No HP82143C Time Module plugged in - stub read, no-op. */
void timer_rd_n(int n)  { (void)n; }
/** No HP82143C Time Module plugged in - stub write, no-op. */
void timer_wr_n(int n)  { (void)n; }
/** No HP82143C Time Module plugged in - stub write, no-op. */
void timer_wr(void)     { }
/** No HP82143C Time Module plugged in - stub init, no-op. */
void init_timer(void)   { }

/** No HP-IL module plugged in - stub init, no-op. */
void init_hpil(void)        { }
/** No HP-IL module plugged in - stub write, no-op. */
void hpil_wr(int reg, int n) { (void)reg; (void)n; }
/** No HP-IL module plugged in - stub read, always returns 0. */
int  hpil_rd(int reg)        { (void)reg; return 0; }

/** No HP82143A printer plugged in - stub char-print, no-op. */
void print_char(int n)          { (void)n; }
/** No HP82143A printer plugged in - stub status read, always "not present". */
int  get_printer_status(void)   { return 0; }
/** No HP82143A printer plugged in - stub flag test, always false/0. */
int  test_printer_flag(int n)   { (void)n; return 0; }
