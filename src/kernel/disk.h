#pragma once
#include <stdint.h>

typedef enum { DISK_NONE = 0, DISK_IDE, DISK_AHCI, DISK_NVME, DISK_EMMC } disk_driver_t;

// Scans PCI, picks whichever storage controller is actually present
// (AHCI > NVMe > legacy IDE), initializes it, returns which one won.
disk_driver_t disk_init(void);

// Reads one 512-byte sector, dispatching to whichever driver disk_init()
// picked. fat32.c should call this instead of touching hardware directly.
void disk_read_sector(uint32_t lba, uint8_t* buf);
