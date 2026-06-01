#include "gdt.h"

static uint64_t gdt[3];
static gdt_ptr_t gdt_ptr;

extern void gdt_load(gdt_ptr_t* ptr);

static uint64_t make_entry(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
	uint64_t descriptor = 0;
	descriptor |= (uint64_t)(limit & 0xFFFF);
	descriptor |= (uint64_t)(base & 0xFFFFFF) << 16;
	descriptor |= (uint64_t)access << 40;
	descriptor |= (uint64_t)(limit >> 16) << 48;
	descriptor |= (uint64_t)(flags & 0xF) << 52;
	descriptor |= (uint64_t)(base >> 24) << 56;
	return descriptor;
}

void gdt_init() {
	gdt[0] = 0;
	gdt[1] = make_entry(0, 0xFFFFF, 0x9A, 0xC);
	gdt[2] = make_entry(0, 0xFFFFF, 0x92, 0xC);

	gdt_ptr.limit = sizeof(gdt) - 1;
	gdt_ptr.base = (uint32_t)gdt;

	gdt_load(&gdt_ptr);
}
