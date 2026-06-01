#pragma once
#include <stdint.h>

typedef struct {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

void gdt_init();
