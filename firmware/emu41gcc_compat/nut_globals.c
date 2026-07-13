/**
 * @file nut_globals.c
 * @brief Instantiates storage for nutcpu.h's GLOBAL-declared CPU state
 *        (regA, regB, regC, tabpage, typmod, espaceRAM, keybuffer, etc.).
 *
 * In upstream emu41gcc this is emu41.c's job (it's the one file that
 * #define GLOBAL's to nothing instead of extern before including
 * nutcpu.h) - but emu41.c is a whole DOS console monitor app we don't
 * want (main(), command-line debugger, alloc.h/conio.h dependencies).
 * This file exists purely to play that one role, exactly once, without
 * dragging any of that in.
 */

#define GLOBAL
#include "nutcpu.h"
