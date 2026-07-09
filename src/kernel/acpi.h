#pragma once
#include <stdint.h>

// Scans for the ACPI tables and finds the \_S5 sleep-state values needed
// to power off. Must be called once before acpi_poweroff(). Returns 1 on
// success, 0 if ACPI tables couldn't be found/parsed (caller should fall
// back to "just halt the CPU" in that case, e.g. a triple-fault-safe hlt
// loop, since there's no other portable way to cut power).
int acpi_init(void);

// Transitions the machine into S5 (soft-off) via the PM1 control
// register(s). Does not return on success. If acpi_init() was never
// called or failed, this does nothing.
void acpi_poweroff(void);
