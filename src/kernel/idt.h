#pragma once
#include <stdint.h>

typedef struct {
	uint16_t base_low; //lower 16 bits of handler address
	uint16_t selector; // code segment selector
	uint8_t zero; // always 0
	uint8_t flags; // type and attributes
	uint16_t base_high; //upper 16 bits of handler address
} __attribute__((packed)) idt_entry_t;

typedef struct {
	uint16_t limit; // size of idt - 1
	uint32_t base; //address of idt
} __attribute__((packed)) idt_ptr_t;

void idt_init();
