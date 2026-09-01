/**
 * @file hpil_video_render_test.c
 * @brief Native (host) test for firmware/hp41_hpil_video_render.c.
 *
 * Same exact-pixel-count approach as quad_display_bridge_test.c/
 * elite_display_bridge_test.c: pokes the video bridge's state directly
 * (via real HP-IL frames through hp41_hpil_controller.c, exactly like
 * hpil_controller_test.c does) and checks the rendered framebuffer's
 * lit-pixel count against an independently-computed expectation.
 *
 * Build: make -C tests
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GLOBAL extern
#include "hpil.h"

#define GLOBAL extern
#include "nutcpu.h"

#include "hp41_hpil_controller.h"
#include "hp41_hpil_video_bridge.h"
#include "hp41_hpil_video_render.h"
#include "hp41_quad_font_table.h"
#include "ilvideo_font.h"
#include "ilvideo_term.h"

#define DISP_WIDTH_BYTES ((HP41_QUAD_DISP_WIDTH_PX + 7) / 8)
#define FB_SIZE ((size_t)DISP_WIDTH_BYTES * HP41_QUAD_DISP_HEIGHT_PX)

static int check_int(const char *label, int got, int want) {
    assert(label != NULL);
    int ok = (got == want);
    printf("%-58s got=%-6d want=%-6d %s\n", label, got, want, ok ? "OK" : "MISMATCH");
    return ok;
}

/** @brief Count 0-bits (dark/lit pixels) in a framebuffer. */
static int count_dark_pixels(const uint8_t *fb) {
    int count = 0;
    size_t i;
    int bit;

    for (i = 0; i < FB_SIZE; ++i) {
        for (bit = 0; bit < 8; ++bit) {
            if ((fb[i] & (0x80 >> bit)) == 0) {
                count++;
            }
        }
    }
    return count;
}

/** @brief Count lit bits in one font glyph, independent of the renderer. */
static int count_glyph_bits(uint8_t code) {
    const uint8_t *rows = ilvideo_font_glyph_rows(code);
    int count = 0;
    int y;
    int x;

    for (y = 0; y < ILVIDEO_FONT_HEIGHT_PX; ++y) {
        for (x = 0; x < ILVIDEO_FONT_WIDTH_PX; ++x) {
            if ((rows[y] & (0x80 >> x)) != 0) {
                count++;
            }
        }
    }
    return count;
}

/* Frame-class control bytes for hpil_wr(1, ...), matching
 * hpil_controller_test.c's own convention exactly. */
#define CLASS_DOE 0x00
#define CLASS_CMD 0x80
#define CLASS_RDY 0xA0

/** @brief Address the video interface and stream `text` to it as real HP-IL frames. */
static void send_text(const char *text) {
    hpil_wr(1, CLASS_RDY);
    hpil_wr(2, 0x80); /* AAD: assign primary address 0 */
    hpil_wr(1, CLASS_CMD);
    hpil_wr(2, 0x20); /* LAD 0: address as listener */
    hpil_wr(0, 0x10); /* LA=1 - see hpil_controller_test.c's own comment on why */
    hpil_wr(1, CLASS_DOE);
    for (; *text != '\0'; ++text) {
        hpil_wr(2, (int)(unsigned char)*text);
    }
}

static int test_blank_screen(void) {
    static uint8_t fb[FB_SIZE];
    int failures = 0;

    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    hp41_hpil_video_render_into(fb);
    failures += !check_int("blank screen: zero dark pixels", count_dark_pixels(fb), 0);
    return failures;
}

static int test_single_char(void) {
    static uint8_t fb[FB_SIZE];
    int failures = 0;

    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    send_text("A");
    hp41_hpil_video_render_into(fb);

    int expected = count_glyph_bits((uint8_t)'A');
    failures += !check_int("single 'A': dark pixel count matches the glyph's own bit count",
                            count_dark_pixels(fb), expected);
    return failures;
}

static int test_two_chars_dont_overlap(void) {
    static uint8_t fb[FB_SIZE];
    int failures = 0;

    hp41_hpil_controller_init();
    hp41_hpil_video_bridge_init();

    send_text("AB");
    hp41_hpil_video_render_into(fb);

    int expected = count_glyph_bits((uint8_t)'A') + count_glyph_bits((uint8_t)'B');
    failures += !check_int("'A'+'B': dark pixel count is the exact sum of both glyphs (no overlap)",
                            count_dark_pixels(fb), expected);
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_blank_screen();
    failures += test_single_char();
    failures += test_two_chars_dont_overlap();

    if (failures) {
        printf("\nFAIL: %d check(s) failed\n", failures);
        return 1;
    }
    printf("\nPASS: all checks matched\n");
    return 0;
}
