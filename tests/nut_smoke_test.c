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
 * Build (see CLAUDE.md for the exact command, run from repo root):
 *   cc -std=gnu11 -fcommon -Iemu41gcc -Ifirmware/emu41gcc_compat \
 *      -include firmware/emu41gcc_compat/nut_stubs.h \
 *      -o tests/build/nut_smoke_test tests/nut_smoke_test.c \
 *      emu41gcc/nutcpu.c emu41gcc/display.c \
 *      firmware/emu41gcc_compat/nut_stubs.c \
 *      firmware/emu41gcc_compat/nut_globals.c \
 *      firmware/emu41gcc_compat/nut_rom.c \
 *      roms/rom_images.c
 */

#include <stdio.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "display.h"
#include "nut_rom.h"

int main(void)
{
  char dispbuf[32];
  char annbuf[40];
  int ret;
  const int max_instr = 2000000; /* generous ceiling for a bring-up smoke test */

  nut_boot();

  /* cptinstr is a global, cumulative across calls - executeNUT(n) runs up
   * to n instructions per call, stopping early on POWOFF/invalid/
   * breakpoint/display-dirty (fdsp), so looping and re-checking cptinstr
   * is the right way to run "up to max_instr total". */
  do {
    ret = executeNUT(1000);
  } while (ret == 0 && cptinstr < max_instr);

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
