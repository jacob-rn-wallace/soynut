#ifndef LCD_BRINGUP_PINS_H
#define LCD_BRINGUP_PINS_H

// Same physical wiring as firmware/pins.h's dormant direct-drive path:
//   Pico GP0 -> shifter B1 <-> shifter A1 -> LCD pin 4 (/CS)
//   Pico GP1 -> shifter B2 <-> shifter A2 -> LCD pin 5 (SID)
//   Pico GP2 -> shifter B3 <-> shifter A3 -> LCD pin 6 (SCLK)
// LCD must be jumpered for serial mode (J4 shorted / J3 open).

#define PIN_LCD_CS    0
#define PIN_LCD_SID   1
#define PIN_LCD_SCLK  2

#endif // LCD_BRINGUP_PINS_H
