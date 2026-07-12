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

// Minimum spacing between hp41_arduino_bridge_send_display_state() calls
// - see the big comment at the call site in the main loop below for the
// full story (CLAUDE.md's "Screen goes blank" section). The Arduino's
// drawFrameFromRAM() takes ~30-50ms of real wall-clock time per frame,
// and (since a fix elsewhere made reception impossible, not just
// unreliable, during that window) sending faster than the Arduino can
// keep up risks losing an entire rapid burst - including its final,
// otherwise-correct settled frame - not just a redundant middle one.
// NOTE: 80ms turned out not to be enough margin in practice (see
// CLAUDE.md) - uart_write_blocking() only blocks until bytes are queued
// into the Pico's UART hardware TX FIFO, not until they've actually
// finished shifting out over the wire (~42ms for the 40-byte packet at
// 9600 baud), so the true idle gap the Arduino gets is this interval
// MINUS that ~42ms transmission tail, not the full interval. Bumped up
// with generous margin to comfortably cover transmission (~42ms) +
// draw (~30-50ms) + safety margin, rather than trying to model the
// hardware FIFO's exact timing.
#define MIN_ARDUINO_SEND_INTERVAL_MS 180

// Full system bring-up: boots the real HP-41 OS ROM (roms/rom_images.c,
// wired via emu41gcc_compat/nut_rom.c), computes the emulator's LCD state
// into a framebuffer (hp41_display_bridge.c) whenever it flags a display
// change, and feeds USB serial bytes into the keyboard buffer
// (hp41_key_bridge.c). See CLAUDE.md step 6.
//
// *** Display output currently goes out over the Arduino bridge, NOT
// directly to the LCD - see CLAUDE.md "Arduino display bridge" and
// pins.h. The direct path (st7920_init()/st7920_clear()/
// hp41_display_render(), which pushes straight to the LCD over
// firmware/st7920.c) is fully intact below, just commented out - swap
// the two marked blocks back once a better level shifter is in and the
// direct path is worth retrying. ***
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
        // TEMPORARY: '`' has no HP-41 keycode (tabcode['`']==0, see
        // hp41_key_bridge.c), repurposed here as a diagnostic trigger
        // to send a fixed, captured-known-good "2.0000" payload
        // directly to the Arduino, bypassing the Nut CPU/ROM entirely
        // - isolates whether this specific content renders correctly
        // in isolation, free of the normal 3-frames-per-key burst
        // timing. See hp41_arduino_bridge_send_test_payload().
        if (c == '`') {
            hp41_arduino_bridge_send_test_payload();
            continue;
        }
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

    // dbg("soynut: st7920_init()...\n");
    // st7920_init();
    // dbg("soynut: st7920_clear()...\n");
    // st7920_clear();
    dbg("soynut: hp41_arduino_bridge_init()...\n");
    hp41_arduino_bridge_init();
    dbg("soynut: nut_boot()...\n");
    nut_boot();
    dbg("soynut: entering main loop\n");

    static uint8_t framebuf[LCD_FB_SIZE];
    int render_count = 0;
    uint32_t last_heartbeat_ms = to_ms_since_boot(get_absolute_time());
    // Paces hp41_arduino_bridge_send_display_state() calls - see the big
    // comment at the call site below for why this exists (the Arduino
    // can only receive+decode+draw a frame roughly every ~60-80ms, and
    // sending faster than that used to cause dropped/corrupted frames,
    // sometimes including the final settled one - see CLAUDE.md's
    // "Screen goes blank" section).
    uint32_t last_arduino_send_ms = 0;

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

            // Still computed locally purely as ground-truth debug output
            // (ASCII art + checksum below) - independent of whatever the
            // Arduino does with the data it actually receives. What goes
            // out over the wire now is the compact raw display state
            // (see hp41_arduino_bridge_send_display_state()), not this
            // framebuffer - the Arduino re-derives an identical
            // framebuffer on its own side, from its own copy of the same
            // segment/pixel tables (see NHD14432_DisplayBridge/
            // hp41_display_tables_avr.h). If the two ever disagree, that
            // means the tables or decode logic have drifted between the
            // two sides, not a display bug.
            hp41_display_compute_framebuffer(framebuf);

            // TEMPORARY: ground-truth verification - dumps exactly what the
            // ROM computed for display, independent of the Arduino/wiring/
            // LCD chain entirely, plus an XOR checksum (same algorithm as
            // hp41_arduino_bridge_send_frame()'s wire checksum) to compare
            // by eye against known values (e.g. the 3 stock Arduino test
            // bitmaps' checksums) without needing the full ASCII art.
            uint8_t checksum = 0;
            for (int i = 0; i < LCD_FB_SIZE; i++) {
                checksum ^= framebuf[i];
            }
            dbg("soynut: sending display state #%d to Arduino (PC=0x%04X, instr=%d, local checksum=0x%02X)\n",
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

            // Pace sends to the Arduino - the ROM can legitimately emit
            // several fdsp events in quick succession per keypress (a
            // real, normal "settle, settle, final" pattern, not a bug),
            // but the Arduino can only fully receive+decode+draw a frame
            // roughly every ~30-50ms. Sending faster than that used to
            // cause dropped frames - usually harmless (the dropped one
            // was a duplicate of its neighbor) but not always: CLX
            // (backspace) right after a digit produces 4 rapid updates
            // instead of the usual 3, and was observed on real hardware
            // to drop *all four*, including the final correct one,
            // leaving the display stuck blank until an unrelated later
            // keypress - see CLAUDE.md's "Screen goes blank" section.
            // Blocking here (rather than skipping the send) guarantees
            // every update - including the final one - always eventually
            // reaches the Arduino, just not necessarily instantly; a few
            // tens of ms of extra latency on intermediate, already-
            // invisible settling frames is not perceptible to a human.
            uint32_t now_send_ms = to_ms_since_boot(get_absolute_time());
            uint32_t since_last_send_ms = now_send_ms - last_arduino_send_ms;
            // TEMPORARY prints used to diagnose why an earlier, shorter
            // interval (80ms) still weren't enough margin - confirmed
            // fixed and no longer needed day-to-day, left here
            // commented out rather than deleted (see CLAUDE.md).
            // dbg("soynut: pacing check: since_last_send_ms=%lu (last=%lu now=%lu)\n",
            //     (unsigned long)since_last_send_ms, (unsigned long)last_arduino_send_ms, (unsigned long)now_send_ms);
            if (last_arduino_send_ms != 0 && since_last_send_ms < MIN_ARDUINO_SEND_INTERVAL_MS) {
                uint32_t wait_ms = MIN_ARDUINO_SEND_INTERVAL_MS - since_last_send_ms;
                // dbg("soynut: pacing: sleeping %lu ms before send\n", (unsigned long)wait_ms);
                sleep_ms(wait_ms);
            }
            hp41_arduino_bridge_send_display_state();
            last_arduino_send_ms = to_ms_since_boot(get_absolute_time());
            // hp41_arduino_bridge_send_frame(framebuf); // old, fatter wire format - see hp41_arduino_bridge.h
            // st7920_draw_frame(framebuf); // direct-drive path - dormant again, see CLAUDE.md
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
