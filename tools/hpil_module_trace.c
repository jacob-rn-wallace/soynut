/**
 * @file hpil_module_trace.c
 * @brief Host-side diagnostic: with the real HP82160A HP-IL Module ROM
 *        (roms/HPIL.MOD) actually wired into tabpage[6]/tabpage[7] via
 *        nut_rom_wire_hpil_module(), does the calculator now touch
 *        HP-IL state - both through normal key sequences, and by
 *        directly executing the module's own SELPF/CH= code found by
 *        disassembly (page 7, offset 0x01F: `SELPF 1` / `CH= 01`)?
 *
 * tools/hpil_opcode_trace.c already established, empirically, that the
 * base OS alone (no module) never touches HP-IL under a representative
 * sweep of operations - this is the direct follow-up with the module
 * actually present, same "diff hpil_reg[]/flgenb before/after" approach
 * and the same positive-control-first discipline, see that file's own
 * header comment for the full rationale (not repeated here).
 *
 * Requires roms/HPIL.MOD (see roms/README.md's "HP-IL module" section)
 * and the generated roms/rom_images_hpil.c - unlike hpil_opcode_trace,
 * this tool has no "run without it" mode, since testing WITH the
 * module is its entire point.
 *
 * Build: make -C tools   (see tools/Makefile), then
 *        ./tools/build/hpil_module_trace
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
#include "nut_rom_hpil.h"

#define MAX_STEPS_PER_KEY 500000

/**
 * @brief Push one HP-41 keycode and single-step until it's fully
 *        processed - identical to flag_array_trace.c's own press_key().
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

#define KEY_ON 0x18
#define KEY_XEQ 0x32
#define KEY_ALPHA 0xc4
#define KEY_A 0x10
#define KEY_C 0x70
#define KEY_G 0x31
#define KEY_L 0x72
#define KEY_O 0x73
#define KEY_T 0x84
#define KEY_1 0x36

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

int main(void) {
    hpil_snapshot_t snap_a, snap_b;

    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    /* Positive control - see hpil_opcode_trace.c's own header comment
     * for why this runs first. */
    printf("=== positive control: direct hpil_wr(1, 0x01) call ===\n");
    snapshot_hpil(&snap_a);
    hpil_wr(1, 0x01);
    snapshot_hpil(&snap_b);
    if (diff_hpil("control", &snap_a, &snap_b) == 0) {
        printf("!!! detector is broken: a direct hpil_wr() call produced no visible diff !!!\n");
        return 1;
    }
    printf("control OK.\n\n");
    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    /* --- The actual test: wire in the real module before booting. --- */
    printf("=== nut_rom_wire_hpil_module(): HP-IL module ROM now present at pages 6-7 ===\n");
    nut_rom_wire_hpil_module();

    snapshot_hpil(&snap_a);
    boot_and_wake();
    printf("=== ready state reached ===\n\n");
    snapshot_hpil(&snap_b);
    printf("=== cold boot + wake, module present ===\n");
    diff_hpil("boot", &snap_a, &snap_b);
    printf("\n");

    snapshot_hpil(&snap_a);
    printf("=== XEQ CATALOG 1, module present ===\n");
    unsigned char catalog[] = {KEY_C, KEY_A, KEY_T, KEY_A, KEY_L, KEY_O, KEY_G};
    press_key("CAT1", KEY_XEQ);
    press_key("CAT1", KEY_ALPHA);
    press_keys("CAT1", catalog, (int)(sizeof(catalog) / sizeof(catalog[0])));
    press_key("CAT1", KEY_ALPHA);
    press_key("CAT1", KEY_1);
    snapshot_hpil(&snap_b);
    diff_hpil("CAT1", &snap_a, &snap_b);
    printf("\n");

    /* --- Direct execution test: jump straight to the module's own
     * SELPF/CH= code found by disassembly (page 7, offset 0x01F) and
     * single-step through it, bypassing key-sequence guessing
     * entirely. This is the most direct possible confirmation that
     * hp41_hpil_controller.c correctly handles a real ROM's real
     * opcode stream, not just synthetic frames. --- */
    printf("=== direct execution at page7:0x01F (real module SELPF/CH= code) ===\n");
    snapshot_hpil(&snap_a);
    regPC = 0x701F; /* page 7 (top 4 bits), offset 0x01F */
    flagKey = 0;
    for (int i = 0; i < 20; i++) {
        int ret = executeNUT(1);
        if (ret == 2) {
            printf("  invalid opcode at PC=0x%04X after %d steps\n", regPC, i);
            break;
        }
        if (ret == 1) {
            printf("  POWOFF at PC=0x%04X after %d steps\n", regPC, i);
            break;
        }
    }
    snapshot_hpil(&snap_b);
    diff_hpil("direct-exec", &snap_a, &snap_b);
    printf("  hpil_reg[0..9] after = ");
    for (int i = 0; i < 10; i++)
        printf("%02X ", snap_b.reg[i]);
    printf("| flgenb=%d\n", snap_b.flgenb);

    return 0;
}
