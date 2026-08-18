/**
 * @file pins.h
 * @brief GPIO pin assignments for quad_bringup's standalone Sharp Memory
 *        LCD (LS027B7DH01) bring-up project.
 */
#ifndef QUAD_BRINGUP_PINS_H
#define QUAD_BRINGUP_PINS_H

// LS027B7DH01, driven over SPI0 - matches pico_sharpmem_display's own
// already-confirmed-working pinout exactly (see
// sharpdisp_init_freq_hz()'s hardcoded defaults in
// third_party/pico_sharpmem_display/include/sharpdisp/sharpdisp.h), so
// this bring-up program passes these same numbers to sharpdisp_init()
// explicitly rather than relying on that convenience wrapper.
//
//   VIN  -> Pico VSYS (pin 39)
//   GND  -> Pico GND
//   CLK  -> PIN_LCD_SCK  below (SPI0 SCK)
//   DIN  -> PIN_LCD_MOSI below (SPI0 TX)
//   CS   -> PIN_LCD_CS   below
//   DISP -> tie high (3V3) - panel power-on/off pin, always-on for bring-up
//
// No EXTCOMIN pin on this wiring: VCOM inversion is toggled entirely in
// software by sharpdisp_refresh_vscroll(), piggybacked on the SPI
// command byte on every refresh - see main.c's periodic-refresh loop,
// which exists specifically to keep this happening even when idle.

#define PIN_LCD_CS    17
#define PIN_LCD_SCK   18
#define PIN_LCD_MOSI  19

#endif // QUAD_BRINGUP_PINS_H
