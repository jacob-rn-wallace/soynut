/**
 * @file hp41_persist_state.h
 * @brief Snapshot/restore/validate the Nut CPU's "continuous memory" -
 *        the subset of emu41gcc's global state that the real HP-41's
 *        battery-backed CMOS RAM would have kept alive across a power
 *        cycle - into a flat, flash-storable struct.
 */
#ifndef SOYNUT_HP41_PERSIST_STATE_H
#define SOYNUT_HP41_PERSIST_STATE_H

#include <stdbool.h>
#include <stdint.h>

/** Marks a struct as "this looks like a real saved snapshot", distinct
 *  from erased flash (which reads back as all 0xFF) or unrelated data. */
#define HP41_PERSIST_MAGIC 0x48503431u /* "HP41" */

/** Bump whenever a field is added/removed/reordered below - a version
 *  mismatch is treated exactly like "no valid snapshot", so a firmware
 *  update that reshapes this struct safely falls back to a cold
 *  MEMORY LOST boot instead of misinterpreting stale bytes. */
#define HP41_PERSIST_VERSION 1u

/**
 * @brief Flat snapshot of the HP-41's "continuous memory" state.
 *
 * Deliberately excludes key-scan/execution/render bookkeeping
 * (regPC, flagKey, flagKB, cptKey, keybuffer[]/lgkeybuf, smartp,
 * breakcode, selper, cptinstr, fjmp, dspon, facces_dsp, fdsp) - those
 * aren't "memory" in the HP-41 sense, and restoring them verbatim risks
 * resurrecting a stuck mid-instruction/mid-keypress state. They already
 * reset correctly on their own via main.c's existing POWOFF->wake path
 * and the key bridge's own idle-on-boot state.
 */
typedef struct {
    uint32_t magic;
    uint32_t version;

    uint8_t espaceRAM[8200];
    uint8_t regA[14];
    uint8_t regB[14];
    uint8_t regC[14];
    uint8_t regM[14];
    uint8_t regN[14];

    uint32_t regST;
    uint8_t  regPQ[2];
    uint8_t  regG;
    uint8_t  Carry;
    uint8_t  regK;
    uint8_t  regFO;
    uint32_t regFI;

    int8_t  regPT;
    int8_t  flagdec;
    int32_t regData;
    int32_t regPer;

    int8_t mode_printer;
    int8_t flagPrter;
    int8_t flagPrx;
    int8_t flagAdv;

    uint32_t checksum; /* over every field above; always computed last */
} hp41_persist_state_t;

/**
 * @brief Snapshot the live Nut CPU/RAM globals into a persistable struct.
 *
 * Pure logic (reads emu41gcc's global state directly, same pattern as
 * hp41_display_bridge.c) - no hardware access, safe to call/test on a
 * host build. Sets magic/version and computes checksum last, so the
 * result is immediately valid input to hp41_persist_validate().
 *
 * @param out Struct to fill; fully overwritten.
 */
void hp41_persist_capture(hp41_persist_state_t *out);

/**
 * @brief Write a previously-captured snapshot back into the live globals.
 *
 * Caller must have validated @p state first (hp41_persist_validate()) -
 * this function trusts its input completely and does not re-check it.
 *
 * @param state Snapshot to apply.
 */
void hp41_persist_apply(const hp41_persist_state_t *state);

/**
 * @brief Check whether a struct looks like a genuine, uncorrupted snapshot.
 *
 * Verifies the magic number, version, and checksum. Rejects erased flash
 * (reads back as all 0xFF), a snapshot saved by a different firmware
 * version's struct layout, and simple bit-level corruption.
 *
 * @param state Struct to check.
 * @return true if @p state is safe to pass to hp41_persist_apply().
 */
bool hp41_persist_validate(const hp41_persist_state_t *state);

#endif // SOYNUT_HP41_PERSIST_STATE_H
