/**
 * @file persist_state_test.c
 * @brief Native (host) test for firmware/hp41_persist_state.c - the pure
 *        pack/unpack/validate half of "continuous memory" persistence
 *        (see firmware/hp41_persist_flash.c for the separate, ARM-only
 *        hardware half this deliberately doesn't need or touch).
 *
 * Fills the live Nut CPU/RAM globals with known, distinct values,
 * captures a snapshot, wipes the globals back to zero, restores from
 * the snapshot, and checks every persisted field round-trips exactly.
 * Separately confirms hp41_persist_validate() actually rejects the
 * cases it exists to catch: erased flash (all 0xFF), zeroed/garbage
 * data, a flipped checksum bit, and a version mismatch - each checked
 * against a genuine, positively-accepted snapshot so the rejection
 * checks aren't vacuous.
 *
 * Doesn't call nut_boot()/executeNUT() at all - like key_hold_test.c,
 * this only needs nutcpu.h's global storage (via
 * emu41gcc_compat/nut_globals.c) and the module under test.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_persist_state.h"

/**
 * @brief Zero every global field hp41_persist_state_t captures/restores.
 *
 * Deliberately mirrors exactly the field list hp41_persist_capture()/
 * hp41_persist_apply() touch - see hp41_persist_state.h's struct comment
 * for why regPC/flagKey/etc. are excluded from that list (and so from
 * this reset too).
 */
static void reset_persisted_globals(void)
{
    memset(espaceRAM, 0, sizeof(espaceRAM));
    memset(regA, 0, sizeof(regA));
    memset(regB, 0, sizeof(regB));
    memset(regC, 0, sizeof(regC));
    memset(regM, 0, sizeof(regM));
    memset(regN, 0, sizeof(regN));
    regST = 0;
    memset(regPQ, 0, sizeof(regPQ));
    regG = 0;
    Carry = 0;
    regK = 0;
    regFO = 0;
    regFI = 0;
    regPT = 0;
    flagdec = 0;
    regData = 0;
    regPer = 0;
    mode_printer = 0;
    flagPrter = 0;
    flagPrx = 0;
    flagAdv = 0;
    assert(espaceRAM[0] == 0 && espaceRAM[8199] == 0);
    assert(regA[0] == 0 && regN[13] == 0);
}

/**
 * @brief Fill every persisted global with a known, distinct pattern.
 *
 * Values are individually distinguishable (not all the same constant)
 * so a field-swapping bug in capture()/apply() would show up as a
 * mismatch rather than accidentally passing.
 */
static void fill_known_pattern(void)
{
    for (int i = 0; i < 8200; i++)
        espaceRAM[i] = (unsigned char)(i & 0xFF);
    for (int i = 0; i < 14; i++) {
        regA[i] = (unsigned char)(0x10 + i);
        regB[i] = (unsigned char)(0x20 + i);
        regC[i] = (unsigned char)(0x30 + i);
        regM[i] = (unsigned char)(0x40 + i);
        regN[i] = (unsigned char)(0x50 + i);
    }
    regST = 0x0BAD;
    regPQ[0] = 1;
    regPQ[1] = 0;
    regG = 7;
    Carry = 1;
    regK = 0x37;
    regFO = 0x11;
    regFI = 0x2233u;
    regPT = 1;
    flagdec = 1;
    regData = 42;
    regPer = -7;
    mode_printer = (char)-1; /* the real value nut_boot() sets - see nut_rom.c */
    flagPrter = 1;
    flagPrx = 0;
    flagAdv = 1;
    assert(espaceRAM[0] == 0 && espaceRAM[255] == 255);
    assert(regA[0] == 0x10 && regN[13] == 0x5D);
}

/**
 * @brief Print a labeled pass/fail line for one boolean check.
 * @param label Human-readable name for this check.
 * @param cond  The check's result (0 or 1).
 * @return @p cond, unchanged.
 */
static int check(const char *label, int cond)
{
    assert(label != NULL);
    assert(cond == 0 || cond == 1); /* C's logical/relational operators always yield 0 or 1 */
    printf("%-58s %s\n", label, cond ? "OK" : "MISMATCH");
    return cond;
}

/** capture()'s own magic/version/validate contract, then a full
 *  wipe-and-restore round trip across every persisted field group. */
#define ROUND_TRIP_CHECK_COUNT 8

/**
 * @brief Verify a full capture()/apply() round trip restores every field exactly.
 * @return Number of failed checks (0 = all pass).
 */
static int test_round_trip(void)
{
    int failures = 0;
    assert(failures == 0);

    reset_persisted_globals();
    fill_known_pattern();

    hp41_persist_state_t snap;
    hp41_persist_capture(&snap);

    failures += !check("capture() sets magic/version",
                        snap.magic == HP41_PERSIST_MAGIC && snap.version == HP41_PERSIST_VERSION);
    failures += !check("capture() produces a struct that validates",
                        hp41_persist_validate(&snap));

    reset_persisted_globals();
    failures += !check("globals genuinely zeroed before apply()",
                        espaceRAM[255] == 0 && regN[13] == 0);

    hp41_persist_apply(&snap);

    failures += !check("espaceRAM round-trips exactly",
                        memcmp(espaceRAM, snap.espaceRAM, sizeof(espaceRAM)) == 0);
    failures += !check("regA-N round-trip exactly",
                        memcmp(regA, snap.regA, sizeof(regA)) == 0 &&
                        memcmp(regB, snap.regB, sizeof(regB)) == 0 &&
                        memcmp(regC, snap.regC, sizeof(regC)) == 0 &&
                        memcmp(regM, snap.regM, sizeof(regM)) == 0 &&
                        memcmp(regN, snap.regN, sizeof(regN)) == 0);
    failures += !check("status registers round-trip exactly (regST/regPQ/regG/Carry/regK/regFO/regFI)",
                        regST == 0x0BAD && regPQ[0] == 1 && regPQ[1] == 0 && regG == 7 &&
                        Carry == 1 && regK == 0x37 && regFO == 0x11 && regFI == snap.regFI);
    failures += !check("mode/data-select fields round-trip exactly (regPT/flagdec/regData/regPer)",
                        regPT == 1 && flagdec == 1 && regData == 42 && regPer == -7);
    failures += !check("printer flags round-trip exactly (mode_printer/flagPrter/flagPrx/flagAdv)",
                        mode_printer == (char)-1 && flagPrter == 1 && flagPrx == 0 && flagAdv == 1);

    assert(failures >= 0 && failures <= ROUND_TRIP_CHECK_COUNT);
    return failures;
}

/** A positive control (a genuine snapshot must validate) plus every
 *  rejection case hp41_persist_validate() exists to catch. */
#define VALIDATE_CHECK_COUNT 5

/**
 * @brief Verify hp41_persist_validate() accepts a genuine snapshot and
 *        rejects every corrupted variant.
 * @return Number of failed checks (0 = all pass).
 */
static int test_validate_rejects_corruption(void)
{
    int failures = 0;
    assert(failures == 0);

    reset_persisted_globals();
    fill_known_pattern();
    hp41_persist_state_t good;
    hp41_persist_capture(&good);
    failures += !check("a genuine captured snapshot validates (positive control)",
                        hp41_persist_validate(&good));

    hp41_persist_state_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    failures += !check("an all-zero struct is rejected", !hp41_persist_validate(&zeroed));

    hp41_persist_state_t erased;
    memset(&erased, 0xFF, sizeof(erased));
    failures += !check("erased-flash content (all 0xFF) is rejected", !hp41_persist_validate(&erased));

    hp41_persist_state_t bad_checksum = good;
    bad_checksum.checksum ^= 1u;
    failures += !check("a flipped checksum bit is rejected", !hp41_persist_validate(&bad_checksum));

    hp41_persist_state_t bad_version = good;
    bad_version.version += 1u;
    failures += !check("a version mismatch is rejected", !hp41_persist_validate(&bad_version));

    assert(failures >= 0 && failures <= VALIDATE_CHECK_COUNT);
    return failures;
}

#define TOTAL_CHECK_COUNT (ROUND_TRIP_CHECK_COUNT + VALIDATE_CHECK_COUNT)

/**
 * @brief Run all persist-state check groups and report pass/fail.
 * @return 0 on pass, 1 on fail.
 */
int main(void)
{
    const int failures = test_round_trip() + test_validate_rejects_corruption();
    assert(failures >= 0);
    assert(failures <= TOTAL_CHECK_COUNT);

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
