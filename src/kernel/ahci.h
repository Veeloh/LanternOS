#pragma once
#include <stdint.h>
#include "pci.h"

int  ahci_init(pci_device_t* dev);
void ahci_read_sector(uint32_t lba, uint8_t* buf);
void ahci_write_sector(uint32_t lba, uint8_t* buf);
