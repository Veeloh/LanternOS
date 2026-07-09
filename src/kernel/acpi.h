#pragma once
#include <stdint.h>

// mb_info_addr = the Multiboot2 info pointer GRUB left in EBX (same value
// passed into kernel_main/vga_init). We prefer the RSDP tag GRUB gives us
// there - it works under both legacy BIOS and UEFI/OVMF. If that tag is
// missing for some reason we fall back to the old legacy-BIOS memory scan,
// which only works when booted via SeaBIOS (not OVMF).
int acpi_init(uint32_t mb_info_addr);

// Transitions the machine into S5 (soft-off) via the PM1 control
// register(s). Does not return on success. If acpi_init() was never
// called or failed, this does nothing.
void acpi_poweroff(void);
