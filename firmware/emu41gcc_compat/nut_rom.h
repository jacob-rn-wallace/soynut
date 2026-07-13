/**
 * @file nut_rom.h
 * @brief Wires the base HP-41 ROM into the Nut CPU core and resets it to
 *        cold-start state.
 */
#pragma once

/**
 * @brief Wire the base HP-41CV OS ROM pages into tabpage[]/typmod[] and
 *        reset CPU state to Nut's documented cold-start values.
 *
 * Call once, before the first executeNUT().
 */
void nut_boot(void);
