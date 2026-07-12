/* Host-side single-step trace of real HP-41 key sequences through
 * emu41gcc's actual ROM, watching the decoded display content (not just
 * PC/status bits) change over time - built to test a specific
 * hypothesis: is the "screen goes blank, catches up next key" bug (see
 * CLAUDE.md) caused by the ROM legitimately writing blank content to
 * lcd_a/b/c before the correct result, with our system freezing (POWOFF)
 * before ever reaching the correct write - or is it something else?
 *
 * Build (from repo root):
 *   cc -std=gnu11 -fcommon -Iemu41gcc -Ifirmware/emu41gcc_compat \
 *      -include firmware/emu41gcc_compat/nut_stubs.h \
 *      -o tools/build/powoff_trace tools/powoff_trace.c \
 *      emu41gcc/nutcpu.c emu41gcc/display.c \
 *      firmware/emu41gcc_compat/nut_stubs.c \
 *      firmware/emu41gcc_compat/nut_globals.c \
 *      firmware/emu41gcc_compat/nut_rom.c \
 *      roms/rom_images.c
 *   tools/build/powoff_trace
 */
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "nut_rom.h"

extern unsigned char lcd_a[12];
extern unsigned char lcd_b[12];
extern unsigned char lcd_c[12];
extern int lcd_ann;

/* Same decode as firmware/hp41_display_bridge.c's hp41_decode_ascii() -
 * kept in sync by hand, see that file's own comment for provenance. */
static int decode_ascii(int v) {
    v &= 0x13f;
    if (v <= 0x1f) return v + '@';
    if (v <= 0x3f) {
        if (v == 0x2c) return '<';
        if (v == 0x2e) return '>';
        if (v == 0x3a) return '*';
        return v;
    }
    if (v <= 0x105) return v - 0xa0;
    if (v <= 0x11f) {
        switch (v) {
            case 0x106: return '~';
            case 0x107: return '\'';
            case 0x10c: return 'u';
            case 0x10d: return '#';
            case 0x10e: return 's';
            case 0x10f: return 'a';
            default:    return 'x';
        }
    }
    return v - 0x120 + 'a' - 1;
}

static void render_display(char *out /* [13] */) {
    for (int pos = 0; pos < 12; pos++) {
        int i = 11 - pos;
        int v = (lcd_c[i] << 8) | ((lcd_b[i] & 3) << 4) | lcd_a[i];
        out[pos] = (char)(decode_ascii(v) & 0x7f);
    }
    out[12] = '\0';
}

static char last_display[13] = "";
static int last_ann = -1;
static char last_rendered_display[13] = "\x01"; /* sentinel: nothing rendered yet */

static void check_display_change(const char *tag) {
    char cur[13];
    render_display(cur);
    if (strcmp(cur, last_display) != 0 || lcd_ann != last_ann) {
        printf("  [%s] instr=%d PC=0x%04X dspon=%d display=\"%s\" ann=0x%03X\n",
               tag, cptinstr, regPC, dspon, cur, lcd_ann);
        strcpy(last_display, cur);
        last_ann = lcd_ann;
    }
}

/* Runs until POWOFF/invalid/2000 steps, printing every time the decoded
 * display content actually changes (not every instruction - that alone
 * tells us whether blank content is really being written by the ROM, or
 * whether we're just freezing before a later, correct write happens). */
static int run_until_powoff(const char *tag, int max_steps) {
    int steps = 0, ret = 0;
    int render_count = 0;
    while (steps < max_steps) {
        int fdsp_before = fdsp;
        ret = executeNUT(1);
        steps++;
        check_display_change(tag);
        if (!fdsp_before && fdsp) {
            /* Exactly what main.c does: render on the fdsp transition, then
             * clear it - each one is a separate "frame main.c would have
             * sent to the Arduino" event. */
            char cur[13];
            render_display(cur);
            render_count++;
            printf("  [%s] *** fdsp frame #%d at instr=%d: display=\"%s\" ann=0x%03X\n",
                   tag, render_count, cptinstr, cur, lcd_ann);
            strcpy(last_rendered_display, cur);
            fdsp = 0;
        }
        if (ret == 1) {
            char cur[13];
            render_display(cur);
            printf("  [%s] >>> POWOFF at instr=%d, PC=0x%04X, Carry=%d, dspon=%d - "
                   "TRUE final state=\"%s\" ann=0x%03X (%s)\n",
                   tag, cptinstr, regPC, Carry, dspon, cur, lcd_ann,
                   strcmp(cur, last_rendered_display) == 0
                       ? "matches the last fdsp frame above - main.c would show this correctly"
                       : "DIFFERENT from the last fdsp frame above - main.c would miss this, showing stale content instead");
            return ret;
        }
        if (ret == 2) {
            printf("  [%s] >>> INVALID OPCODE at PC=0x%04X\n", tag, regPC);
            return ret;
        }
    }
    printf("  [%s] >>> gave up after %d steps (PC=0x%04X)\n", tag, steps, regPC);
    return ret;
}

static void wake_with_key(const char *tag, unsigned char code) {
    keybuffer[lgkeybuf++] = code;
    flagKey = 0;
    fdsp = 0; /* fresh per wake, matching main.c clearing it after each render */
    regPC = 0;
    run_until_powoff(tag, 2000);
}

int main(void) {
    nut_boot();
    printf("=== boot ===\n");
    int ret;
    do {
        ret = executeNUT(1000);
        check_display_change("boot");
    } while (ret == 0 && cptinstr < 2000000);
    printf("  boot: ret=%d PC=0x%04X instr=%d Carry=%d\n\n", ret, regPC, cptinstr, Carry);

    /* Reproduces the user's exact reported sequence: cold start -> ON,
     * ON (reach the real "0.0000" ready state) -> '2' (expect "2_") ->
     * ENTER (expect "2.0000", reported as blank instead) -> '2' again
     * (expect "2_") -> '+' (expect "4.0000", reported as blank) ->
     * SHIFT (reported as when "4.0000" finally appears). */
    printf("=== wake: ON #1 ===\n");
    wake_with_key("ON#1", 0x18);

    printf("=== wake: ON #2 ===\n");
    wake_with_key("ON#2", 0x18);

    printf("=== wake: '2' ===\n");
    wake_with_key("2", 0x76);

    printf("=== wake: ENTER ===\n");
    wake_with_key("ENTER", 0x13);

    printf("=== wake: '2' again ===\n");
    wake_with_key("2b", 0x76);

    printf("=== wake: '+' ===\n");
    wake_with_key("+", 0x15);

    printf("=== wake: SHIFT ===\n");
    wake_with_key("SHIFT", 0x12);

    return 0;
}
