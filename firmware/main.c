/**
 * @file main.c
 * @brief Full system integration: boots the HP-41 ROM on the emulated Nut
 *        CPU core, drives the ST7920 LCD, and feeds USB-serial keypresses
 *        into the emulator's keyboard buffer.
 *
 * Display output goes directly to the LCD over st7920.c's 8-bit parallel
 * drive - see CLAUDE.md "Direct Pico->LCD parallel link" and pins.h. The
 * Arduino bridge (hp41_arduino_bridge.h/.c) is kept intact but dormant -
 * see pins.h's "Arduino display bridge" note for how to swap back to it
 * if the direct link ever needs to be bypassed.
 *
 * Display bring-up's static test bitmaps (bitmaps.c/.h) are still built
 * but no longer called here either - kept around as a hardware-debugging
 * aid (wire up a temporary call to them if the real display output is
 * dark/wrong and you need to isolate a wiring problem from a software
 * one).
 */

#include "pico/stdlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "st7920.h"
#include "nut_rom.h"
#include "hp41_display_bridge.h"
#include "hp41_key_bridge.h"
#include "hp41_key_hold_bridge.h"
#include "hp41_arduino_bridge.h"
#include "hp41_persist_state.h"
#include "hp41_persist_flash.h"

#define GLOBAL extern
#include "nutcpu.h"

// TEMPORARY: prefixes every debug line with milliseconds-since-boot, so
// the Pico's log can be lined up chronologically against the Arduino's
// own millis()-timestamped log (see NHD14432_DisplayBridge.ino) during
// the "screen goes blank" investigation - see CLAUDE.md. The two boards'
// clocks aren't synchronized with each other (each just counts from its
// own power-on/reset), so this only gives useful correlation if both are
// power-cycled at roughly the same moment.
/**
 * @brief printf()-style debug log line, prefixed with milliseconds since boot.
 *
 * TEMPORARY: the timestamp prefix lets this board's log be lined up
 * chronologically against the Arduino bridge's own millis()-timestamped
 * log (see NHD14432_DisplayBridge.ino) during hardware bring-up. The two
 * boards' clocks are not synchronized - each just counts from its own
 * power-on/reset - so this only gives useful correlation if both are
 * power-cycled at roughly the same moment.
 *
 * @param fmt printf()-style format string; must be non-NULL.
 * @param ... printf()-style arguments matching @p fmt.
 *
 * Power of 10, Rule 5 note: fmt!=NULL is the only real precondition
 * this function has - its whole job is "prefix a timestamp, forward
 * the rest to vprintf" - so a second assertion here would just restate
 * that forwarding rather than check anything new.
 */
static void dbg(const char *fmt, ...) {
    assert(fmt != NULL);
    printf("[%lums] ", (unsigned long)to_ms_since_boot(get_absolute_time()));
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/** Throttle target for executeNUT(), in emulated Nut CPU instructions/sec.
 *
 * The real Nut CPU runs at roughly tens-to-a-few-hundred kHz - this
 * target (~200,000 instructions/sec) is an approximate, commonly-cited
 * figure, not a cycle-exact one; getting it perfect isn't the point,
 * just getting ROM-side timing assumptions (auto-power-off, key
 * debounce) into a human-usable range instead of the ~1.4 MILLION
 * instructions/sec this Pico actually runs the core at, which was
 * observed on real hardware to blow through the ROM's own shutdown
 * timer before a human (or even a quick scripted keypress) could react.
 */
#define TARGET_INSTRUCTIONS_PER_SEC 200000

/** Defensive upper bound on getchar_timeout_us() calls per drain_usb_bytes()
 *  call (Power of 10, Rule 2) - see that function's header for why this
 *  loop otherwise has no provable bound of its own. Set well above any
 *  realistic single USB burst; keybuffer[]'s own 8-slot cap means a real
 *  drain always finishes in well under this many iterations. */
#define MAX_BYTES_PER_DRAIN 256

/**
 * @brief Drain any pending USB serial bytes into the key bridge, without blocking.
 *
 * Factored out so it can be called both once per outer-loop iteration
 * (the normal case) and once per single-stepped instruction while a key
 * hold is in progress - see the main loop's hold-handling block below
 * for why the latter matters: checking for an incoming "[-]" release
 * only once per up-to-1000-instruction batch (as an earlier version of
 * this fix did) meant a real release sitting in the USB buffer wasn't
 * even looked at until that whole batch completed, artificially
 * sustaining every tap - however fast - past the ROM's own blink
 * threshold (see CLAUDE.md's "Real key hold-duration" section).
 *
 * Power of 10, Rule 2: CLAUDE.md flags this loop by name as the one
 * existing pattern that isn't trivially bounded - it only terminates
 * because the USB FIFO is finite, not because anything here counts.
 * MAX_BYTES_PER_DRAIN adds an explicit, provable cap (well above any
 * realistic single burst - keybuffer[] itself caps useful input at 8
 * pending keys) as a defensive backstop, without changing normal
 * behavior at all: a real USB FIFO drains in well under this many
 * iterations every time.
 */
static void drain_usb_bytes(void) {
    int c;
    int drained = 0;
    while (drained < MAX_BYTES_PER_DRAIN
           && (c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        // TEMPORARY: confirms bytes are actually arriving over USB and
        // shows the key bridge's effect on keybuffer[] directly.
        dbg("soynut: got byte 0x%02X ('%c'), lgkeybuf %d -> ",
               (unsigned)c, (c >= 32 && c < 127) ? c : '.', lgkeybuf);
        hp41_key_bridge_feed_byte(c);
        printf("%d\n", lgkeybuf);
        drained++;
    }
    assert(drained >= 0 && drained <= MAX_BYTES_PER_DRAIN);
    assert(lgkeybuf >= 0 && lgkeybuf <= 8);
}

/**
 * @brief Entry point: bring up hardware, boot the ROM, then run forever.
 *
 * Per main-loop iteration: drains pending USB keypresses (always, even
 * while asleep, since a key is what wakes it); if asleep and a key is
 * now queued, resets regPC/flagKey and wakes; otherwise skips
 * executeNUT() entirely while asleep; else runs executeNUT(1000)
 * (single-stepping instead, sustaining the key-hold state, if a hold
 * is active); throttles via sleep_us() to TARGET_INSTRUCTIONS_PER_SEC;
 * on fdsp, computes/checksums/pushes a new framebuffer to the LCD; on
 * POWOFF, goes to sleep; on an invalid opcode, halts. See CLAUDE.md's
 * "main.c" section for the full walkthrough.
 *
 * @return Never returns - this is bare-metal firmware with no OS to
 *         return control to.
 */
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
    assert(regPC == 0); /* nut_boot()'s documented cold-start value */

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

    // "Continuous memory": restore whatever hp41_persist_flash_save()
    // last wrote (see the POWOFF handling below), if it's still valid.
    // A successful restore reuses the exact same POWOFF->wake transition
    // above rather than inventing a new boot path: it just starts
    // "asleep", waiting for a keypress (ON) to trigger the existing
    // flagKey=0/regPC=0 wake block below - matching real continuous-
    // memory HP-41 power-on, which doesn't unprompted redraw/run either,
    // it waits for you to press ON. nut_boot()'s own ROM wiring and
    // Carry=1 coldstart default stand as-is on a failed/absent restore.
    hp41_persist_state_t saved_state;
    if (hp41_persist_flash_load(&saved_state)) {
        hp41_persist_apply(&saved_state);
        asleep = true;
        dbg("soynut: restored continuous memory from flash\n");
    } else {
        dbg("soynut: no valid persisted memory - MEMORY LOST cold start\n");
    }

    dbg("soynut: entering main loop\n");

    static uint8_t framebuf[LCD_FB_SIZE];
    int render_count = 0;
    uint32_t last_heartbeat_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        // Drain any pending USB serial keypresses without blocking.
        // Always runs, even while "asleep" - a key is exactly what wakes
        // it up.
        drain_usb_bytes();

        // "[CLRMEM]" - the deliberate "give me MEMORY LOST back" reset
        // (see hp41_key_bridge.h). Handled here, not inside the key
        // bridge itself, since the key bridge is pure/host-testable and
        // has no business touching flash or CPU state directly. Erases
        // the persisted snapshot, then re-runs the same cold-start reset
        // nut_boot() does at true power-on (plus an explicit espaceRAM
        // clear, since nut_boot() itself never touches it - see
        // nut_rom.c) and drops out of "asleep" so the ROM's own
        // cold-start code runs immediately, exactly like a real power-on
        // reset would, rather than waiting for a further keypress.
        if (hp41_key_bridge_clear_memory_requested()) {
            dbg("soynut: CLRMEM requested - erasing persisted memory\n");
            hp41_persist_flash_erase();
            nut_boot();
            memset(espaceRAM, 0, sizeof(espaceRAM));
            asleep = false;
        }

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
        assert(ret >= 0 && ret <= 3); /* executeNUT()'s only documented return values */

        // Throttle to approximate real Nut CPU speed - see
        // TARGET_INSTRUCTIONS_PER_SEC's comment above. executeNUT() may
        // have run fewer than 1000 instructions (stops early on fdsp/ret),
        // so pace based on how many it actually ran, not the ceiling.
        int instructions_ran = cptinstr - cptinstr_before;
        assert(instructions_ran >= 0);
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
            // new key arrives. This is also the "continuous memory"
            // save point: the real HP-41's auto-power-off already fires
            // after essentially every keypress (see CLAUDE.md), so
            // saving here gives near-continuous persistence with only a
            // small, honestly-documented gap (a literal power yank
            // between keypresses, before the next auto-POWOFF, loses
            // that in-flight session). hp41_persist_flash_save() skips
            // the actual flash write entirely when nothing changed, so
            // this costs nothing extra on an idle/no-op POWOFF.
            dbg("soynut: POWOFF (Carry=%d) - sleeping until next key\n", Carry);

            // POWOFF fires after essentially every keystroke (see the big
            // comment above) - it's a power-saving CPU halt, not "the
            // user turned the calculator off," and a real HP-41's display
            // stays lit through it (the direct-drive segments are held by
            // the ROM's own dspon-controlled state, not the running CPU).
            // Only actually blank this panel's persistent-GDRAM glass when
            // the ROM itself says the display should be off right now -
            // emu41gcc/nutcpu.c's POWOFF opcode sets Carry=(dspon==0), so
            // checking dspon directly here (rather than relying on that
            // Carry side effect) is the same signal, more self-explanatory
            // at the call site. Clearing unconditionally on every POWOFF
            // (an earlier version of this fix) blanked the screen after
            // nearly every keystroke instead of only on a real power-off -
            // confirmed as a real regression on hardware, not theoretical.
            if (dspon == 0) {
                st7920_clear();
                dbg("soynut: LCD cleared for power-off (dspon=0)\n");
            }

            hp41_persist_state_t snap;
            hp41_persist_capture(&snap);
            hp41_persist_flash_save(&snap);
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
            /* Power of 10, Rule 2/5: this intentionally-nonterminating
             * halt loop is the "explicit recovery on failure" Rule 5
             * calls for on bare-metal firmware with no OS/exception
             * handler to hand an error to - see DEVIATIONS.md's Rule 2
             * entry, which covers this pattern alongside the outer
             * while(true) above. */
            while (true) {
                tight_loop_contents();
            }
        }
    }
}
