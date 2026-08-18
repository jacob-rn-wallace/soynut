/**
 * @file flag_array_trace.c
 * @brief Host-side diagnostic: find where the HP-41's 56-entry FOCAL
 *        flag array (flags 0-55, including the FIX/SCI/ENG display-mode
 *        flags 40/41 and the 36-39 digit-count field) lives in
 *        `espaceRAM`, by driving real key sequences through the real
 *        ROM and diffing raw `espaceRAM` bytes before/after.
 *
 * Same technique already used successfully in this project to find the
 * stack registers (0-3), the ALPHA echo register (5), and the ALPHA/
 * SHIFT mode bits (register 14) - see CLAUDE.md's "Elite User Mode"
 * section. This is the Phase 2a diagnostic from the Magellan/QUAD plan
 * (`/Users/jake/.claude/plans/gentle-mapping-dewdrop.md`): FIX/SCI/ENG
 * formatting rules are publicly documented (HP-41C/CV Owner's Handbook),
 * but *where* those mode bits live in this emulator's RAM was unknown
 * until this diagnostic finds it.
 *
 * Build: make -C tools   (see tools/Makefile), then
 *        ./tools/build/flag_array_trace
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "nut_rom.h"

#define ESPACE_RAM_SIZE 8200
#define MAX_STEPS_PER_KEY 500000

/**
 * @brief Push one HP-41 keycode and single-step until it's fully
 *        processed (a fixed, generous step budget - Power of 10, Rule 2).
 *
 * Same wake pattern as powoff_trace.c's wake_with_key(): the real HP-41
 * (and this emulator, faithfully) goes back to its low-power POWOFF
 * state after processing each keystroke, waiting for the next one -
 * that's not an error, it's exactly what main.c's own "asleep" main-loop
 * branch handles every real keypress (regPC=0/flagKey=0 on each new
 * key). So POWOFF ending this key's run is the expected, successful
 * outcome; only exhausting the step budget without it is worth flagging.
 *
 * @param tag  Short label for diagnostic output.
 * @param code HP-41 keycode to push (see firmware/hp41_key_bridge.c's
 *             tabcode[]/named_keys[] for how these were sourced).
 */
static void press_key(const char *tag, unsigned char code) {
    assert(tag != NULL);
    assert(lgkeybuf >= 0 && lgkeybuf < 8); /* keybuffer[]'s real capacity */
    keybuffer[lgkeybuf++] = code;
    flagKey = 0;
    regPC = 0; /* wake vector - same reset main.c does on every real keypress */
    for (int i = 0; i < MAX_STEPS_PER_KEY; i++) {
        int ret = executeNUT(1);
        if (ret == 1)
            return; /* POWOFF: this key finished processing, as expected */
        if (ret == 2) {
            printf("  [%s] unexpected INVALID OPCODE after key 0x%02X at PC=0x%04X\n",
                   tag, code, regPC);
            return;
        }
    }
    printf("  [%s] key 0x%02X never reached POWOFF within %d steps\n",
           tag, code, MAX_STEPS_PER_KEY);
}

/**
 * @brief Press a sequence of keys in order (see press_key()).
 * @param tag   Short label for diagnostic output.
 * @param codes Keycodes to press, in order.
 * @param n     Number of codes in `codes`.
 */
static void press_keys(const char *tag, const unsigned char *codes, int n) {
    assert(tag != NULL);
    assert(codes != NULL);
    assert(n > 0);
    for (int i = 0; i < n; i++)
        press_key(tag, codes[i]);
}

/**
 * @brief Print every espaceRAM byte that differs between two snapshots.
 * @param tag    Short label for diagnostic output.
 * @param before Snapshot taken before the operation under test.
 * @param after  Snapshot taken after.
 */
static void diff_espace_ram(const char *tag, const unsigned char *before,
                             const unsigned char *after) {
    assert(tag != NULL);
    assert(before != NULL);
    assert(after != NULL);
    int changes = 0;
    for (int i = 0; i < ESPACE_RAM_SIZE; i++) {
        if (before[i] != after[i]) {
            int reg = i / 8;
            int byte_in_reg = i % 8;
            printf("  [%s] espaceRAM[%d] (reg %d, byte %d): 0x%02X -> 0x%02X\n",
                   tag, i, reg, byte_in_reg, before[i], after[i]);
            changes++;
        }
    }
    if (changes == 0)
        printf("  [%s] no espaceRAM change\n", tag);
}

/* HP-41 keycodes, sourced from firmware/hp41_key_bridge.c's tabcode[]/
 * named_keys[] tables (themselves sourced from emu41gcc's own
 * traite_touche()) - see that file's comments for provenance. */
#define KEY_ON    0x18
#define KEY_XEQ   0x32
#define KEY_ALPHA 0xc4
#define KEY_F 0x11
#define KEY_I 0x81
#define KEY_X 0x85
#define KEY_S 0x74
#define KEY_C 0x70
#define KEY_E 0xc0
#define KEY_N 0x13
#define KEY_G 0x31
#define KEY_0 0x37
#define KEY_1 0x36
#define KEY_2 0x76
#define KEY_3 0x86
#define KEY_4 0x35
#define KEY_5 0x75
#define KEY_6 0x85
#define KEY_7 0x34
#define KEY_8 0x74
#define KEY_9 0x84

/**
 * @brief Boot the ROM and wake it to the normal "ready" state.
 */
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

    press_key("boot", KEY_ON); /* first ON: reach "MEMORY LOST" cold-start state */
    press_key("boot", KEY_ON); /* second ON: reach the real ready state */
}

/**
 * @brief Drive XEQ ALPHA <3-letter name> ALPHA <digit>, e.g. "XEQ FIX 4".
 * @param tag    Short label for diagnostic output.
 * @param letter1 First letter's keycode.
 * @param letter2 Second letter's keycode.
 * @param letter3 Third letter's keycode.
 * @param digit  Trailing single-digit argument's keycode.
 */
static void xeq_3letter_1digit(const char *tag, unsigned char letter1,
                                unsigned char letter2, unsigned char letter3,
                                unsigned char digit) {
    assert(tag != NULL);
    unsigned char codes[] = {KEY_XEQ, KEY_ALPHA, letter1, letter2, letter3, KEY_ALPHA, digit};
    press_keys(tag, codes, (int)(sizeof(codes) / sizeof(codes[0])));
}

/**
 * @brief Drive XEQ ALPHA <2-letter name> ALPHA <2-digit argument>, e.g. "XEQ SF 40".
 * @param tag    Short label for diagnostic output.
 * @param letter1 First letter's keycode.
 * @param letter2 Second letter's keycode.
 * @param digit_tens  Argument's tens-digit keycode.
 * @param digit_units Argument's units-digit keycode.
 */
static void xeq_2letter_2digit(const char *tag, unsigned char letter1,
                                unsigned char letter2, unsigned char digit_tens,
                                unsigned char digit_units) {
    assert(tag != NULL);
    unsigned char codes[] = {KEY_XEQ, KEY_ALPHA, letter1, letter2, KEY_ALPHA,
                              digit_tens, digit_units};
    press_keys(tag, codes, (int)(sizeof(codes) / sizeof(codes[0])));
}

/**
 * @brief Boot, wake, then drive FIX/SCI/ENG and SF/CF 40/41 sequences,
 *        diffing espaceRAM around each to find the flag array's location.
 * @return Always 0.
 */
int main(void) {
    boot_and_wake();
    printf("=== ready state reached ===\n\n");

    static unsigned char snap_a[ESPACE_RAM_SIZE];
    static unsigned char snap_b[ESPACE_RAM_SIZE];

    memcpy(snap_a, espaceRAM, ESPACE_RAM_SIZE);
    printf("=== XEQ FIX 4 ===\n");
    xeq_3letter_1digit("FIX4", KEY_F, KEY_I, KEY_X, KEY_4);
    memcpy(snap_b, espaceRAM, ESPACE_RAM_SIZE);
    diff_espace_ram("FIX4", snap_a, snap_b);
    printf("\n");

    memcpy(snap_a, espaceRAM, ESPACE_RAM_SIZE);
    printf("=== XEQ FIX 6 (change digit count only) ===\n");
    xeq_3letter_1digit("FIX6", KEY_F, KEY_I, KEY_X, KEY_6);
    memcpy(snap_b, espaceRAM, ESPACE_RAM_SIZE);
    diff_espace_ram("FIX6", snap_a, snap_b);
    printf("\n");

    memcpy(snap_a, espaceRAM, ESPACE_RAM_SIZE);
    printf("=== XEQ SCI 2 ===\n");
    xeq_3letter_1digit("SCI2", KEY_S, KEY_C, KEY_I, KEY_2);
    memcpy(snap_b, espaceRAM, ESPACE_RAM_SIZE);
    diff_espace_ram("SCI2", snap_a, snap_b);
    printf("\n");

    memcpy(snap_a, espaceRAM, ESPACE_RAM_SIZE);
    printf("=== XEQ ENG 3 ===\n");
    xeq_3letter_1digit("ENG3", KEY_E, KEY_N, KEY_G, KEY_3);
    memcpy(snap_b, espaceRAM, ESPACE_RAM_SIZE);
    diff_espace_ram("ENG3", snap_a, snap_b);
    printf("\n");

    memcpy(snap_a, espaceRAM, ESPACE_RAM_SIZE);
    printf("=== XEQ SF 40 ===\n");
    xeq_2letter_2digit("SF40", KEY_S, KEY_F, KEY_4, KEY_0);
    memcpy(snap_b, espaceRAM, ESPACE_RAM_SIZE);
    diff_espace_ram("SF40", snap_a, snap_b);
    printf("\n");

    memcpy(snap_a, espaceRAM, ESPACE_RAM_SIZE);
    printf("=== XEQ CF 40 ===\n");
    xeq_2letter_2digit("CF40", KEY_C, KEY_F, KEY_4, KEY_0);
    memcpy(snap_b, espaceRAM, ESPACE_RAM_SIZE);
    diff_espace_ram("CF40", snap_a, snap_b);
    printf("\n");

    /* Confirmation sweep: same "FIX" letters every time (no ALPHA-typing
     * noise from varying which letters are pressed), only the trailing
     * digit changes 0-9 - isolates exactly which byte/nibble tracks the
     * digit-count argument, decisively (not just plausibly) confirming
     * or refuting the espaceRAM[114]/[115] candidates found above. */
    printf("=== FIX 0..9 sweep (confirming espaceRAM[114]/[115]) ===\n");
    unsigned char fix_digits[] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
                                   KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    for (int n = 0; n < 10; n++) {
        xeq_3letter_1digit("FIXsweep", KEY_F, KEY_I, KEY_X, fix_digits[n]);
        printf("  [FIXsweep] after FIX %d: espaceRAM[114]=0x%02X espaceRAM[115]=0x%02X\n",
               n, espaceRAM[114], espaceRAM[115]);
    }
    printf("\n");

    return 0;
}
