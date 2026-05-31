#include "idt.h"

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

extern void idt_load(idt_ptr_t* ptr); //defined in idt_asm.asm

static void idt_set_entry(int n, uint32_t handler) {
	idt[n].base_low = handler & 0xFFFF;
	idt[n].selector = 0x08; //kernel code segment
	idt[n].zero = 0;
	idt[n].flags = 0x8E;
	idt[n].base_high = (handler >> 16) & 0xFFFF;
}

extern void isr_default();

void idt_init() {
	idt_ptr.limit = sizeof(idt) - 1;
	idt_ptr.base = (uint32_t)idt;

	//fill all 256 entries with defuakt handlre oh yeah baby
	for (int i = 0; i < 256; i++)
		idt_set_entry(i, (uint32_t)isr_default);

	idt_load(&idt_ptr);
}

void idt_set_handler(int n, uint32_t handler) {
	idt_set_entry(n, handler);
}
