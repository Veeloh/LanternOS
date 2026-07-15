#pragma once
#include <stdint.h>

// Elan I2C-HID touchpad driver.
// This machine: \_SB.I2CC.D015, _HID "ELAN0000", 7-bit I2C address 0x15,
// on the I2CC DesignWare controller (MMIO 0xFEDC4000).
//
// IMPORTANT: this talks to the touchpad by polling (called from the timer
// tick), not via its real interrupt. The real interrupt is a level-active-low
// GpioInt on \_SB.GPIO pin 5, which needs an AMD GPIO controller driver
// (AMD0030 @ 0xFED81500) we haven't written yet. Polling is slower/uses more
// CPU but requires no new driver to get first data flowing.

// Call once after i2c_dw_init() on the I2CC bus. Returns 0 on success.
int elan_init(void);

// Call periodically (e.g. once per timer tick). Reads one report if
// available and updates the internal x/y/button state.
// If dump_raw is non-zero, prints the raw report bytes to VGA for
// debugging - turn this on first to see real device output, since the
// exact bit layout below is a best-effort guess and likely needs tuning.
void elan_poll(int dump_raw);

// Raw touchpad units (0-4095, 12-bit) as reported by the hardware -
// not yet scaled to your screen resolution.
int elan_get_raw_x(void);
int elan_get_raw_y(void);

// Same position, scaled to [0, screen_w) / [0, screen_h) using
// vga_get_fb_width()/height() and ELAN_MAX_X/Y below. NOTE: ELAN_MAX_X/Y
// in elan_touchpad.c are a guess at this pad's physical raw-unit range -
// if the cursor doesn't reach the screen edges (or overshoots and clamps
// early), tune those two constants using your own min/max observed
// raw X/Y from moving your finger to each physical corner of the pad.
int elan_get_x(void);
int elan_get_y(void);

int elan_left_pressed(void);
int elan_right_pressed(void);
