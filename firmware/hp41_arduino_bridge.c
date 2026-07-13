/**
 * @file hp41_arduino_bridge.c
 * @brief See hp41_arduino_bridge.h for the design of this dormant
 *        fallback display path.
 */

#include "hp41_arduino_bridge.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "pins.h"
#include "st7920.h" // LCD_FB_SIZE
#include "hp41_display_tables.h" // HP41_NUM_CELLS

#define BRIDGE_UART      uart0
#define BRIDGE_BAUD_RATE 9600 // matches the Arduino side's SoftwareSerial rate -
                              // see Arduino NHD14432/NHD14432_DisplayBridge.ino.
                              // Kept conservative: SoftwareSerial is bit-banged
                              // (interrupt-driven bit sampling), not a hardware
                              // UART, and needs a comfortable safety margin.

#define FRAME_SYNC 0xAA

// lcd_a/b/c/lcd_ann are plain globals in emu41gcc/display.c with no
// header exposing them - declared extern here directly, same pattern
// hp41_display_bridge.c uses for the same globals.
extern unsigned char lcd_a[HP41_NUM_CELLS];
extern unsigned char lcd_b[HP41_NUM_CELLS];
extern unsigned char lcd_c[HP41_NUM_CELLS];
extern int lcd_ann;

/** lcd_a[12] + lcd_b[12] + lcd_c[12] + lcd_ann (2 bytes, low byte then
 *  high byte) - see hp41_arduino_bridge_send_display_state()'s header
 *  comment. */
#define DISPLAY_STATE_SIZE (HP41_NUM_CELLS * 3 + 2)

/**
 * @brief Initialize the UART link to the Arduino bridge; see the header.
 */
void hp41_arduino_bridge_init(void) {
    /* uart_init() returns the actual baud rate the hardware clock
     * divider achieved, which can differ slightly from what was
     * requested - checked here (Power of 10, Rule 7) rather than
     * silently discarded, since a divider far off the requested rate
     * would silently corrupt every byte this bridge ever sends. */
    uint actual_baud = uart_init(BRIDGE_UART, BRIDGE_BAUD_RATE);
    assert(actual_baud > 0);
    assert(actual_baud >= BRIDGE_BAUD_RATE * 9 / 10
           && actual_baud <= BRIDGE_BAUD_RATE * 11 / 10); /* within 10% */
    gpio_set_function(PIN_ARDUINO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_ARDUINO_UART_RX, GPIO_FUNC_UART);
}

/**
 * @brief Send a full pixel framebuffer to the Arduino; see the header.
 *
 * @param fb Framebuffer to send, exactly LCD_FB_SIZE bytes.
 */
void hp41_arduino_bridge_send_frame(const uint8_t *fb) {
    assert(fb != NULL);
    assert(LCD_FB_SIZE > 0);
    uint8_t checksum = 0;
    for (int i = 0; i < LCD_FB_SIZE; i++) {
        checksum ^= fb[i];
    }

    uart_putc_raw(BRIDGE_UART, FRAME_SYNC);
    uart_write_blocking(BRIDGE_UART, fb, LCD_FB_SIZE);
    uart_putc_raw(BRIDGE_UART, checksum);
}

/**
 * @brief Send the Nut CPU's raw display registers to the Arduino; see the header.
 */
void hp41_arduino_bridge_send_display_state(void) {
    uint8_t payload[DISPLAY_STATE_SIZE];
    assert(sizeof(payload) == DISPLAY_STATE_SIZE);
    /* lcd_ann is packed into 2 bytes below (low byte, then high byte) -
     * only meaningful if it actually fits the 12 annunciator bits it's
     * documented to hold (see CLAUDE.md's "Display" section). */
    assert(lcd_ann >= 0 && lcd_ann <= 0xFFF);
    memcpy(payload, lcd_a, HP41_NUM_CELLS);
    memcpy(payload + HP41_NUM_CELLS, lcd_b, HP41_NUM_CELLS);
    memcpy(payload + HP41_NUM_CELLS * 2, lcd_c, HP41_NUM_CELLS);
    payload[HP41_NUM_CELLS * 3] = (uint8_t)(lcd_ann & 0xFF);
    payload[HP41_NUM_CELLS * 3 + 1] = (uint8_t)((lcd_ann >> 8) & 0xFF);

    uint8_t checksum = 0;
    for (int i = 0; i < DISPLAY_STATE_SIZE; i++) {
        checksum ^= payload[i];
    }

    // TEMPORARY diagnostic - dump the exact raw payload bytes so a
    // specific state (e.g. "2.0000") can be captured once and replayed
    // directly to the Arduino in isolation, bypassing ROM/timing bursts
    // entirely - see CLAUDE.md's "Screen goes blank" section for what
    // this was used for. Confirmed no longer needed day-to-day; left
    // commented out (not deleted) rather than stripped, in case a
    // similar isolated-payload test is useful again later.
    // printf("soynut: raw payload (%d bytes) + checksum 0x%02X: ", DISPLAY_STATE_SIZE, checksum);
    // for (int i = 0; i < DISPLAY_STATE_SIZE; i++) {
    //     printf("%02X", payload[i]);
    // }
    // printf("\n");

    uart_putc_raw(BRIDGE_UART, FRAME_SYNC);
    uart_write_blocking(BRIDGE_UART, payload, DISPLAY_STATE_SIZE);
    uart_putc_raw(BRIDGE_UART, checksum);
}

/**
 * @brief TEMPORARY diagnostic: send a hardcoded, known-good display payload; see the header.
 */
void hp41_arduino_bridge_send_test_payload(void) {
    // Captured verbatim from a real run's raw-payload hex dump (see
    // above) at the exact moment the ROM computed "2.0000" (instr=9217,
    // local/576-byte checksum 0x63) - this is genuinely correct HP-41
    // display content, not synthetic test data.
    static const uint8_t payload[DISPLAY_STATE_SIZE] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x07, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
    };
    assert(sizeof(payload) == DISPLAY_STATE_SIZE);
    assert(DISPLAY_STATE_SIZE > 0);
    uint8_t checksum = 0;
    for (int i = 0; i < DISPLAY_STATE_SIZE; i++) {
        checksum ^= payload[i];
    }
    printf("soynut: sending fixed test payload, checksum 0x%02X\n", checksum);
    uart_putc_raw(BRIDGE_UART, FRAME_SYNC);
    uart_write_blocking(BRIDGE_UART, payload, DISPLAY_STATE_SIZE);
    uart_putc_raw(BRIDGE_UART, checksum);
}
