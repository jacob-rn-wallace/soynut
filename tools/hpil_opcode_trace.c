/**
 * @file hpil_opcode_trace.c
 * @brief Host-side diagnostic: does the currently-wired base HP-41CV
 *        ROM (NUT0-2, no HP-IL module plugged in - see roms/README.md)
 *        ever execute an HP-IL opcode (`SELPF` targeting the HP-IL
 *        peripheral range, `HPIL=C n`, or anything else that reaches
 *        `execp()` with `selper<8`) on its own, without a real
 *        HP82160A HP-IL Module's own ROM present?
 *
 * On real hardware, HP-IL support came from that separate plug-in
 * module - the base OS by itself might never touch the HP-IL address
 * range at all. If true, firmware/hp41_hpil_controller.c's real
 * protocol logic (see CLAUDE.md's "HP-IL video interface" section) is
 * currently unreachable by anything the calculator itself does,
 * whatever key sequence a user tries - it would only ever fire once a
 * real HP-IL module ROM is also wired in.
 *
 * Same "diff state before/after driving real key sequences through the
 * real ROM" technique flag_array_trace.c already used successfully to
 * find the flag array - here diffing hpil_reg[]/flgenb (both `extern`
 * via hpil.h, real storage now in hp41_hpil_controller.c) instead of
 * espaceRAM. A `positive_control()` pass runs first, deliberately
 * calling hpil_wr() directly to prove this diffing mechanism actually
 * detects a real change before trusting any "no change" result that
 * follows it - a "nothing changed" reading is only meaningful evidence
 * once the detector itself is confirmed working.
 *
 * Build: make -C tools   (see tools/Makefile), then
 *        ./tools/build/hpil_opcode_trace
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "hpil.h"

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_hpil_controller.h"
#include "hp41_hpil_video_bridge.h"
#include "nut_rom.h"

#define MAX_STEPS_PER_KEY 500000

/**
 * @brief Push one HP-41 keycode and single-step until it's fully
 *        processed - identical to flag_array_trace.c's own
 *        press_key(), see that file's header comment for the full
 *        rationale (POWOFF ending this key's run is the expected,
 *        successful outcome).
 * @param tag  Short label for diagnostic output.
 * @param code HP-41 keycode to push.
 */
static void press_key(const char *tag, unsigned char code) {
    assert(tag != NULL);
    assert(lgkeybuf >= 0 && lgkeybuf < 8);
    keybuffer[lgkeybuf++] = code;
    flagKey = 0;
    regPC = 0;
    for (int i = 0; i < MAX_STEPS_PER_KEY; i++) {
        int ret = executeNUT(1);
        if (ret == 1)
            return;
        if (ret == 2) {
            printf("  [%s] unexpected INVALID OPCODE after key 0x%02X at PC=0x%04X\n",
                   tag, code, regPC);
            return;
        }
    }
    printf("  [%s] key 0x%02X never reached POWOFF within %d steps\n",
           tag, code, MAX_STEPS_PER_KEY);
}

/** @brief Press a sequence of keys in order. @param tag Label. @param codes Keycodes. @param n Count. */
static void press_keys(const char *tag, const unsigned char *codes, int n) {
    assert(tag != NULL);
    assert(codes != NULL);
    assert(n > 0);
    for (int i = 0; i < n; i++)
        press_key(tag, codes[i]);
}

/* HP-41 keycodes - same source/provenance as flag_array_trace.c's own table. */
#define KEY_ON 0x18
#define KEY_XEQ 0x32
#define KEY_ALPHA 0xc4
#define KEY_ENTER 0x13 /* CR, per hp41_key_bridge.c's tabcode[] */
#define KEY_PLUS 0x15  /* '+' */
#define KEY_A 0x10
#define KEY_C 0x70
#define KEY_G 0x31
#define KEY_L 0x72
#define KEY_O 0x73
#define KEY_R 0x34
#define KEY_T 0x84
#define KEY_0 0x37
#define KEY_1 0x36
#define KEY_2 0x76
#define KEY_3 0x86
#define KEY_4 0x35
#define KEY_5 0x75
#define KEY_6 0x85

/** @brief Boot the ROM and wake it to the normal "ready" state - identical to flag_array_trace.c's own. */
static void boot_and_wake(void) {
    nut_boot();
    assert(regPC == 0);
    int ret = 0;
    for (int batch = 0; batch < 2000; batch++) {
        ret = executeNUT(1000);
        if (ret != 0 || cptinstr >= 2000000)
            break;
    }
    assert(ret >= 0 && ret <= 3);

    press_key("boot", KEY_ON);
    press_key("boot", KEY_ON);
}

/** @brief Snapshot of everything hp41_hpil_controller.c owns, for before/after diffing. */
typedef struct {
    unsigned char reg[10];
    char flgenb;
} hpil_snapshot_t;

/** @brief Capture the current HP-IL chip state. @param snap Output. */
static void snapshot_hpil(hpil_snapshot_t *snap) {
    assert(snap != NULL);
    memcpy(snap->reg, hpil_reg, sizeof(snap->reg));
    snap->flgenb = flgenb;
}

/**
 * @brief Print any difference between two HP-IL state snapshots.
 * @param tag    Short label for diagnostic output.
 * @param before Snapshot taken before the operation under test.
 * @param after  Snapshot taken after.
 * @return Number of differing fields (0 = no change).
 */
static int diff_hpil(const char *tag, const hpil_snapshot_t *before, const hpil_snapshot_t *after) {
    assert(tag != NULL);
    assert(before != NULL);
    int changes = 0;
    for (int i = 0; i < 10; i++) {
        if (before->reg[i] != after->reg[i]) {
            printf("  [%s] hpil_reg[%d]: 0x%02X -> 0x%02X\n", tag, i, before->reg[i], after->reg[i]);
            changes++;
        }
    }
    if (before->flgenb != after->flgenb) {
        printf("  [%s] flgenb: %d -> %d\n", tag, before->flgenb, after->flgenb);
        changes++;
    }
    if (changes == 0) {
        printf("  [%s] no HP-IL state change\n", tag);
    }
    return changes;
}

/**
 * @brief Drive XEQ ALPHA <name> ALPHA [<digit>] through the real ROM -
 *        the universal way to invoke any named FOCAL function
 *        regardless of its normal keyboard shortcut, same idiom
 *        flag_array_trace.c's xeq_3letter_1digit()/xeq_2letter_2digit()
 *        use, generalized to an arbitrary-length name.
 * @param tag        Short label for diagnostic output.
 * @param name_codes Letter keycodes, in order.
 * @param name_len   Number of letters in `name_codes`.
 * @param has_digit  Whether a trailing digit argument follows.
 * @param digit      The trailing digit's keycode, if `has_digit`.
 */
static void xeq_alpha(const char *tag, const unsigned char *name_codes, int name_len, int has_digit,
                       unsigned char digit) {
    assert(tag != NULL);
    assert(name_codes != NULL);
    assert(name_len > 0);
    press_key(tag, KEY_XEQ);
    press_key(tag, KEY_ALPHA);
    press_keys(tag, name_codes, name_len);
    press_key(tag, KEY_ALPHA);
    if (has_digit) {
        press_key(tag, digit);
    }
}

int main(void) {
    hpil_snapshot_t snap_a, snap_b;

    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    /* --- Positive control: prove the diff mechanism itself works
     * before trusting any "no change" result that follows. --- */
    printf("=== positive control: direct hpil_wr(1, 0x01) call ===\n");
    snapshot_hpil(&snap_a);
    hpil_wr(1, 0x01); /* control reg write: sets flgenb */
    snapshot_hpil(&snap_b);
    int control_changes = diff_hpil("control", &snap_a, &snap_b);
    if (control_changes == 0) {
        printf("!!! detector is broken: a direct hpil_wr() call produced no visible diff !!!\n");
        return 1;
    }
    printf("control OK: detector sees real HP-IL state changes.\n\n");

    /* Reset to power-on state before driving the real ROM - the
     * control call above deliberately dirtied hpil_reg[]/flgenb. */
    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    boot_and_wake();
    printf("=== ready state reached ===\n\n");

    snapshot_hpil(&snap_a);
    printf("=== baseline HP-IL state after cold boot + wake ===\n");
    printf("  hpil_reg[0..9] = ");
    for (int i = 0; i < 10; i++)
        printf("%02X ", snap_a.reg[i]);
    printf("| flgenb=%d\n\n", snap_a.flgenb);

    snapshot_hpil(&snap_a);
    printf("=== plain arithmetic (123 ENTER 456 +), negative control ===\n");
    unsigned char arith[] = {KEY_1, KEY_2, KEY_3, KEY_ENTER, KEY_4, KEY_5, KEY_6, KEY_PLUS};
    press_keys("arith", arith, (int)(sizeof(arith) / sizeof(arith[0])));
    snapshot_hpil(&snap_b);
    diff_hpil("arith", &snap_a, &snap_b);
    printf("\n");

    snapshot_hpil(&snap_a);
    printf("=== XEQ CATALOG 1 (functions) ===\n");
    unsigned char catalog[] = {KEY_C, KEY_A, KEY_T, KEY_A, KEY_L, KEY_O, KEY_G};
    xeq_alpha("CAT1", catalog, (int)(sizeof(catalog) / sizeof(catalog[0])), 1, KEY_1);
    snapshot_hpil(&snap_b);
    diff_hpil("CAT1", &snap_a, &snap_b);
    printf("\n");

    snapshot_hpil(&snap_a);
    printf("=== XEQ CATALOG 2 (programs) ===\n");
    xeq_alpha("CAT2", catalog, (int)(sizeof(catalog) / sizeof(catalog[0])), 1, KEY_2);
    snapshot_hpil(&snap_b);
    diff_hpil("CAT2", &snap_a, &snap_b);
    printf("\n");

    snapshot_hpil(&snap_a);
    printf("=== XEQ CATALOG 3 (extended functions) ===\n");
    xeq_alpha("CAT3", catalog, (int)(sizeof(catalog) / sizeof(catalog[0])), 1, KEY_3);
    snapshot_hpil(&snap_b);
    diff_hpil("CAT3", &snap_a, &snap_b);
    printf("\n");

    snapshot_hpil(&snap_a);
    printf("=== ON, ON again (a second cold-start-style wake cycle) ===\n");
    press_key("ON2", KEY_ON);
    press_key("ON2", KEY_ON);
    snapshot_hpil(&snap_b);
    diff_hpil("ON2", &snap_a, &snap_b);
    printf("\n");

    return 0;
}
