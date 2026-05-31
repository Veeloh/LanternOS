#include "keyboard.h"
#include "vga.h"
#include "idt.h"

#define KEYBOARD_PORT 0x60

char scancode_map[58] = {
    0,      // 0  - null
    0,      // 1  - escape
    '1',    // 2
    '2',    // 3
    '3',    // 4
    '4',    // 5
    '5',    // 6
    '6',    // 7
    '7',    // 8
    '8',    // 9
    '9',    // 10
    '0',    // 11
    '-',    // 12
    '=',    // 13
    '\b',      // 14 - backspace
    0,      // 15 - tab
    'q',    // 16
    'w',    // 17
    'e',    // 18
    'r',    // 19
    't',    // 20
    'y',    // 21
    'u',    // 22
    'i',    // 23
    'o',    // 24
    'p',    // 25
    '[',    // 26
    ']',    // 27
    '\n',   // 28 - enter
    0,      // 29 - ctrl
    'a',    // 30
    's',    // 31
    'd',    // 32
    'f',    // 33
    'g',    // 34
    'h',    // 35
    'j',    // 36
    'k',    // 37
    'l',    // 38
    ';',    // 39
    '\'',   // 40
    '`',    // 41
    0,      // 42 - left shift
    '\\',   // 43
    'z',    // 44
    'x',    // 45
    'c',    // 46
    'v',    // 47
    'b',    // 48
    'n',    // 49
    'm',    // 50
    ',',    // 51
    '.',    // 52
    '/',    // 53
    0,      // 54 - right shift
    '*',    // 55
    0,      // 56 - alt
    ' ',    // 57
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
	//test code to see if this is even firing
//	vga_set_colour(VGA_RED, VGA_BLACK);
//	vga_set_cursor(0,0);
//	vga_print("KEY!");
	uint8_t scancode = inb(KEYBOARD_PORT);

	if (scancode >= 0x80) {
		outb(0x20, 0x20);
		return;
	}
	
	vga_set_colour(VGA_WHITE, VGA_BLACK);

	
	char c = scancode_map[scancode];
	if (c) {
		if (c >= 32 && c <= 126 || c == '\b' || c == '\n') {
			last_char = c;
		}
	
		
	//	vga_putchar(c);
	} else {
		vga_putchar('?');
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
