#pragma once
#include <stdint.h>

typedef enum { DISK_NONE = 0, DISK_IDE, DISK_AHCI, DISK_NVME, DISK_EMMC } disk_driver_t;

// Scans PCI, picks whichever storage controller is actually present
// (AHCI > NVMe > legacy IDE), initializes it, returns which one won.
disk_driver_t disk_init(void);

// Reads one 512-byte sector, dispatching to whichever driver disk_init()
// picked. fat32.c should call this instead of touching hardware directly.
void disk_read_sector(uint32_t lba, uint8_t* buf);

// Writes one 512-byte sector, dispatching the same way. Caller is
// responsible for read-modify-write if it only wants to change part of
// a sector - this always writes the full 512 bytes from buf.
void disk_write_sector(uint32_t lba, uint8_t* buf);
