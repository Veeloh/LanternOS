#pragma once
#include <stdint.h>

#define PAGE_SIZE 4096

void pmm_init(uint32_t multiboot_addr);
uint32_t pmm_alloc_page();
void pmm_free_page(uint32_t addr);
uint32_t pmm_free_pages();
void pmm_reserve_region(uint32_t start_addr, uint32_t end_addr);
