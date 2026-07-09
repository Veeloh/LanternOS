#include "disk.h"
#include "pci.h"
#include "ahci.h"
#include "nvme.h"
#include "vga.h"

#define SECTOR_SIZE 512
#define ATA_DATA    0x1F0
#define ATA_SECTOR  0x1F3
#define ATA_LCYL    0x1F4
#define ATA_HCYL    0x1F5
#define ATA_HEAD    0x1F6
#define ATA_CMD     0x1F7
#define ATA_STATUS  0x1F7
#define ATA_USE_SLAVE 0 // 0 = master. flip to 1 for the old two-image QEMU setup.

static void outb(uint16_t port, uint8_t val) { __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static void inw_buffer(uint16_t port, uint16_t* buf, uint32_t count) {
	__asm__ volatile ("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}

static void ide_read_sector(uint32_t lba, uint8_t* buf) {
	while (inb(ATA_STATUS) & 0x80);
	outb(ATA_HEAD, 0xE0 | (ATA_USE_SLAVE << 4) | ((lba >> 24) & 0x0F));
	outb(0x1F2, 1);
	outb(ATA_SECTOR, lba & 0xFF);
	outb(ATA_LCYL,   (lba >> 8) & 0xFF);
	outb(ATA_HCYL,   (lba >> 16) & 0xFF);
	outb(ATA_CMD,    0x20);
	while (!(inb(ATA_STATUS) & 0x08));
	inw_buffer(ATA_DATA, (uint16_t*)buf, SECTOR_SIZE / 2);
}

static disk_driver_t active_driver = DISK_NONE;
static pci_device_t pci_devices[64];

disk_driver_t disk_init(void) {
	int count = pci_scan(pci_devices, 64);

	pci_device_t* ahci_dev = pci_find(pci_devices, count, 0x01, 0x06);
	pci_device_t* nvme_dev = pci_find(pci_devices, count, 0x01, 0x08);
	pci_device_t* ide_dev  = pci_find(pci_devices, count, 0x01, 0x01);

	if (ahci_dev) {
		vga_print("\ndisk: AHCI controller found, initializing...");
		if (ahci_init(ahci_dev)) {
			vga_print("\ndisk: using AHCI driver");
			active_driver = DISK_AHCI;
			return active_driver;
		}
		vga_print("\ndisk: AHCI init failed, trying other options");
	}

	if (nvme_dev) {
		vga_print("\ndisk: NVMe controller found, initializing...");
		if (nvme_init(nvme_dev)) {
			vga_print("\ndisk: using NVMe driver");
			active_driver = DISK_NVME;
			return active_driver;
		}
		vga_print("\ndisk: NVMe init failed, trying other options");
	}

	if (ide_dev) {
		vga_print("\ndisk: using legacy IDE driver");
		active_driver = DISK_IDE;
		return active_driver;
	}

	vga_print("\ndisk: WARNING - no supported storage controller found!");
	active_driver = DISK_NONE;
	return active_driver;
}

void disk_read_sector(uint32_t lba, uint8_t* buf) {
	switch (active_driver) {
		case DISK_AHCI: ahci_read_sector(lba, buf); return;
		case DISK_NVME: nvme_read_sector(lba, buf); return;
		case DISK_IDE:  ide_read_sector(lba, buf);  return;
		default: return;
	}
}
