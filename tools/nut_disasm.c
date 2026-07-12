/* Minimal standalone disassembler harness using emu41gcc's own desas41.c
 * (unmodified) against our ROM images, to inspect real ROM code around
 * addresses of interest without guessing from instruction counts alone.
 *
 * Stubs: ch_label() (symbol lookup - needs external .ADD/.IDX label
 * files from trans.c we don't have; just returns "not found") and
 * char41() (used only for CON string disassembly; copied verbatim from
 * monit.c, it's tiny and self-contained).
 *
 * Build: make -C tools   (see tools/Makefile)
 *
 * Usage: tools/build/nut_disasm <start_addr_hex> <num_instructions>
 *   e.g. tools/build/nut_disasm 180 20   (disassemble 20 instrs from 0x180)
 *
 * desas_proto.h force-includes a prototype for char41() - desas41.c
 * calls it without any header declaring it (upstream relied on
 * implicit-int declarations), same "compat shim, don't touch the
 * vendored file" pattern as firmware/emu41gcc_compat/.
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

extern const uint16_t rom_nut0[4096];
extern const uint16_t rom_nut1[4096];
extern const uint16_t rom_nut2[4096];

/* Power of 10, Rule 5 note: both parameters are unconditionally
 * ignored - there's no real symbol table to validate against (see the
 * file header comment), so there's no precondition or postcondition
 * beyond "always returns 0" to check. */
int ch_label(long adr, char *s) {
    (void)adr; (void)s;
    return 0;
}

char char41(int v) {
    char c;
    v &= 0x13f;
    assert(v >= 0 && v <= 0x13f);
    if (v <= 0x1f) c = v + '@';
    else if (v <= 0x3f) c = v;
    else if (v < 0x100) c = '.';
    else if (v <= 0x105) c = v - 0xa0;
    else if (v <= 0x10f) c = '*';
    else c = '.';
    assert(c != '\0'); /* every branch above produces a printable char or '.'/'*' */
    return c;
}

extern int desas(int adr, int *codes, char *ligne);

static int fetch_word(int addr) {
    int page = (addr >> 12) & 0xF;
    int off = addr & 0xFFF;
    assert(page >= 0 && page <= 15);  /* tabpage[]-style 4-bit page field */
    assert(off >= 0 && off < 4096);   /* matches rom_nut{0,1,2}[4096]'s size */
    switch (page) {
        case 0: return rom_nut0[off];
        case 1: return rom_nut1[off];
        case 2: return rom_nut2[off];
        default: return 0;
    }
}

/* Generous but real cap - the wired ROM set is 3 pages of 4096 words
 * each; nothing sensible ever needs more instructions dumped in one run
 * than that, and without a cap here a mistyped/huge count argument
 * would turn a diagnostic tool into an unbounded loop (Power of 10,
 * Rule 2). */
#define MAX_DISASM_COUNT (3 * 4096)

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <start_addr_hex> <num_instructions>\n", argv[0]);
        return 1;
    }
    int addr = (int)strtol(argv[1], NULL, 16);
    int count = atoi(argv[2]);
    if (count <= 0 || count > MAX_DISASM_COUNT) {
        fprintf(stderr, "count must be between 1 and %d (got %d)\n", MAX_DISASM_COUNT, count);
        return 1;
    }

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
