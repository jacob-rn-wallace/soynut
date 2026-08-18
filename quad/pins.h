/**
 * @file pins.h
 * @brief GPIO pin assignments for the quad/ firmware target.
 *
 * Identical wiring to quad_bringup/pins.h (same physical Sharp Memory
 * LCD panel, same pin choice) - duplicated rather than shared since
 * these are target-specific constants, not shared logic, and
 * quad_bringup/ is meant to stay a standalone sandbox (see CLAUDE.md's
 * "Sharp Memory LCD bring-up" section) rather than something this real
 * target depends on.
 */
#ifndef QUAD_PINS_H
#define QUAD_PINS_H

// LS027B7DH01, driven over SPI0 - matches pico_sharpmem_display's own
// already-confirmed-working pinout exactly (see
// sharpdisp_init_freq_hz()'s hardcoded defaults in
// third_party/pico_sharpmem_display/include/sharpdisp/sharpdisp.h,
// vendored via quad_bringup/ - see this target's own CMakeLists.txt).
//
//   VIN  -> Pico VSYS (pin 39)
//   GND  -> Pico GND
//   CLK  -> PIN_LCD_SCK  below (SPI0 SCK)
//   DIN  -> PIN_LCD_MOSI below (SPI0 TX)
//   CS   -> PIN_LCD_CS   below
//   DISP -> tie high (3V3) - panel power-on/off pin, always-on
//
// No EXTCOMIN pin: VCOM inversion is toggled entirely in software by
// sharpdisp_refresh_vscroll(), piggybacked on the SPI command byte on
// every refresh - see main.c's periodic-refresh loop, which exists
// specifically to keep this happening even when idle.

#define PIN_LCD_CS    17
#define PIN_LCD_SCK   18
#define PIN_LCD_MOSI  19

#endif // QUAD_PINS_H
