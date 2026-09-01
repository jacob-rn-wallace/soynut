/**
 * @file nut_stubs.c
 * @brief No-op stand-ins for the peripheral modules emu41gcc expects
 *        (timer.h, and the HP82143 printer entry points declared in
 *        nut_stubs.h) so nutcpu.c's execp()/storeData()/recallData()
 *        have something to link against.
 *
 * None of these peripherals are being emulated yet - a base HP-41CV has
 * no clock or printer module plugged in, so real behavior here is
 * "there's nothing there." Real timer/printer emulation (emu41gcc's
 * ignore/timer.c, printer.c) can replace these later without touching
 * nutcpu.c.
 *
 * HP-IL is no longer a stub here - see hp41_hpil_controller.c/.h for
 * the real init_hpil()/hpil_wr()/hpil_rd() implementation and hpil.h's
 * GLOBAL-declared storage (hpil_reg[], flgenb), both now owned by that
 * file instead.
 *
 * This file instantiates storage for timer.h's GLOBAL-declared state
 * (clock_reg, etc.) - exactly one translation unit needs to do that,
 * same pattern emu41gcc's own ignore/timer.c uses.
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

#include "nut_stubs.h"

/** No HP82143C Time Module plugged in - stub read, no-op. */
void timer_rd_n(int n)  { (void)n; }
/** No HP82143C Time Module plugged in - stub write, no-op. */
void timer_wr_n(int n)  { (void)n; }
/** No HP82143C Time Module plugged in - stub write, no-op. */
void timer_wr(void)     { }
/** No HP82143C Time Module plugged in - stub init, no-op. */
void init_timer(void)   { }

/** No HP82143A printer plugged in - stub char-print, no-op. */
void print_char(int n)          { (void)n; }
/** No HP82143A printer plugged in - stub status read, always "not present". */
int  get_printer_status(void)   { return 0; }
/** No HP82143A printer plugged in - stub flag test, always false/0. */
int  test_printer_flag(int n)   { (void)n; return 0; }
