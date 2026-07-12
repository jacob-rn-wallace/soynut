/* Native (host) smoke test for the Nut CPU core wiring.
 *
 * Not part of the firmware - builds and runs on the dev machine with the
 * system compiler, no Pico/ARM toolchain needed. Boots the real HP-41CV
 * OS ROM (roms/rom_images.c) against emu41gcc's nutcpu.c/display.c and
 * runs it for a bounded number of instructions, watching for the ROM
 * ever executing an invalid opcode (executeNUT() returning 2) - the
 * simplest concrete evidence that tabpage[]/typmod[] are wired
 * correctly and the CPU is actually decoding real HP-41 code, not
 * garbage.
 *
 * Build: make -C tests   (see tests/Makefile and CLAUDE.md's "Native
 * (host) tests" section for what it does and why).
 */

#include <assert.h>
#include <stdio.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "display.h"
#include "nut_rom.h"

#define BATCH_SIZE 1000
#define MAX_INSTR 2000000 /* generous ceiling for a bring-up smoke test */
/* Power of 10, Rule 2: the ceiling on total instructions (MAX_INSTR) is
 * a runtime quantity (cptinstr), not a loop-iteration count - a fixed
 * cap on the number of batches makes the loop below provably bounded
 * regardless of how cptinstr behaves. */
#define MAX_BATCHES ((MAX_INSTR / BATCH_SIZE) + 1)

/* Runs the ROM in fixed-size batches until it stops advancing (POWOFF,
 * invalid opcode, breakpoint, or display-dirty) or MAX_INSTR total
 * instructions have run. Returns executeNUT()'s last status code. */
static int run_until_settled(void)
{
    int ret = 0;

    assert(cptinstr == 0); /* only ever called once, right after nut_boot() */
    for (int batch = 0; batch < MAX_BATCHES; batch++) {
        ret = executeNUT(BATCH_SIZE);
        if (ret != 0 || cptinstr >= MAX_INSTR)
            break;
    }
    assert(cptinstr <= MAX_INSTR + BATCH_SIZE);
    assert(ret >= 0 && ret <= 3); /* executeNUT()'s only documented return values */
    return ret;
}

int main(void)
{
    char dispbuf[32];
    char annbuf[40];

    nut_boot();
    assert(regPC == 0); /* nut_boot()'s documented cold-start value */
    assert(cptinstr == 0);

    const int ret = run_until_settled();
    assert(ret >= 0 && ret <= 3);

    printf("executeNUT stopped: ret=%d (0=OK/limit 1=POWOFF 2=INVALID 3=BREAK)\n", ret);
    printf("instructions executed: %d\n", cptinstr);
    printf("final regPC: 0x%04X (page %d, offset 0x%03X)\n",
           regPC, regPC >> 12, regPC & 0xfff);
    printf("display: \"%s\"\n", display_to_buf(dispbuf));
    printf("annunciators: \"%s\"\n", ann_to_buf(annbuf));

    if (ret == 2) {
        printf("FAIL: hit an invalid opcode - ROM wiring or CPU core is broken\n");
        return 1;
    }

    printf("PASS: no invalid opcode in %d instructions\n", cptinstr);
    return 0;
}
