#include "pico/stdlib.h"
#include <stdio.h>

#include "st7920.h"

// Sole purpose of this program: get ANYTHING to show up on the physical
// NHD-14432WG LCD over the direct 3-wire serial link, with zero
// dependency on the Nut CPU emulator, ROM, key bridge, or Arduino bridge
// - all of which are proven correct/irrelevant to this problem (see
// ../CLAUDE.md's "Arduino display bridge" section for the full bring-up
// history). If this can't get a single pixel lit, the problem is
// definitely in the LCD/level-shifter/wiring/protocol layer, not
// anything above it.
//
// Interactive over USB serial (115200 baud) - type a single character at
// any time to run that test once; auto-cycles through the basic tests by
// itself if left alone, so you don't even need to type anything to start
// seeing (attempted) activity.
//
// Commands:
//   i - re-run st7920_gpio_init() + st7920_run_init_sequence()
//   c - fill GDRAM with 0x00 (blank)
//   f - fill GDRAM with 0xFF (solid on - the clearest possible "did
//       anything land on the glass" test)
//   k - checkerboard pattern
//   p - toggle CS polarity (prints new state) - does NOT auto-reinit,
//       press 'i' afterward to actually re-run init under the new polarity
//   a - toggle auto-cycle on/off

static void print_help(void) {
    printf("lcd_bringup: commands: i=reinit c=clear f=fill(solid) k=checkerboard p=toggle-CS-polarity a=toggle-auto-cycle\n");
}

static void do_checkerboard(void) {
    static uint8_t fb[LCD_FB_SIZE];
    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        for (int x = 0; x < LCD_BYTES_PER_ROW; x++) {
            // 8x8 checkerboard, alternating by byte-column and row-group
            fb[y * LCD_BYTES_PER_ROW + x] = ((x + (y / 8)) % 2) ? 0xFF : 0x00;
        }
    }
    st7920_draw_frame(fb);
}

static void run_command(int c) {
    switch (c) {
        case 'i':
            printf("lcd_bringup: re-running gpio_init + init sequence (CS active_%s)\n",
                   st7920_get_cs_active_low() ? "low" : "high");
            st7920_gpio_init();
            st7920_run_init_sequence();
            break;
        case 'c':
            printf("lcd_bringup: fill 0x00 (blank)\n");
            st7920_fill(0x00);
            break;
        case 'f':
            printf("lcd_bringup: fill 0xFF (solid on)\n");
            st7920_fill(0xFF);
            break;
        case 'k':
            printf("lcd_bringup: checkerboard\n");
            do_checkerboard();
            break;
        case 'p': {
            bool new_state = !st7920_get_cs_active_low();
            st7920_set_cs_active_low(new_state);
            printf("lcd_bringup: CS polarity now active-%s (press 'i' to reinit under this setting)\n",
                   new_state ? "low" : "high");
            break;
        }
        default:
            print_help();
            break;
    }
}

int main(void) {
    stdio_init_all();

    for (int i = 3; i > 0; i--) {
        printf("lcd_bringup: starting in %d...\n", i);
        sleep_ms(1000);
    }

    print_help();

    st7920_gpio_init();
    st7920_run_init_sequence();
    printf("lcd_bringup: initial init sequence done (CS active_%s)\n",
           st7920_get_cs_active_low() ? "low" : "high");

    bool auto_cycle = true;
    int auto_state = 0;
    uint32_t last_auto_ms = to_ms_since_boot(get_absolute_time());
    uint32_t last_heartbeat_ms = last_auto_ms;

    printf("lcd_bringup: auto-cycle ON (clear/solid/checkerboard every 2s) - type any command to take over, 'a' to re-enable auto-cycle\n");

    while (true) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == 'a') {
                auto_cycle = !auto_cycle;
                printf("lcd_bringup: auto-cycle %s\n", auto_cycle ? "ON" : "OFF");
            } else {
                auto_cycle = false;
                run_command(c);
            }
        }

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        if (auto_cycle && now_ms - last_auto_ms >= 2000) {
            last_auto_ms = now_ms;
            auto_state = (auto_state + 1) % 3;
            switch (auto_state) {
                case 0: printf("lcd_bringup: [auto] clear\n"); st7920_fill(0x00); break;
                case 1: printf("lcd_bringup: [auto] solid\n"); st7920_fill(0xFF); break;
                case 2: printf("lcd_bringup: [auto] checkerboard\n"); do_checkerboard(); break;
            }
        }

        if (now_ms - last_heartbeat_ms >= 1000) {
            last_heartbeat_ms = now_ms;
            printf("lcd_bringup: heartbeat t=%lums auto_cycle=%d CS_active_%s\n",
                   (unsigned long)now_ms, auto_cycle,
                   st7920_get_cs_active_low() ? "low" : "high");
        }
    }
}
