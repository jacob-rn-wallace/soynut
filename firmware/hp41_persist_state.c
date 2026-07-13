/**
 * @file hp41_persist_state.c
 * @brief Pure pack/unpack/validate logic for hp41_persist_state_t - see
 *        hp41_persist_state.h. No hardware access; hp41_persist_flash.c
 *        is the separate, ARM-only half that actually touches flash.
 */

#include "hp41_persist_state.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

/**
 * @brief FNV-1a hash over every field of @p state preceding its own
 *        checksum field.
 *
 * A simple, deterministic integrity check - not cryptographic, just
 * strong enough to reject erased flash (all 0xFF) and ordinary bit
 * corruption, which is all hp41_persist_validate() needs. The loop
 * bound (offsetof(..., checksum)) is a compile-time constant, so this
 * satisfies Power of 10 Rule 2 the same way every other fixed-size
 * array loop in this project does.
 *
 * @param state Struct to hash (checksum field itself is not included).
 * @return The computed hash.
 */
static uint32_t compute_checksum(const hp41_persist_state_t *state)
{
    assert(state != NULL);
    const uint8_t *bytes = (const uint8_t *)state;
    const size_t len = offsetof(hp41_persist_state_t, checksum);
    uint32_t hash = 2166136261u; /* FNV-1a 32-bit offset basis */
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619u; /* FNV-1a 32-bit prime */
    }
    assert(len == offsetof(hp41_persist_state_t, checksum));
    return hash;
}

/**
 * @brief Snapshot the live Nut CPU/RAM globals; see the header.
 *
 * @param out Struct to fill; fully overwritten.
 */
void hp41_persist_capture(hp41_persist_state_t *out)
{
    assert(out != NULL);
    memset(out, 0, sizeof(*out));

    out->magic = HP41_PERSIST_MAGIC;
    out->version = HP41_PERSIST_VERSION;

    memcpy(out->espaceRAM, espaceRAM, sizeof(out->espaceRAM));
    memcpy(out->regA, regA, sizeof(out->regA));
    memcpy(out->regB, regB, sizeof(out->regB));
    memcpy(out->regC, regC, sizeof(out->regC));
    memcpy(out->regM, regM, sizeof(out->regM));
    memcpy(out->regN, regN, sizeof(out->regN));

    out->regST = regST;
    memcpy(out->regPQ, regPQ, sizeof(out->regPQ));
    out->regG = regG;
    out->Carry = Carry;
    out->regK = regK;
    out->regFO = regFO;
    out->regFI = regFI;

    out->regPT = (int8_t)regPT;
    out->flagdec = (int8_t)flagdec;
    out->regData = regData;
    out->regPer = regPer;

    out->mode_printer = (int8_t)mode_printer;
    out->flagPrter = (int8_t)flagPrter;
    out->flagPrx = (int8_t)flagPrx;
    out->flagAdv = (int8_t)flagAdv;

    out->checksum = compute_checksum(out);

    assert(out->magic == HP41_PERSIST_MAGIC && out->version == HP41_PERSIST_VERSION);
    assert(hp41_persist_validate(out));
}

/**
 * @brief Write a previously-captured snapshot back into the live globals;
 *        see the header.
 *
 * @param state Snapshot to apply.
 */
void hp41_persist_apply(const hp41_persist_state_t *state)
{
    assert(state != NULL);

    memcpy(espaceRAM, state->espaceRAM, sizeof(espaceRAM));
    memcpy(regA, state->regA, sizeof(regA));
    memcpy(regB, state->regB, sizeof(regB));
    memcpy(regC, state->regC, sizeof(regC));
    memcpy(regM, state->regM, sizeof(regM));
    memcpy(regN, state->regN, sizeof(regN));

    regST = state->regST;
    memcpy(regPQ, state->regPQ, sizeof(regPQ));
    regG = state->regG;
    Carry = state->Carry;
    regK = state->regK;
    regFO = state->regFO;
    regFI = state->regFI;

    regPT = (char)state->regPT;
    flagdec = (char)state->flagdec;
    regData = state->regData;
    regPer = state->regPer;

    mode_printer = (char)state->mode_printer;
    flagPrter = (char)state->flagPrter;
    flagPrx = (char)state->flagPrx;
    flagAdv = (char)state->flagAdv;

    assert(regST == state->regST && Carry == state->Carry);
}

/**
 * @brief Check whether a struct looks like a genuine, uncorrupted
 *        snapshot; see the header.
 *
 * @param state Struct to check.
 * @return true if safe to pass to hp41_persist_apply().
 */
bool hp41_persist_validate(const hp41_persist_state_t *state)
{
    assert(state != NULL);
    if (state->magic != HP41_PERSIST_MAGIC)
        return false;
    if (state->version != HP41_PERSIST_VERSION)
        return false;
    bool ok = state->checksum == compute_checksum(state);
    assert(ok == true || ok == false);
    return ok;
}
