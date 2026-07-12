/* Compatibility shim for emu41gcc's DOS-era <dos.h> include.
 *
 * nutcpu.c only needs this header for the Borland/Watcom `near`/`far`
 * memory-model keywords (used live in `#define LOCAL static near`) and a
 * commented-out MK_FP() far-pointer call. Neither means anything on ARM,
 * so both keywords are defined away. This lets nutcpu.c compile completely
 * unmodified under a standards-conforming modern compiler.
 */
#pragma once

#define near
#define far
