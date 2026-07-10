#pragma once
#include <stdint.h>
#include "pci.h"

int  nvme_init(pci_device_t* dev);
void nvme_read_sector(uint32_t lba, uint8_t* buf);
void nvme_write_sector(uint32_t lba, uint8_t* buf);
