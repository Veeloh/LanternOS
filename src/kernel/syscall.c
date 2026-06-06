#include "syscall.h"
#include "vga.h"
#include "keyboard.h"
#include "process.h"
#include "idt.h"

extern void syscall_isr();

static void  sys_exit(int code) {
	process_t* p = process_get_current();
	p->state = PROCESS_DEAD;
	//halt for now
	while(1);
}

static void sys_write(const char* str) {
	vga_print(str);
}

static char sys_read() {
	char c = 0;
	while (c == 0)
		c = keyboard_getchar();
	return c;
}

void syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
	switch (eax) {
		case SYS_EXIT:
			sys_exit(ebx);
			break;
		case SYS_WRITE:
			sys_write((const char*)ebx);
			break;
		case SYS_READ:
		// handled in isr
			break;
		default: vga_print("\nUnknown syscall!");
		break;
	}
}

void syscall_init() {
	idt_set_handler(0x80, (uint32_t)syscall_isr);
}
