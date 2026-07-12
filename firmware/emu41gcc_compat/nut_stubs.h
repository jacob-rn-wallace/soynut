/* Prototypes for the HP82143 printer entry points that emu41gcc's
 * execp() (in nutcpu.c) calls but that upstream never declared in any
 * header - the original DOS build relied on old implicit-int function
 * declarations. Force-included via the build's -include flag when
 * compiling nutcpu.c, so the vendored source can stay byte-for-byte
 * unmodified while still building under a modern compiler that treats
 * implicit declarations as an error.
 */
#pragma once

void print_char(int n);
int  get_printer_status(void);
int  test_printer_flag(int n);
