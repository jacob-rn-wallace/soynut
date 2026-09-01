/**
 * @file hpil_controller_test.c
 * @brief Native (host) test for hp41_hpil_controller.c/
 *        hp41_hpil_video_bridge.c.
 *
 * Drives hpil_wr()/hpil_rd() directly, exactly as emu41gcc/nutcpu.c's
 * execp() would from a real SELPF/CH=/RDPTRN/HPIL=C opcode sequence,
 * rather than crafting the actual opcode words and booting the ROM -
 * same "poke the state directly, no ROM boot needed" approach
 * elite_display_bridge_test.c/register_decode_test.c already use.
 *
 * Frame construction reminder (see hp41_hpil_controller.c's own header
 * comment): writing control register 1 sets which frame *class* the
 * next data-register (2) write transmits, via bits 5-7 of the written
 * byte becoming bits 8-10 of the 11-bit frame - so a test drives
 * hpil_wr(1, class_byte) once, then one or more hpil_wr(2, data_byte)
 * calls for that class, exactly like a real ROM's own HPIL=C sequence
 * would.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdio.h>

#define GLOBAL extern
#include "hpil.h"

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_hpil_controller.h"
#include "hp41_hpil_video_bridge.h"

/** @brief Report one check's outcome. @return 1 on match, 0 on mismatch. */
static int check_int(const char *label, int got, int want) {
    assert(label != NULL);
    int ok = (got == want);
    printf("%-58s got=%-4d want=%-4d %s\n", label, got, want, ok ? "OK" : "MISMATCH");
    return ok;
}

/** @brief Reset both halves of the HP-IL wiring to their power-on state. */
static void reset_hpil(void) {
    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();
}

/* Frame-class control bytes for hpil_wr(1, ...) - see this file's own
 * header comment for the bit-8-10-from-bits-5-7 derivation.
 * DOE=000, CMD=100, RDY=101 (matches ilvideo-native/core/hpil_device.h's
 * documented frame layout exactly). */
#define CLASS_DOE 0x00
#define CLASS_CMD 0x80
#define CLASS_RDY 0xA0

/** @brief init_hpil()'s documented power-on register values. */
static int test_init_defaults(void) {
    int failures = 0;

    reset_hpil();
    failures += !check_int("init: status reg (R0) == 0x81 (SC=1 MCL=1)", hpil_reg[0], 0x81);
    failures += !check_int("init: status reg 1 (R1) == 1 (ORAV)", hpil_reg[1], 1);
    failures += !check_int("init: flgenb == 0", flgenb, 0);
    failures += !check_int("init: regFI HP-IL bits clear while flgenb==0", (int)(regFI & (0x1F << 6)), 0);
    return failures;
}

/** @brief flgenb gates whether hpil_reg[1] actually reaches regFI. */
static int test_flgenb_gates_regfi(void) {
    int failures = 0;

    reset_hpil();
    /* hpil_reg[1] already has ORAV set from init; regFI must stay clear
     * until a control-register write turns flgenb on. */
    failures += !check_int("pre-enable: regFI HP-IL bits still clear", (int)(regFI & (0x1F << 6)), 0);

    hpil_wr(1, 0x01); /* control reg: flgenb=1, class bits (unused here) 0 */
    failures += !check_int("post-enable: flgenb == 1", flgenb, 1);
    failures += !check_int("post-enable: regFI ORAV bit (10) now set", (int)((regFI >> 10) & 1), 1);

    hpil_wr(0, 0x01); /* status reg: MCL (n&1) clears flgenb again */
    failures += !check_int("post-MCL: flgenb == 0", flgenb, 0);
    failures += !check_int("post-MCL: regFI HP-IL bits clear again", (int)(regFI & (0x1F << 6)), 0);
    return failures;
}

/** @brief CLIFCR (status reg bit 1) clears IFCR without touching MCL's other effects. */
static int test_clifcr(void) {
    int failures = 0;

    reset_hpil();
    hpil_wr(1, CLASS_CMD | 0x01); /* flgenb on (IFCR visible in regFI), CMD class selected */
    hpil_wr(2, 0x90); /* IFC command frame (0x490) sets IFCR */
    failures += !check_int("IFC: IFCR bit (hpil_reg[1] bit4) set", (hpil_reg[1] >> 4) & 1, 1);
    failures += !check_int("IFC: regFI IFCR bit (6) set", (int)((regFI >> 6) & 1), 1);

    hpil_wr(0, 0x02); /* CLIFCR */
    failures += !check_int("CLIFCR: IFCR bit cleared", (hpil_reg[1] >> 4) & 1, 0);
    failures += !check_int("CLIFCR: regFI IFCR bit (6) cleared", (int)((regFI >> 6) & 1), 0);
    return failures;
}

/**
 * @brief End-to-end: address the video interface (AAD, LAD) and stream
 *        data to it exactly as a real ROM's HPIL=C sequence would,
 *        confirming the frames actually reach the video bridge.
 */
static int test_video_end_to_end(void) {
    int failures = 0;

    reset_hpil();

    hpil_wr(1, CLASS_RDY); /* select RDY class */
    hpil_wr(2, 0x80);      /* AAD: offer primary address 0 */

    hpil_wr(1, CLASS_CMD); /* select CMD class */
    hpil_wr(2, 0x20);      /* LAD 0: address the video interface as listener */

    /* Set LA (Listen Active, hpil_reg[0] bit 4) on this chip's own
     * status register before writing data. Without it,
     * hp41_hpil_controller.c's DOE handling treats every reg-2 write
     * as "nobody's claimed this yet" and keeps retransmitting the
     * identical frame (bounded by HP41_HPIL_MAX_DOE_RETRANSMITS) -
     * each retransmit still reaches the video device's InData hook,
     * so an unset LA would write every character several times over
     * and desync the cursor. This mirrors what real ROM HP-IL driver
     * code must do before sending data to an addressed listener - not
     * independently confirmed against real ROM disassembly (no HP-IL
     * driver routine has been traced the way e.g. the display-format
     * espaceRAM bytes were - see CLAUDE.md's own "confirmed
     * empirically" callouts elsewhere), but required for any DOE
     * transfer to behave sanely under the reference 1LB3 semantics
     * hp41_hpil_controller.c ports, so treated as a reasonable,
     * documented assumption for now. */
    hpil_wr(0, 0x10); /* LA=1; n&1=0 and n&2=0, so this doesn't also trigger MCL/CLIFCR */

    hpil_wr(1, CLASS_DOE); /* select DOE class */
    hpil_wr(2, (int)'O');
    hpil_wr(2, (int)'K');

    failures += !check_int("video bridge received 'O' at (0,0)", hp41_hpil_video_bridge_char_at(0, 0), 'O');
    failures += !check_int("video bridge received 'K' at (1,0)", hp41_hpil_video_bridge_char_at(1, 0), 'K');
    failures += !check_int("video bridge dirty flag set after real writes", hp41_hpil_video_bridge_is_dirty(), 1);

    hp41_hpil_video_bridge_clear_dirty();
    failures += !check_int("dirty flag clears on request", hp41_hpil_video_bridge_is_dirty(), 0);
    return failures;
}

/** @brief hpil_rd(2) clears FRAV/FRNS and copies R1R into R1W (hpil_reg[8]). */
static int test_read_clears_status(void) {
    int failures = 0;

    reset_hpil();
    /* A plain AAD reply (no LA/TA, no scope mode, sent != returned since
     * the address gets +1'd) only ever sets ORAV|FRNS in the RDY
     * branch's fallback case, not FRAV - matching hp41_hpil_controller.c's
     * class_rdy-equivalent handling exactly (FRAV means "a full data
     * frame is available to read back", which an address-assignment
     * handshake isn't). LA (set the same way test_video_end_to_end
     * does) puts a DOE write's reply through the branch that actually
     * sets FRAV, so use that scenario here instead of AAD's. */
    hpil_wr(0, 0x10); /* LA=1, see test_video_end_to_end()'s own comment on why */
    hpil_wr(1, CLASS_RDY);
    hpil_wr(2, 0x80); /* AAD - offer address 0 first, so the video device is addressed listener next */
    hpil_wr(1, CLASS_CMD);
    hpil_wr(2, 0x20); /* LAD 0 */
    hpil_wr(1, CLASS_DOE);
    hpil_wr(2, (int)'Z'); /* DOE with LA=1 set: sets FRAV|ORAV, see hp41_hpil_controller.c's DOE branch */
    failures += !check_int("DOE reply with LA set: FRAV bit set before any read", (hpil_reg[1] >> 2) & 1, 1);

    int read_back = hpil_rd(2);
    failures += !check_int("hpil_rd(2) returns the last transmitted frame's low byte", read_back, hpil_reg[2]);
    failures += !check_int("hpil_rd(2) clears FRAV", (hpil_reg[1] >> 2) & 1, 0);
    failures += !check_int("hpil_rd(2) copies R1R into R1W (hpil_reg[8])", hpil_reg[8], hpil_reg[1]);
    return failures;
}

/** @brief Run every HP-IL controller/video-bridge check and report pass/fail. @return 0 on pass, 1 on fail. */
int main(void) {
    int failures = 0;

    failures += test_init_defaults();
    failures += test_flgenb_gates_regfi();
    failures += test_clifcr();
    failures += test_video_end_to_end();
    failures += test_read_clears_status();

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
