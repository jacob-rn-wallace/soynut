#include "pico/stdlib.h"
#include <stdio.h>
#include <stdarg.h>

#include "st7920.h"
#include "nut_rom.h"
#include "hp41_display_bridge.h"
#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"
#include "hp41_arduino_bridge.h"

#define GLOBAL extern
#include "nutcpu.h"

// TEMPORARY: prefixes every debug line with milliseconds-since-boot, so
// the Pico's log can be lined up chronologically against the Arduino's
// own millis()-timestamped log (see NHD14432_DisplayBridge.ino) during
// the "screen goes blank" investigation - see CLAUDE.md. The two boards'
// clocks aren't synchronized with each other (each just counts from its
// own power-on/reset), so this only gives useful correlation if both are
// power-cycled at roughly the same moment.
static void dbg(const char *fmt, ...) {
    printf("[%lums] ", (unsigned long)to_ms_since_boot(get_absolute_time()));
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// The real Nut CPU runs at roughly tens-to-a-few-hundred kHz - this
// target (~200,000 instructions/sec) is an approximate, commonly-cited
// figure, not a cycle-exact one; getting it perfect isn't the point,
// just getting ROM-side timing assumptions (auto-power-off, key
// debounce) into a human-usable range instead of the ~1.4 MILLION
// instructions/sec this Pico actually runs the core at, which was
// observed on real hardware to blow through the ROM's own shutdown
// timer before a human (or even a quick scripted keypress) could react.
#define TARGET_INSTRUCTIONS_PER_SEC 200000

// Full system bring-up: boots the real HP-41 OS ROM (roms/rom_images.c,
// wired via emu41gcc_compat/nut_rom.c), computes the emulator's LCD state
// into a framebuffer (hp41_display_bridge.c) whenever it flags a display
// change, and feeds USB serial bytes into the keyboard buffer
// (hp41_key_bridge.c). See CLAUDE.md step 6.
//
// *** Display output goes directly to the LCD over firmware/st7920.c's
// 8-bit parallel drive - see CLAUDE.md "Hardware" and pins.h. The
// Arduino bridge (hp41_arduino_bridge.h/.c) is kept intact but dormant,
// not deleted - see pins.h's "Arduino display bridge" note for how to
// swap back to it if the direct link ever needs to be bypassed. ***
//
// Display bring-up's static test bitmaps (bitmaps.c/.h) are still built
// but no longer called here either - kept around as a hardware-debugging
// aid (wire up a temporary call to them if the real display output is
// dark/wrong and you need to isolate a wiring problem from a software one).

// Drains any pending USB serial keypresses without blocking. Factored
// out so it can be called both once per outer-loop iteration (the
// normal case) and once per single-stepped instruction while a key
// hold is in progress - see the main loop's hold-handling block below
// for why the latter matters: checking for an incoming "[-]" release
// only once per up-to-1000-instruction batch (as an earlier version of
// this fix did) meant a real release sitting in the USB buffer wasn't
// even looked at until that whole batch completed, artificially
// sustaining every tap - however fast - past the ROM's own blink
// threshold (see CLAUDE.md's "Real key hold-duration" section).
static void drain_usb_bytes(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        // TEMPORARY: confirms bytes are actually arriving over USB and
        // shows the key bridge's effect on keybuffer[] directly.
        dbg("soynut: got byte 0x%02X ('%c'), lgkeybuf %d -> ",
               (unsigned)c, (c >= 32 && c < 127) ? c : '.', lgkeybuf);
        hp41_key_bridge_feed_byte(c);
        printf("%d\n", lgkeybuf);
    }
}

int main(void) {
    stdio_init_all();

    // --- TEMPORARY hardware bring-up debug logging - strip once the
    // display is confirmed working. Pico boots faster than a human can
    // open a serial terminal, so this counts down for a few seconds
    // (each print is a fresh chance to catch it live) before continuing,
    // and traces each major init step so a real hardware failure can be
    // isolated to "software never got here" vs "software did everything
    // right, the electrical/protocol path to the display is what's broken".
    for (int i = 5; i > 0; i--) {
        dbg("soynut: starting in %d...\n", i);
        sleep_ms(1000);
    }

    dbg("soynut: st7920_init()...\n");
    st7920_init();
    dbg("soynut: st7920_clear()...\n");
    st7920_clear();
    // dbg("soynut: hp41_arduino_bridge_init()...\n");
    // hp41_arduino_bridge_init(); // dormant - see pins.h "Arduino display bridge" note
    dbg("soynut: nut_boot()...\n");
    nut_boot();
    dbg("soynut: entering main loop\n");

    static uint8_t framebuf[LCD_FB_SIZE];
    int render_count = 0;
    uint32_t last_heartbeat_ms = to_ms_since_boot(get_absolute_time());

    // Real HP-41 hardware halts the CPU clock entirely after POWOFF and
    // only resumes it via a hardware keyboard-scan interrupt, which
    // restarts execution at address 0 - it never resumes wherever
    // POWOFF left off. emu41gcc's own reference main loop (emu41.c,
    // handling res==1) matches this: it stops calling executeNUT(),
    // waits for a key, then sets regPC=0 before resuming. Without this,
    // whatever ROM code sits after POWOFF (which assumes the CPU is
    // genuinely asleep) just spins forever as a busy loop instead -
    // exactly what was observed on real hardware: PC stuck at 0x0193,
    // instruction count climbing at ~1.4M/s, no further key or display
    // activity ever again.
    bool asleep = false;

    while (true) {
        // Drain any pending USB serial keypresses without blocking.
        // Always runs, even while "asleep" - a key is exactly what wakes
        // it up.
        drain_usb_bytes();

        // TEMPORARY: once/second liveness check, independent of display
        // activity - proves whether the CPU loop is genuinely still
        // running (PC/instr advancing) vs actually stuck, and shows the
        // keyboard state machine's own view of things (flagKey: 0=idle,
        // 1=key down, 2=released-debounce - see nutcpu.c's dokey()).
        // Moved ahead of the `asleep` check below (was previously after
        // it, past a `continue` that skipped this entirely while asleep -
        // meaning the heartbeat silently stopped the instant the ROM went
        // to POWOFF, which looked identical to a genuine hang/crash from
        // the serial log alone and caused real confusion during the
        // direct-serial-LCD bring-up - see CLAUDE.md).
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_heartbeat_ms >= 1000) {
            last_heartbeat_ms = now_ms;
            dbg("soynut: heartbeat PC=0x%04X instr=%d lgkeybuf=%d flagKey=%d regK=0x%02X asleep=%d\n",
                   regPC, cptinstr, lgkeybuf, flagKey, regK, asleep);
        }

        if (asleep) {
            if (lgkeybuf > 0) {
                dbg("soynut: waking from POWOFF (key queued), resetting PC to 0\n");
                flagKey = 0; // matches emu41.c's reference: force dokey()'s
                             // state machine to treat this as a fresh press
                regPC = 0;
                asleep = false;
            } else {
                continue; // keep waiting - do not call executeNUT() while asleep
            }
        }

        // While a real key hold is in progress, single-step executeNUT()
        // and re-assert flagKB/regK before every instruction rather than
        // running the normal larger batch below - the ROM's own
        // hold-duration check clears flagKB (via RSTKB) and re-reads it
        // (via CHKKB) within a handful of instructions, so a coarser
        // batch would let it see several spurious "released" reads
        // before the next reassertion, silently reproducing the
        // original bug (see hp41_key_hold_bridge.h's design note).
        //
        // That single-stepping only needs to apply to executeNUT()
        // itself, not the whole outer loop body. Two versions of this
        // fix were tried before this one:
        //   1. Running the *entire* outer loop body (USB byte-drain,
        //      heartbeat check, throttle sleep_us() call) once per
        //      single Nut CPU instruction - correct, but multiplied that
        //      overhead ~1000x for the duration of any hold, making the
        //      whole system noticeably less responsive while a key was
        //      held.
        //   2. Batching up to 1000 single-stepped instructions per
        //      outer-loop iteration, only checking for new USB bytes
        //      once per batch (like the non-held path) - fixed the
        //      overhead, but introduced a *different*, worse bug: a
        //      real release event ("[-]") sitting in the USB buffer
        //      wasn't looked at until the current up-to-1000-instruction
        //      batch finished, so *every* tap - however fast the real
        //      press was - got sustained for at least ~1000 instructions
        //      before release could even be noticed. That's close to
        //      (and in practice, spanning multiple batches, easily
        //      exceeds) the ROM's own ~1245-instruction "blink the
        //      label" threshold (see CLAUDE.md's "Real key hold-duration"
        //      section), so every tap started flashing the held key's
        //      name - reported right after this "fix" shipped.
        // Fix: drain USB bytes (cheap - one non-blocking read call when
        // nothing's pending) on every single inner-loop iteration, not
        // just once per batch, so a release is noticed within a handful
        // of instructions - while still only doing the actually-expensive
        // part (the throttle sleep_us() calculation) once per batch, via
        // instructions_ran below. Breaks early on fdsp/ret!=0 exactly
        // like executeNUT(1000) itself would (so display responsiveness
        // and POWOFF/invalid-opcode handling are unaffected).
        int cptinstr_before = cptinstr;
        int ret = 0;
        if (hp41_key_hold_active()) {
            for (int i = 0; i < 1000 && hp41_key_hold_active(); i++) {
                hp41_key_hold_sustain();
                ret = executeNUT(1);
                drain_usb_bytes();
                if (ret != 0 || fdsp) break;
            }
        } else {
            ret = executeNUT(1000);
        }

        // Throttle to approximate real Nut CPU speed - see
        // TARGET_INSTRUCTIONS_PER_SEC's comment above. executeNUT() may
        // have run fewer than 1000 instructions (stops early on fdsp/ret),
        // so pace based on how many it actually ran, not the ceiling.
        int instructions_ran = cptinstr - cptinstr_before;
        if (instructions_ran > 0) {
            sleep_us((instructions_ran * 1000000ULL) / TARGET_INSTRUCTIONS_PER_SEC);
        }

        // executeNUT() returns early (well before the 1000 ceiling) as
        // soon as the emulator flags the display dirty (fdsp) - see
        // nutcpu.c's executeNUT() and storeData()'s facces_dsp
        // countdown - so this stays responsive without any extra timing
        // logic here.
        if (fdsp) {
            render_count++;

            hp41_display_compute_framebuffer(framebuf);

            // TEMPORARY: ground-truth verification - dumps exactly what the
            // ROM computed for display, plus an XOR checksum, to compare
            // by eye against known values (e.g. the cold-start "MEMORY
            // LOST" screen's expected lit-pixel count/checksum - see
            // tests/display_bridge_test.c) without needing the full ASCII
            // art.
            uint8_t checksum = 0;
            for (int i = 0; i < LCD_FB_SIZE; i++) {
                checksum ^= framebuf[i];
            }
            dbg("soynut: rendering display state #%d (PC=0x%04X, instr=%d, checksum=0x%02X)\n",
                   render_count, regPC, cptinstr, checksum);
            // ASCII-art framebuffer dump - disabled (too verbose for
            // day-to-day use over USB serial). Checksum above is still
            // enough for a quick ground-truth sanity check; uncomment
            // this block if a full visual dump is needed again.
            // for (int y = 0; y < LCD_HEIGHT_PX; y++) {
            //     for (int x = 0; x < LCD_WIDTH_PX; x++) {
            //         int byte_idx = y * LCD_BYTES_PER_ROW + x / 8;
            //         int bit = (framebuf[byte_idx] >> (7 - (x % 8))) & 1;
            //         putchar(bit ? '#' : '.');
            //     }
            //     putchar('\n');
            // }

            st7920_draw_frame(framebuf);
            // hp41_arduino_bridge_send_display_state(); // dormant - see pins.h "Arduino display bridge" note
            fdsp = 0;
        }

        if (ret == 1) {
            // POWOFF - see the big comment above. Stop running until a
            // new key arrives.
            dbg("soynut: POWOFF (Carry=%d) - sleeping until next key\n", Carry);
            asleep = true;
        }

        if (ret == 2) {
            // Invalid opcode: a real bug (ROM wiring, CPU core, or a
            // stubbed peripheral not doing what execp() expects), not a
            // normal calculator state - the validated ROM boot never
            // hits this (see CLAUDE.md's ROM wiring section). Report
            // once and halt rather than spin printing the same message
            // forever (regPC doesn't advance past the bad opcode).
            dbg("soynut: invalid opcode at PC=0x%04X, halting\n", regPC);
            while (true) {
                tight_loop_contents();
            }
        }
    }
}
