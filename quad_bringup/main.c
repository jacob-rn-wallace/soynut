/**
 * @file main.c
 * @brief Standalone Sharp Memory LCD (LS027B7DH01) bring-up/wiring-
 *        verification program - see the block comment below for its
 *        purpose and serial command set.
 */

#include "pico/stdlib.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sharpdisp/sharpdisp.h"
#include "pins.h"

// Sole purpose of this program: get ANYTHING to show up on the physical
// Sharp Memory LCD over SPI, with zero dependency on the Nut CPU
// emulator, ROM, or key bridge - same isolation strategy ../lcd_bringup/
// already used for the NHD14432. Two things specifically worth
// confirming here before Phase 2/3 of the QUAD plan build on top of
// this (see ../CLAUDE.md's "Sharp Memory LCD bring-up" section):
//   1. Framebuffer polarity - a lit/dark segment must CLEAR its bit
//      against a buffer pre-filled with 0xFF, the opposite convention
//      from st7920.c. The 'f' command below is the direct test for
//      this: it should render as a dark rectangle on a light
//      background, not the reverse.
//   2. The periodic-refresh idiom - this panel needs refreshing even
//      when idle (no GDRAM of its own). This program refreshes on a
//      fixed timer unconditionally, regardless of whether the buffer
//      actually changed, to prove that idiom out here first.
//
// Interactive over USB serial (115200 baud) - type a single character at
// any time to run that test once; auto-cycles through the basic tests by
// itself if left alone, so you don't even need to type anything to start
// seeing (attempted) activity.
//
// Commands:
//   c - fill buffer 0xFF (all-white background)
//   f - fill buffer 0x00 (solid dark - the clearest possible "did
//       anything land on the glass, and with the right polarity" test)
//   k - checkerboard pattern
//   s - single hand-plotted rectangle on an otherwise white background
//       (the real polarity test - see note 1 above)
//   a - toggle auto-cycle on/off

#define DISP_WIDTH_PX 400
#define DISP_HEIGHT_PX 240
#define DISP_WIDTH_BYTES ((DISP_WIDTH_PX + 7) / 8) /* 50 */
#define DISP_BUF_SIZE (DISP_WIDTH_BYTES * DISP_HEIGHT_PX) /* 12000 */
#define DISP_SPI spi0
#define DISP_SPI_FREQ_HZ 10000000u

#define REFRESH_INTERVAL_MS 1000u
#define AUTO_CYCLE_INTERVAL_MS 2000u
#define HEARTBEAT_INTERVAL_MS 1000u

// Byte-aligned so do_test_shape() can clear whole bytes without partial-
// bit math - this is a wiring/polarity smoke test, not real content.
#define TEST_SHAPE_BYTE_COL_START 5
#define TEST_SHAPE_BYTE_COL_COUNT 10 /* 80px wide */
#define TEST_SHAPE_ROW_START 40
#define TEST_SHAPE_ROW_COUNT 160

/**
 * @brief Print the serial command menu.
 */
static void print_help(void) {
    printf("quad_bringup: commands: c=clear(white) f=fill(dark) "
           "k=checkerboard s=test-shape a=toggle-auto-cycle\n");
}

/**
 * @brief Fill the whole display buffer with one byte value and push it.
 * @param disp Initialized display handle.
 * @param fill_byte 0xFF for all-white, 0x00 for all-dark.
 */
static void do_fill(struct SharpDisp *disp, uint8_t fill_byte) {
    assert(disp != NULL);
    assert(fill_byte == 0x00 || fill_byte == 0xFF);
    memset(disp->bitmap.data, fill_byte, DISP_BUF_SIZE);
    sharpdisp_refresh(disp);
}

/**
 * @brief Render and push an 8x8 checkerboard test pattern.
 * @param disp Initialized display handle.
 */
static void do_checkerboard(struct SharpDisp *disp) {
    assert(disp != NULL);
    assert(disp->bitmap.data != NULL);
    for (int y = 0; y < DISP_HEIGHT_PX; y++) {
        for (int x = 0; x < DISP_WIDTH_BYTES; x++) {
            // 8x8 checkerboard, alternating by byte-column and row-group.
            disp->bitmap.data[y * DISP_WIDTH_BYTES + x] =
                ((x + (y / 8)) % 2) ? 0xFF : 0x00;
        }
    }
    sharpdisp_refresh(disp);
}

/**
 * @brief Render an all-white buffer with one hand-plotted dark rectangle
 *        - the direct test for correct on-segment polarity (see the
 *        header comment's note 1).
 * @param disp Initialized display handle.
 */
static void do_test_shape(struct SharpDisp *disp) {
    assert(disp != NULL);
    assert(TEST_SHAPE_BYTE_COL_START + TEST_SHAPE_BYTE_COL_COUNT <= DISP_WIDTH_BYTES);
    assert(TEST_SHAPE_ROW_START + TEST_SHAPE_ROW_COUNT <= DISP_HEIGHT_PX);
    memset(disp->bitmap.data, 0xFF, DISP_BUF_SIZE);
    for (int y = TEST_SHAPE_ROW_START; y < TEST_SHAPE_ROW_START + TEST_SHAPE_ROW_COUNT; y++) {
        uint8_t *row = disp->bitmap.data + y * DISP_WIDTH_BYTES + TEST_SHAPE_BYTE_COL_START;
        memset(row, 0x00, TEST_SHAPE_BYTE_COL_COUNT);
    }
    sharpdisp_refresh(disp);
}

/**
 * @brief Dispatch one incoming serial command byte to the matching test.
 * @param c Command character, as read from getchar_timeout_us().
 * @param disp Initialized display handle.
 */
static void run_command(int c, struct SharpDisp *disp) {
    assert(c >= 0 && c <= 255); /* caller already filtered out PICO_ERROR_TIMEOUT */
    assert(disp != NULL);
    switch (c) {
        case 'c':
            printf("quad_bringup: fill 0xFF (white)\n");
            do_fill(disp, 0xFF);
            break;
        case 'f':
            printf("quad_bringup: fill 0x00 (dark)\n");
            do_fill(disp, 0x00);
            break;
        case 'k':
            printf("quad_bringup: checkerboard\n");
            do_checkerboard(disp);
            break;
        case 's':
            printf("quad_bringup: test shape (expect a DARK rectangle on white)\n");
            do_test_shape(disp);
            break;
        default:
            print_help();
            break;
    }
}

/**
 * @brief Entry point: init the display, then loop handling serial
 *        commands, auto-cycling through the basic test patterns, and
 *        unconditionally refreshing on a fixed timer for VCOM health.
 * @return Never returns.
 */
int main(void) {
    stdio_init_all();

    for (int i = 3; i > 0; i--) {
        printf("quad_bringup: starting in %d...\n", i);
        sleep_ms(1000);
    }

    print_help();

    static uint8_t disp_buffer[DISP_BUF_SIZE];
    static struct SharpDisp disp;
    sharpdisp_init(&disp, disp_buffer, DISP_WIDTH_PX, DISP_HEIGHT_PX, 0xFF,
                   PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_MOSI, DISP_SPI, DISP_SPI_FREQ_HZ);
    printf("quad_bringup: sharpdisp_init done (%ux%u, %u byte buffer)\n",
           DISP_WIDTH_PX, DISP_HEIGHT_PX, DISP_BUF_SIZE);

    bool auto_cycle = true;
    int auto_state = 0;
    uint32_t last_auto_ms = to_ms_since_boot(get_absolute_time());
    uint32_t last_refresh_ms = last_auto_ms;
    uint32_t last_heartbeat_ms = last_auto_ms;

    printf("quad_bringup: auto-cycle ON (white/dark/checkerboard every 2s) - "
           "type any command to take over, 'a' to re-enable auto-cycle\n");

    while (true) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == 'a') {
                auto_cycle = !auto_cycle;
                printf("quad_bringup: auto-cycle %s\n", auto_cycle ? "ON" : "OFF");
            } else {
                auto_cycle = false;
                run_command(c, &disp);
            }
        }

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        if (auto_cycle && now_ms - last_auto_ms >= AUTO_CYCLE_INTERVAL_MS) {
            last_auto_ms = now_ms;
            auto_state = (auto_state + 1) % 3;
            assert(auto_state >= 0 && auto_state < 3);
            switch (auto_state) {
                case 0: printf("quad_bringup: [auto] white\n"); do_fill(&disp, 0xFF); break;
                case 1: printf("quad_bringup: [auto] dark\n"); do_fill(&disp, 0x00); break;
                case 2: printf("quad_bringup: [auto] checkerboard\n"); do_checkerboard(&disp); break;
            }
        }

        // Periodic forced refresh, independent of any content change -
        // proves out the idiom the real quad/ target's main loop will
        // need for VCOM/DC-bias health during idle periods (see the
        // header comment's note 2).
        if (now_ms - last_refresh_ms >= REFRESH_INTERVAL_MS) {
            last_refresh_ms = now_ms;
            sharpdisp_refresh(&disp);
        }

        if (now_ms - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
            last_heartbeat_ms = now_ms;
            printf("quad_bringup: heartbeat t=%lums auto_cycle=%d\n",
                   (unsigned long)now_ms, auto_cycle);
        }
    }
}
