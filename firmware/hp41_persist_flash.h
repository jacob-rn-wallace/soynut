/**
 * @file hp41_persist_flash.h
 * @brief Reads/writes an hp41_persist_state_t snapshot to the Pico's own
 *        on-die QSPI flash - the hardware half of "continuous memory"
 *        persistence (see hp41_persist_state.h for the pure pack/unpack
 *        logic this builds on). ARM/pico-sdk only; never linked into the
 *        host-native test build.
 */
#ifndef SOYNUT_HP41_PERSIST_FLASH_H
#define SOYNUT_HP41_PERSIST_FLASH_H

#include <stdbool.h>

#include "hp41_persist_state.h"

/**
 * @brief Load and validate the persisted snapshot from flash, if any.
 *
 * Reads directly from the XIP-mapped flash address (no SDK call needed
 * for reads) and validates it (hp41_persist_state.h's
 * hp41_persist_validate()) before returning. Safely returns false - not
 * a crash or undefined behavior - both for a genuinely blank/erased chip
 * (first-ever boot) and for a snapshot saved by a different firmware
 * version's struct layout.
 *
 * @param out Struct to fill. Always fully overwritten with whatever was
 *            on flash; only trust its contents if this returns true.
 * @return true if @p out now holds a valid, restorable snapshot.
 */
bool hp41_persist_flash_load(hp41_persist_state_t *out);

/**
 * @brief Save a snapshot to flash, if it differs from what's already there.
 *
 * Compares against the current on-flash bytes first and returns without
 * touching flash at all if nothing changed - this is the flash-wear
 * guard that makes calling this on every POWOFF safe long-term. When a
 * write is actually needed, briefly disables interrupts around the
 * erase+program (required by the Pico SDK's flash API on this
 * single-core firmware - no multicore lockout is needed since core1 is
 * never used here).
 *
 * @param state Snapshot to save.
 */
void hp41_persist_flash_save(const hp41_persist_state_t *state);

/**
 * @brief Erase the persisted-state flash region, discarding any saved snapshot.
 *
 * The next hp41_persist_flash_load() call after this will return false
 * (erased flash reads back as all 0xFF, which never validates) - the
 * mechanism behind the "[CLRMEM]" wire-protocol command's deliberate
 * "give me MEMORY LOST back" reset.
 */
void hp41_persist_flash_erase(void);

#endif // SOYNUT_HP41_PERSIST_FLASH_H
