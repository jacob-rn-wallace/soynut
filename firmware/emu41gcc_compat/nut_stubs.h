/**
 * @file nut_stubs.h
 * @brief Prototypes for the HP82143 printer entry points that emu41gcc's
 *        execp() (in nutcpu.c) calls but that upstream never declared in
 *        any header.
 *
 * The original DOS build relied on old implicit-int function
 * declarations. Force-included via the build's -include flag when
 * compiling nutcpu.c, so the vendored source can stay byte-for-byte
 * unmodified while still building under a modern compiler that treats
 * implicit declarations as an error.
 */
#pragma once

/** No-op stand-in - see nut_stubs.c. @param n Ignored. */
void print_char(int n);
/** No-op stand-in - see nut_stubs.c. @return Always 0. */
int  get_printer_status(void);
/** No-op stand-in - see nut_stubs.c. @param n Ignored. @return Always 0. */
int  test_printer_flag(int n);
