/* Host-side single-step trace of real HP-41 key sequences through
 * emu41gcc's actual ROM, watching the decoded display content (not just
 * PC/status bits) change over time - built to test a specific
 * hypothesis: is the "screen goes blank, catches up next key" bug (see
 * CLAUDE.md) caused by the ROM legitimately writing blank content to
 * lcd_a/b/c before the correct result, with our system freezing (POWOFF)
 * before ever reaching the correct write - or is it something else?
 *
 * Build: make -C tools   (see tools/Makefile), then ./tools/build/powoff_trace
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "nutcpu.h"

#include "nut_rom.h"

#define BATCH_SIZE 1000
#define MAX_INSTR 2000000
#define MAX_BATCHES ((MAX_INSTR / BATCH_SIZE) + 1) /* see tests/nut_smoke_test.c's run_until_settled() */

extern unsigned char lcd_a[12];
extern unsigned char lcd_b[12];
extern unsigned char lcd_c[12];
extern int lcd_ann;

/* Same decode as firmware/hp41_display_bridge.c's hp41_decode_ascii() -
 * kept in sync by hand, see that file's own comment for provenance. */
static int decode_ascii(int v) {
    v &= 0x13f;
    assert(v >= 0 && v <= 0x13f);

    int result;
    if (v <= 0x1f) {
        result = v + '@';
    } else if (v <= 0x3f) {
        if (v == 0x2c)      result = '<';
        else if (v == 0x2e) result = '>';
        else if (v == 0x3a) result = '*';
        else                result = v;
    } else if (v <= 0x105) {
        result = v - 0xa0;
    } else if (v <= 0x11f) {
        switch (v) {
            case 0x106: result = '~';  break;
            case 0x107: result = '\''; break;
            case 0x10c: result = 'u';  break;
            case 0x10d: result = '#';  break;
            case 0x10e: result = 's';  break;
            case 0x10f: result = 'a';  break;
            default:    result = 'x';  break;
        }
    } else {
        result = v - 0x120 + 'a' - 1;
    }

    /* See firmware/hp41_display_bridge.c's hp41_decode_ascii() for why
     * this matters: the result is used as an index-ish value after
     * masking, and this decode's input domain is sparse in practice, not
     * the full range the mask above allows. */
    assert(result >= 0);
    return result;
}

static void render_display(char *out /* [13] */) {
    assert(out != NULL);
    for (int pos = 0; pos < 12; pos++) {
        int i = 11 - pos;
        int v = (lcd_c[i] << 8) | ((lcd_b[i] & 3) << 4) | lcd_a[i];
        out[pos] = (char)(decode_ascii(v) & 0x7f);
    }
    out[12] = '\0';
    assert(out[12] == '\0');
}

static char last_display[13] = "";
static int last_ann = -1;
static char last_rendered_display[13] = "\x01"; /* sentinel: nothing rendered yet */

static void check_display_change(const char *tag) {
    assert(tag != NULL);
    char cur[13];
    render_display(cur);
    assert(strlen(cur) <= 12);
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
    assert(tag != NULL);
    assert(max_steps > 0);
    int steps = 0, ret = 0;
    int render_count = 0;
    for (int iter = 0; iter < max_steps && steps < max_steps; iter++) {
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
    assert(tag != NULL);
    assert(lgkeybuf >= 0 && lgkeybuf < 8); /* keybuffer[]'s real capacity */
    keybuffer[lgkeybuf++] = code;
    flagKey = 0;
    fdsp = 0; /* fresh per wake, matching main.c clearing it after each render */
    regPC = 0;
    run_until_powoff(tag, 2000);
}

int main(void) {
    nut_boot();
    assert(regPC == 0);
    printf("=== boot ===\n");
    int ret = 0;
    for (int batch = 0; batch < MAX_BATCHES; batch++) {
        ret = executeNUT(BATCH_SIZE);
        check_display_change("boot");
        if (ret != 0 || cptinstr >= MAX_INSTR)
            break;
    }
    assert(ret >= 0 && ret <= 3);
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
