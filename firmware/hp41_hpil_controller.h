/**
 * @file hp41_hpil_controller.h
 * @brief Real HP-IL (1LB3 chip) register simulation, replacing the
 *        no-op init_hpil()/hpil_wr()/hpil_rd() stubs that used to live
 *        in emu41gcc_compat/nut_stubs.c.
 *
 * emu41gcc/nutcpu.c's execp() (the smart-peripheral opcode dispatcher,
 * entered after a SELPF opcode) and its standalone `HPIL=C n` opcode
 * both call straight into init_hpil()/hpil_wr()/hpil_rd() - functions
 * declared in emu41gcc/hpil.h, which nutcpu.c includes directly. This
 * file provides the real bodies: the same 1LB3 register-level protocol
 * emu41gcc's own (excluded-from-the-build) ignore/hpil.c reference
 * implements, ported to this project's own coding standard rather than
 * copied verbatim - see hp41_hpil_controller.c's own header comment
 * for exactly what changed and why (the biggest change: no `goto`,
 * per this project's Power of 10 Rule 1 - the reference's DOE-frame
 * auto-retransmit is a bounded loop here instead).
 *
 * Frame transmission (writing HP-IL chip register 2) is fully
 * synchronous and interrupt-free, exactly matching real Nut CPU
 * behavior: hpil_wr(2, byte) builds an 11-bit frame and hands it to
 * whatever's on the HP-IL loop via hp41_hpil_loop_transmit() (declared
 * here, defined in hp41_hpil_video_bridge.c - the actual device this
 * project wires onto the loop), gets a same-call-stack answer back,
 * and updates emu41gcc/nutcpu.h's regFI bits the ROM polls with
 * `?Fx=1` - no timers, no async callback, no separate thread/core
 * needed on the Nut CPU side.
 */
#ifndef SOYNUT_HP41_HPIL_CONTROLLER_H
#define SOYNUT_HP41_HPIL_CONTROLLER_H

#include <stdint.h>

/**
 * @brief Reset the HP-IL controller to its power-on state.
 *
 * Wraps the real init_hpil() (which emu41gcc/hpil.h declares and
 * nutcpu.c's execp()/loader-equivalent boot path expects to exist) so
 * main.c's own boot sequence has an obviously-named entry point to
 * call, matching the pattern every other bridge module here uses
 * (hp41_key_bridge_reset(), etc.) rather than requiring main.c to know
 * the vendored function name directly.
 */
void hp41_hpil_controller_init(void);

/**
 * @brief Hand one HP-IL frame to whatever's on the loop.
 *
 * Declared here, defined in hp41_hpil_video_bridge.c - the HP-IL
 * register simulation in hp41_hpil_controller.c is deliberately
 * loop-topology-agnostic (it doesn't know or care what devices exist),
 * matching how emu41gcc's own reference hpil_transmit() walks a
 * device table it doesn't otherwise understand. soynut only ever
 * wires exactly one device onto the loop today (the video interface),
 * so this is a single fixed function call rather than a real
 * multi-device chain-walk - see hp41_hpil_video_bridge.h's own header
 * comment.
 *
 * @param frame The 11-bit HP-IL frame (in the low bits of a uint16_t).
 * @return The (possibly transformed) frame after every device on the
 *         loop has seen it, or a negative value if the loop is
 *         severed and the frame never returns (soynut's current
 *         single-device wiring never actually produces this, but the
 *         contract exists to match emu41gcc's own hpil_transmit()).
 */
int hp41_hpil_loop_transmit(uint16_t frame);

#endif /* SOYNUT_HP41_HPIL_CONTROLLER_H */
