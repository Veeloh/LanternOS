#include "pci.h"
#include "vga.h"

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC

static void outl(uint16_t port, uint32_t val) {
	__asm__ volatile ("outl %0, %1" :: "a"(val), "Nd"(port));
}
static uint32_t inl(uint16_t port) {
	uint32_t val;
	__asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
	uint32_t address = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
	                  | ((uint32_t)func << 8) | (offset & 0xFC);
	outl(CONFIG_ADDRESS, address);
	outl(CONFIG_DATA, value);
}


uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
	uint32_t address = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
	                  | ((uint32_t)func << 8) | (offset & 0xFC);
	outl(CONFIG_ADDRESS, address);
	return inl(CONFIG_DATA);
}

static uint16_t pci_vendor_id(uint8_t bus, uint8_t dev, uint8_t func) {
	return pci_config_read32(bus, dev, func, 0x00) & 0xFFFF;
}

static uint8_t pci_header_type(uint8_t bus, uint8_t dev, uint8_t func) {
	return (pci_config_read32(bus, dev, func, 0x0C) >> 16) & 0xFF;
}

static void print_hex8(uint8_t v) {
	const char* hex = "0123456789ABCDEF";
	char buf[3] = { hex[(v >> 4) & 0xF], hex[v & 0xF], 0 };
	vga_print(buf);
}
static void print_hex16(uint16_t v) {
	print_hex8((v >> 8) & 0xFF);
	print_hex8(v & 0xFF);
}

static void print_device(pci_device_t* d) {
	vga_print("\nPCI ");
	print_hex8(d->bus); vga_print(":");
	print_hex8(d->device); vga_print(":");
	print_hex8(d->function);
	vga_print(" vendor=");
	print_hex16(d->vendor_id);
	vga_print(" dev=");
	print_hex16(d->device_id);
	vga_print(" class=");
	print_hex8(d->class_code);
	vga_print(" sub=");
	print_hex8(d->subclass);
	vga_print(" progif=");
	print_hex8(d->prog_if);

	if (d->class_code == 0x01 && d->subclass == 0x06)
		vga_print("  <-- SATA/AHCI controller");
	if (d->class_code == 0x01 && d->subclass == 0x08)
		vga_print("  <-- NVMe controller");
	if (d->class_code == 0x01 && d->subclass == 0x01)
		vga_print("  <-- legacy IDE controller");
}

int pci_scan(pci_device_t* out_devices, int max_devices) {
	int count = 0;

	for (uint32_t bus = 0; bus < 256; bus++) {
		for (uint32_t dev = 0; dev < 32; dev++) {
			uint8_t max_func = 1;
			for (uint32_t func = 0; func < max_func; func++) {
				uint16_t vendor = pci_vendor_id(bus, dev, func);
				if (vendor == 0xFFFF) continue; // nothing here

				if (func == 0) {
					uint8_t htype = pci_header_type(bus, dev, func);
					if (htype & 0x80) max_func = 8; // multi-function device
				}

				uint32_t reg2 = pci_config_read32(bus, dev, func, 0x08);
				uint32_t reg0 = pci_config_read32(bus, dev, func, 0x00);

				if (count >= max_devices) return count;

				pci_device_t* d = &out_devices[count];
				d->bus = bus; d->device = dev; d->function = func;
				d->vendor_id = reg0 & 0xFFFF;
				d->device_id = (reg0 >> 16) & 0xFFFF;
				d->prog_if    = (reg2 >> 8) & 0xFF;
				d->subclass   = (reg2 >> 16) & 0xFF;
				d->class_code = (reg2 >> 24) & 0xFF;

				for (int b = 0; b < 6; b++)
					d->bar[b] = pci_config_read32(bus, dev, func, 0x10 + b * 4);

				print_device(d);
				count++;
			}
		}
	}
	return count;
}

pci_device_t* pci_find(pci_device_t* devices, int count, uint8_t class_code, uint8_t subclass) {
	for (int i = 0; i < count; i++)
		if (devices[i].class_code == class_code && devices[i].subclass == subclass)
			return &devices[i];
	return 0;
}


pci_device_t* pci_find_next(pci_device_t* devices, int count, uint8_t class_code, uint8_t subclass, int* search_idx) {
	for (int i = *search_idx; i < count; i++) {
		if (devices[i].class_code == class_code && devices[i].subclass == subclass) {
			*search_idx = i + 1;
			return &devices[i];
		}
	}
	*search_idx = count;
	return 0;
}
