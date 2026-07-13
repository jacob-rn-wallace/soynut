/**
 * @file hp41_persist_flash.c
 * @brief Hardware half of "continuous memory" persistence - reads/writes
 *        an hp41_persist_state_t to the Pico's own on-die QSPI flash. See
 *        hp41_persist_flash.h for the contract.
 */

#include "hp41_persist_flash.h"

#include <assert.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/config.h"

/** Reserve the last 3 flash sectors (12 KiB) for the persisted snapshot -
 *  comfortably covers sizeof(hp41_persist_state_t) (~8.5 KiB) with room
 *  to grow, while leaving the rest of the chip's flash entirely to the
 *  firmware image/linker, which is placed starting from the beginning of
 *  flash and nowhere near this region for a project this size. */
#define HP41_PERSIST_FLASH_SECTORS 3u
#define HP41_PERSIST_FLASH_SIZE (HP41_PERSIST_FLASH_SECTORS * FLASH_SECTOR_SIZE)
#define HP41_PERSIST_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - HP41_PERSIST_FLASH_SIZE)

/* Any future growth of hp41_persist_state_t past the reserved region is a
 * build failure here, not a silent flash overrun at runtime. */
_Static_assert(sizeof(hp41_persist_state_t) <= HP41_PERSIST_FLASH_SIZE,
               "hp41_persist_state_t no longer fits the reserved flash region");

/**
 * @brief Load and validate the persisted snapshot from flash; see the header.
 *
 * @param out Struct to fill.
 * @return true if @p out now holds a valid, restorable snapshot.
 */
bool hp41_persist_flash_load(hp41_persist_state_t *out)
{
    assert(out != NULL);
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + HP41_PERSIST_FLASH_OFFSET);
    memcpy(out, flash_ptr, sizeof(*out));
    bool valid = hp41_persist_validate(out);
    assert(valid == true || valid == false); /* documents the bool contract, same idiom hp41_persist_validate() itself uses */
    return valid;
}

/**
 * @brief Save a snapshot to flash if it differs from what's already there; see the header.
 *
 * @param state Snapshot to save.
 */
void hp41_persist_flash_save(const hp41_persist_state_t *state)
{
    assert(state != NULL);

    /* Static, not stack-allocated - a 12 KiB local array is not a good
     * fit for this target's stack (Power of 10 Rule 3: fixed, static
     * storage, no dynamic allocation). Padded with the same 0xFF value
     * genuinely-erased flash reads back as, so an unwritten tail beyond
     * sizeof(*state) never looks like a spurious difference on the next
     * save's comparison below. */
    static uint8_t write_buf[HP41_PERSIST_FLASH_SIZE];
    memset(write_buf, 0xFF, sizeof(write_buf));
    memcpy(write_buf, state, sizeof(*state));

    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + HP41_PERSIST_FLASH_OFFSET);
    if (memcmp(flash_ptr, write_buf, sizeof(write_buf)) == 0) {
        return; /* unchanged since last save - skip the erase/program cycle entirely */
    }

    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(HP41_PERSIST_FLASH_OFFSET, HP41_PERSIST_FLASH_SIZE);
    flash_range_program(HP41_PERSIST_FLASH_OFFSET, write_buf, sizeof(write_buf));
    restore_interrupts(saved_irq);

    assert(memcmp(flash_ptr, write_buf, sizeof(write_buf)) == 0);
}

/**
 * @brief Erase the persisted-state flash region; see the header.
 *
 * Power of 10, Rule 5 note: a trivial three-line erase/restore wrapper
 * with no precondition to check and no computed postcondition to verify
 * beyond what flash_range_erase() itself already guarantees - the same
 * exception CLAUDE.md documents for hp41_key_bridge_reset() and
 * st7920.c's write_cmd().
 */
void hp41_persist_flash_erase(void)
{
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(HP41_PERSIST_FLASH_OFFSET, HP41_PERSIST_FLASH_SIZE);
    restore_interrupts(saved_irq);
}
