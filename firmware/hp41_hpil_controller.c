/**
 * @file hp41_hpil_controller.c
 * @brief Real 1LB3 HP-IL chip register simulation - see
 *        hp41_hpil_controller.h for the full contract this implements
 *        (init_hpil()/hpil_wr()/hpil_rd(), the exact function names
 *        emu41gcc/hpil.h declares and emu41gcc/nutcpu.c calls).
 *
 * Behavior is ported from emu41gcc's own (upstream, excluded-from-
 * this-build - it lives under emu41gcc/ignore/, never compiled here)
 * reference implementation, ignore/hpil.c, register-write/read logic
 * for logic bit-for-bit - only the C shape changed, not the protocol:
 *
 * - No `goto` (Power of 10 Rule 1): the reference's DOE-frame
 *   auto-retransmit ("nobody's listening or talking yet, resend the
 *   same frame") is `goto xfer;` there. Here it's a bounded `for` loop
 *   (HP41_HPIL_MAX_DOE_RETRANSMITS) instead - see hpil_wr()'s own
 *   comment for why a bound this small is already generous for
 *   soynut's actual (single-device) loop wiring.
 * - Doxygen headers + named bit constants replace the reference's
 *   terse inline-literal style, matching this project's own
 *   commenting standard.
 * - hpil_transmit()'s device-table walk (tabdev[]/nbdev, a general
 *   multi-device loop the reference supports) is replaced by a single
 *   fixed call to hp41_hpil_loop_transmit() (defined in
 *   hp41_hpil_video_bridge.c) - soynut only wires one HP-IL peripheral
 *   today. See hp41_hpil_controller.h's own header comment.
 */

#include "hp41_hpil_controller.h"

#include <assert.h>
#include <stdbool.h>

#define GLOBAL
#include "hpil.h"
#undef GLOBAL

#define GLOBAL extern
#include "nutcpu.h"
#undef GLOBAL

/** @name HP-IL status register 1 (hpil_reg[1]) bits
 *  Matches emu41gcc/ignore/hpil.c's own comment: "flags register 1:
 *  4:IFCR, 3:SRQR, 2:FRAV, 1:FRNS, 0:ORAV".
 *  @{ */
#define HPIL_STATUS1_ORAV 0x01u /**< Output Register Available */
#define HPIL_STATUS1_FRNS 0x02u /**< Frame Received, Not Same (as sent) */
#define HPIL_STATUS1_FRAV 0x04u /**< Frame Received, AVailable */
#define HPIL_STATUS1_SRQR 0x08u /**< SRQ bit set in the last received frame */
#define HPIL_STATUS1_IFCR 0x10u /**< Interface Clear (IFC command) seen */
/** @} */

/** @name HP-IL chip control-register-0 (hpil_reg[0]) bits actually read here @{ */
#define HPIL_CTRL0_LA 0x10u /**< Listen Active */
#define HPIL_CTRL0_TA 0x20u /**< Talk Active */
/** @} */

/** Frame-class bit tests (11-bit frame in the low bits of a uint16_t/int),
 *  matching this project's own ilvideo-native/core/hpil_device.h layout
 *  exactly (bits 8-10 select the class) - no translation needed between
 *  what this controller builds and what the video interface expects. */
#define HPIL_FRAME_IS_DOE(f) (((f) & 0x400) == 0)
#define HPIL_FRAME_IS_IDY(f) (((f) & 0x200) != 0)
#define HPIL_FRAME_IS_CMD(f) (((f) & 0x100) == 0) /* only meaningful once DOE/IDY are ruled out */
#define HPIL_FRAME_IFC 0x490 /* the specific CMD frame value for "IFC" */

/** Bounded cap on DOE-frame auto-retransmit attempts (replaces the
 * reference's unconditional `goto xfer;` loop - see this file's own
 * header comment). A DOE frame only retransmits while neither Listen
 * Active nor Talk Active is set on this chip; since soynut's only
 * wired device (the video interface) doesn't itself drive LA/TA
 * (that's the *controller* chip's own state, set by the ROM's own
 * addressing sequence before it ever writes data), this bound is
 * never expected to bind in practice - it exists purely so the loop
 * has a provable upper bound (Power of 10 Rule 2), not because more
 * than one or two retransmits is ever anticipated. */
#define HP41_HPIL_MAX_DOE_RETRANSMITS 8

/**
 * @brief Recompute regFI's HP-IL bits (10 down to 6) from hpil_reg[1].
 *
 * Mirrors the real 1LB3's own flag-enable behavior: the HP-IL status
 * bits only actually reach the CPU's pollable input-flags register
 * while flgenb is set (via a control-register write with bit 0 set) -
 * otherwise the ROM's `?Fx=1` polling simply never sees them, exactly
 * as if no HP-IL module were plugged in.
 */
static void update_flags(void) {
    assert(flgenb == 0 || flgenb == 1); /* set only from n&1 in hpil_wr(), a real invariant */
    assert(sizeof(hpil_reg) / sizeof(hpil_reg[0]) == 10); /* confirms we're linked against real emu41gcc/hpil.h storage */

    regFI &= ~(0x1Fu << 6); /* clear bits 6-10 unconditionally, then maybe set them back */
    if (flgenb) {
        if (hpil_reg[1] & HPIL_STATUS1_ORAV) {
            regFI |= 1u << 10;
        }
        if (hpil_reg[1] & HPIL_STATUS1_FRNS) {
            regFI |= 1u << 9;
        }
        if (hpil_reg[1] & HPIL_STATUS1_FRAV) {
            regFI |= 1u << 8;
        }
        if (hpil_reg[1] & HPIL_STATUS1_SRQR) {
            regFI |= 1u << 7;
        }
        if (hpil_reg[1] & HPIL_STATUS1_IFCR) {
            regFI |= 1u << 6;
        }
    }
}

void init_hpil(void) {
    int i;

    for (i = 0; i < 9; ++i) {
        hpil_reg[i] = 0;
    }
    hpil_reg[0] = 0x81; /* SC=1 MCL=1 */
    hpil_reg[1] = HPIL_STATUS1_ORAV;
    flgenb = 0;
    update_flags();
}

void hp41_hpil_controller_init(void) {
    init_hpil();
}

/**
 * @brief Handle a write to HP-IL chip register 2 (the data register) -
 *        the one register write that actually transmits a frame.
 *
 * Split out of hpil_wr() itself only to keep that function under this
 * project's ~60-line guideline; not part of the public contract, so
 * not declared in the header.
 *
 * @param byte The 8-bit value written to register 2.
 */
static void handle_data_register_write(int byte) {
    uint16_t frame;
    int reply = -1;
    int attempt;

    assert(byte >= 0 && byte <= 0xFF);

    hpil_reg[2] = (unsigned char)byte;
    frame = (uint16_t)(byte | ((hpil_reg[8] & 0xE0) << 3)); /* bits 8-10 come from R1W */

    for (attempt = 0; attempt < HP41_HPIL_MAX_DOE_RETRANSMITS; ++attempt) {
        bool retry = false;

        reply = hp41_hpil_loop_transmit(frame);
        hpil_reg[1] &= (unsigned char)~HPIL_STATUS1_FRAV;
        update_flags();
        if (reply < 0) {
            break; /* loop severed - matches reference's "if (n1<0) break;" */
        }

        hpil_reg[2] = (unsigned char)reply;
        hpil_reg[1] &= 0x1Fu;
        hpil_reg[1] |= (unsigned char)((reply & 0x700) >> 3);

        if (HPIL_FRAME_IS_DOE(reply)) {
            if (hpil_reg[0] & HPIL_CTRL0_LA) {
                hpil_reg[1] |= (HPIL_STATUS1_FRAV | HPIL_STATUS1_ORAV);
            } else if (hpil_reg[0] & HPIL_CTRL0_TA) {
                hpil_reg[1] |= ((int)(frame & 0xFF) != (reply & 0xFF)) ? (HPIL_STATUS1_FRNS | HPIL_STATUS1_ORAV)
                                                                        : HPIL_STATUS1_ORAV;
            } else {
                /* neither Listen Active nor Talk Active: nobody has
                 * claimed this DOE frame yet, so it goes back around
                 * the loop unchanged - bounded by the enclosing for
                 * loop rather than the reference's unconditional goto */
                frame = (uint16_t)reply;
                retry = true;
            }
            if (!retry) {
                hpil_reg[1] = (reply & 0x100) ? (unsigned char)(hpil_reg[1] | HPIL_STATUS1_SRQR)
                                               : (unsigned char)(hpil_reg[1] & (unsigned char)~HPIL_STATUS1_SRQR);
            }
        } else if (HPIL_FRAME_IS_IDY(reply)) {
            hpil_reg[1] |= ((hpil_reg[0] & 0x30) == 0x30) ? (HPIL_STATUS1_FRAV | HPIL_STATUS1_ORAV)
                                                            : HPIL_STATUS1_ORAV;
            hpil_reg[1] = (reply & 0x100) ? (unsigned char)(hpil_reg[1] | HPIL_STATUS1_SRQR)
                                           : (unsigned char)(hpil_reg[1] & (unsigned char)~HPIL_STATUS1_SRQR);
        } else if (HPIL_FRAME_IS_CMD(reply)) {
            if ((hpil_reg[0] & 0x30) == 0x30) {
                hpil_reg[1] |= (HPIL_STATUS1_FRAV | HPIL_STATUS1_ORAV);
            } else if ((int)frame == reply) {
                hpil_reg[1] |= HPIL_STATUS1_ORAV;
            } else {
                hpil_reg[1] |= (HPIL_STATUS1_FRNS | HPIL_STATUS1_ORAV);
            }
            if (reply == HPIL_FRAME_IFC) {
                hpil_reg[1] |= HPIL_STATUS1_IFCR;
            }
        } else { /* RDY */
            if ((hpil_reg[0] & 0x30) == 0x30) {
                hpil_reg[1] |= (HPIL_STATUS1_FRAV | HPIL_STATUS1_ORAV);
            } else if ((reply & 0xC0) == 0x40) { /* ARG */
                hpil_reg[1] |= (HPIL_STATUS1_FRAV | HPIL_STATUS1_ORAV);
            } else {
                hpil_reg[1] |= ((int)frame == reply) ? HPIL_STATUS1_ORAV : (HPIL_STATUS1_FRNS | HPIL_STATUS1_ORAV);
            }
        }

        if (retry) {
            continue;
        }
        update_flags();
        break;
    }

    /* hp41_hpil_loop_transmit()'s own contract (hp41_hpil_controller.h)
     * is "a valid 11-bit frame, or negative for a severed loop" -
     * confirms the loop above never mistook a mis-implemented device's
     * out-of-range return value for a real frame. */
    assert(reply < 0 || (reply >= 0 && reply <= 0x7FF));
}

void hpil_wr(int reg, int n) {
    assert(reg >= 0 && reg <= 8);
    assert(n >= 0 && n <= 0xFF);

    switch (reg) {
    case 0: /* status/control register */
        if (n & 1) { /* MCL: master clear */
            hpil_reg[0] |= 0x80;
            hpil_reg[1] &= 0xE1u; /* IFCR=SRQR=FRNS=FRAV=0 */
            hpil_reg[1] |= HPIL_STATUS1_ORAV;
            flgenb = 0;
            update_flags();
        }
        if (n & 2) { /* CLIFCR: clear the interface-clear flag */
            hpil_reg[1] &= (unsigned char)~HPIL_STATUS1_IFCR;
            update_flags();
        }
        hpil_reg[0] = (unsigned char)(n & 0xFD); /* CLIFCR's own bit is self-clearing */
        break;

    case 1: /* control register */
        flgenb = (char)(n & 1);
        hpil_reg[8] = (unsigned char)n; /* R1W */
        update_flags();
        break;

    case 2: /* data register: the actual transmit trigger */
        handle_data_register_write(n);
        break;

    default: /* R3-R7 (and R1W, reg 8, if ever addressed directly) are plain storage */
        hpil_reg[reg] = (unsigned char)n;
        break;
    }
}

int hpil_rd(int reg) {
    int status_bits;

    assert(reg >= 0 && reg < 10);
    assert(sizeof(hpil_reg) / sizeof(hpil_reg[0]) == 10);

    if (reg == 2) {
        status_bits = hpil_reg[1] & (HPIL_STATUS1_FRAV | HPIL_STATUS1_FRNS);
        hpil_reg[1] &= (unsigned char)~(HPIL_STATUS1_FRAV | HPIL_STATUS1_FRNS);
        if (status_bits != 0) {
            hpil_reg[8] = hpil_reg[1]; /* copy R1R to R1W */
        }
        update_flags();
    }
    return hpil_reg[reg];
}
