/* Minimal standalone disassembler harness using emu41gcc's own desas41.c
 * (unmodified) against our ROM images, to inspect real ROM code around
 * addresses of interest without guessing from instruction counts alone.
 *
 * Stubs: ch_label() (symbol lookup - needs external .ADD/.IDX label
 * files from trans.c we don't have; just returns "not found") and
 * char41() (used only for CON string disassembly; copied verbatim from
 * monit.c, it's tiny and self-contained).
 *
 * Build (from repo root):
 *   cc -std=gnu11 -include tools/desas_proto.h \
 *      -o tools/build/nut_disasm tools/nut_disasm.c \
 *      emu41gcc/desas41.c roms/rom_images.c
 *
 * Usage: tools/build/nut_disasm <start_addr_hex> <num_instructions>
 *   e.g. tools/build/nut_disasm 180 20   (disassemble 20 instrs from 0x180)
 *
 * desas_proto.h force-includes a prototype for char41() - desas41.c
 * calls it without any header declaring it (upstream relied on
 * implicit-int declarations), same "compat shim, don't touch the
 * vendored file" pattern as firmware/emu41gcc_compat/.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

extern const uint16_t rom_nut0[4096];
extern const uint16_t rom_nut1[4096];
extern const uint16_t rom_nut2[4096];

int ch_label(long adr, char *s) {
    (void)adr; (void)s;
    return 0;
}

char char41(int v) {
    char c;
    v &= 0x13f;
    if (v <= 0x1f) c = v + '@';
    else if (v <= 0x3f) c = v;
    else if (v < 0x100) c = '.';
    else if (v <= 0x105) c = v - 0xa0;
    else if (v <= 0x10f) c = '*';
    else c = '.';
    return c;
}

extern int desas(int adr, int *codes, char *ligne);

static int fetch_word(int addr) {
    int page = (addr >> 12) & 0xF;
    int off = addr & 0xFFF;
    switch (page) {
        case 0: return rom_nut0[off];
        case 1: return rom_nut1[off];
        case 2: return rom_nut2[off];
        default: return 0;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <start_addr_hex> <num_instructions>\n", argv[0]);
        return 1;
    }
    int addr = (int)strtol(argv[1], NULL, 16);
    int count = atoi(argv[2]);

    for (int i = 0; i < count; i++) {
        int codes[2];
        codes[0] = fetch_word(addr);
        codes[1] = fetch_word(addr + 1);
        char ligne[128] = {0};
        int n = desas(addr, codes, ligne);
        printf("%04X: %04X", addr, codes[0]);
        if (n == 2) printf(" %04X", codes[1]);
        else printf("     ");
        printf("  %s\n", ligne);
        addr += n;
    }
    return 0;
}
