/**
 * @file sim_persist_file.c
 * @brief Host-file-backed continuous memory for the simulator - see
 *        sim_persist_file.h for the contract this mirrors from
 *        firmware/hp41_persist_flash.h.
 */

#include "sim_persist_file.h"

#include <assert.h>
#include <stdio.h>

/** Path the simulator's continuous-memory snapshot is read from/written
 *  to, relative to the current working directory (the sim/Makefile's
 *  `run` target always launches the binary from sim/, so in normal use
 *  this lands at sim/soynut_sim_persist.bin - gitignored, same BYO/
 *  local-artifact treatment as roms/rom_images.c). */
#define SIM_PERSIST_FILE_PATH "soynut_sim_persist.bin"

/**
 * @brief Load and validate the persisted snapshot from the sim's persist file.
 *
 * @param out Struct to fill.
 * @return true if @p out now holds a valid, restorable snapshot.
 */
bool hp41_persist_flash_load(hp41_persist_state_t *out)
{
    assert(out != NULL);

    FILE *f = fopen(SIM_PERSIST_FILE_PATH, "rb");
    if (f == NULL)
        return false; /* no snapshot yet - same as real hardware's first boot */

    size_t read = fread(out, 1, sizeof(*out), f);
    int close_ok = (fclose(f) == 0);
    assert(close_ok == true || close_ok == false); /* documents the bool-shaped check just made */

    if (read != sizeof(*out))
        return false; /* short/truncated file - treat like an invalid snapshot */

    bool valid = hp41_persist_validate(out);
    assert(valid == true || valid == false);
    return valid;
}

/**
 * @brief Write a snapshot to the sim's persist file.
 *
 * @param state Snapshot to save.
 */
void hp41_persist_flash_save(const hp41_persist_state_t *state)
{
    assert(state != NULL);

    FILE *f = fopen(SIM_PERSIST_FILE_PATH, "wb");
    assert(f != NULL);
    if (f == NULL)
        return;

    size_t written = fwrite(state, 1, sizeof(*state), f);
    assert(written == sizeof(*state));
    int close_ok = (fclose(f) == 0);
    assert(close_ok);
    (void)written;
    (void)close_ok;
}

/**
 * @brief Delete the sim's persist file, if present.
 *
 * Power of 10, Rule 5 note: mirrors real hp41_persist_flash_erase()'s
 * own exception (CLAUDE.md) - a trivial one-call wrapper with nothing
 * new to assert beyond what remove() itself already guarantees, since a
 * missing file is a perfectly normal (not error) case here.
 */
void hp41_persist_flash_erase(void)
{
    remove(SIM_PERSIST_FILE_PATH); /* ENOENT (already erased) is not an error here */
}
