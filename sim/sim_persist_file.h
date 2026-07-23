/**
 * @file sim_persist_file.h
 * @brief Host-file-backed stand-in for firmware/hp41_persist_flash.h's
 *        QSPI-flash-backed continuous memory - same 3-function contract,
 *        a local file instead of on-die flash.
 */
#ifndef SOYNUT_SIM_PERSIST_FILE_H
#define SOYNUT_SIM_PERSIST_FILE_H

#include <stdbool.h>

#include "hp41_persist_state.h"

/**
 * @brief Load and validate the persisted snapshot from the sim's persist file.
 *
 * Mirrors firmware/hp41_persist_flash.h's hp41_persist_flash_load(): a
 * missing file, a short/corrupt read, or a struct that fails
 * hp41_persist_validate() are all treated the same as real hardware
 * treats erased flash - "no valid snapshot", not an error.
 *
 * @param out Struct to fill.
 * @return true if @p out now holds a valid, restorable snapshot.
 */
bool hp41_persist_flash_load(hp41_persist_state_t *out);

/**
 * @brief Write a snapshot to the sim's persist file.
 *
 * @param state Snapshot to save.
 */
void hp41_persist_flash_save(const hp41_persist_state_t *state);

/**
 * @brief Delete the sim's persist file, if present.
 */
void hp41_persist_flash_erase(void);

#endif // SOYNUT_SIM_PERSIST_FILE_H
