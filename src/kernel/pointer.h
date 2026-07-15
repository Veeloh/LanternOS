#pragma once

// Single entry point for "whatever pointing device this machine has".
// Tries the Elan I2C touchpad first (real hardware trackpads are almost
// never behind PS/2 anymore); if that fails to ACK on the bus, falls back
// to the legacy PS/2 mouse driver. Callers (cursor.c) just use this API
// and never need to know which backend is active.

void pointer_init(void);

// Call once per main loop iteration (same place cursor_update() is
// called from). No-op for PS/2 (which is interrupt-driven); for the
// Elan backend this is what actually reads a report off the I2C bus.
void pointer_poll(void);

int pointer_get_x(void);
int pointer_get_y(void);
int pointer_left_pressed(void);
int pointer_right_pressed(void);
