#pragma once

/* Wires the base HP-41CV OS ROM pages into nutcpu.h's tabpage[]/typmod[]
 * and resets CPU state to Nut's documented cold-start values. Call once,
 * before the first executeNUT().
 */
void nut_boot(void);
