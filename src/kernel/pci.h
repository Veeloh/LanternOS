#pragma once
#include <stdint.h>

typedef struct {
	uint8_t  bus, device, function;
	uint16_t vendor_id, device_id;
	uint8_t  class_code, subclass, prog_if;
	uint32_t bar[6];
} pci_device_t;

// Scans all buses/devices/functions, prints what it finds, and fills
// out_devices (up to max_devices). Returns how many devices were found.
int pci_scan(pci_device_t* out_devices, int max_devices);

// Convenience: search an already-scanned list for a device matching
// class/subclass (e.g. 0x01/0x06 = SATA AHCI, 0x01/0x08 = NVMe).
// Returns NULL if not found.
pci_device_t* pci_find(pci_device_t* devices, int count, uint8_t class_code, uint8_t subclass);
