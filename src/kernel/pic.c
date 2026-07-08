#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

static void io_wait() {
	__asm__ volatile ("outb %%al, $0x80" :: "a"(0));
}

static void outb(uint16_t port, uint8_t value) {
	__asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
	uint8_t value;
	__asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
	return value;
}

void pic_remap() {
	// start init sequence
	outb(PIC1_COMMAND, 0x11);
	outb(PIC2_COMMAND, 0x11);

	// remap IRQ0-7 to interrupts 32-39
	// remap IRQ8-15 to interrupts 40-47
	outb(PIC1_DATA, 0x20);
	outb(PIC2_DATA, 0x28);

	// tell pics about eachother
	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);

	// 8086 mode
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);

	// explicitly set masks instead of trusting inherited firmware state:
	// unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade); mask the rest
	outb(PIC1_DATA, 0xF8); // 11111000
	outb(PIC2_DATA, 0xFF); // mask everything on slave for now
}
