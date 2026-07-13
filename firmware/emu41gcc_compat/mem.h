/**
 * @file mem.h
 * @brief Compatibility shim for emu41gcc's DOS-era <mem.h> include.
 *
 * nutcpu.c only uses this for memcpy()/memset(), which live in the
 * standard <string.h> on every modern toolchain.
 */
#pragma once

#include <string.h>
