#include "keyboard.h"
#include "vga.h"
#include "idt.h"

#define KEYBOARD_PORT 0x60

static char scancode_map[] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
	0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 
	'*', 0, ' '
};

static char last_char = 0;

	
static uint8_t inb(uint16_t port) {
	uint8_t value;
	__asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
	return value;
}

static void outb(uint16_t port, uint8_t value) {
	__asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

void keyboard_handler() {
	uint8_t scancode = inb(KEYBOARD_PORT);

	//ignore all key releases gng twin son
	if (scancode >= 0x80) {
		//send eoi
		outb(0x20, 0x20);
		return;
	}

	if (scancode < sizeof(scancode_map) && scancode_map[scancode]) {
		last_char = scancode_map[scancode];
		vga_putchar(last_char);
	}

	//send eoi
	outb(0x20, 0x20);
}

char keyboard_getchar() {
	char c = last_char;
	last_char = 0;
	return c;
}

extern void keyboard_isr();

void keyboard_init() {
	last_char = 0;
	idt_set_handler(33, (uint32_t)keyboard_isr);
}
