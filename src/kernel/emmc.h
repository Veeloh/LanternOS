#pragma once
#include <stdint.h>
#include "pci.h"

int  emmc_init(pci_device_t* dev);
void emmc_read_sector(uint32_t lba, uint8_t* buf);
