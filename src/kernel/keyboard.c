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


static char scancode_map_shift[58] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,
    0, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static int shift_pressed = 0;

static char last_char = 0;

	
static uint8_t inb(uint16_t port) {
	uint8_t value;
	__asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
	return value;
}

static void outb(uint16_t port, uint8_t value) {
	__asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static int ctrl_pressed = 0;
static int extended = 0;

void keyboard_handler() {
	uint8_t scancode = inb(KEYBOARD_PORT);

	if (scancode == 0xE0) { extended = 1; outb(0x20, 0x20); return; }

	if (scancode == 0x1D) { ctrl_pressed = 1; outb(0x20, 0x20); return; }
	if (scancode == 0x9D) { ctrl_pressed = 0; outb(0x20, 0x20); return; }

	if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; outb(0x20, 0x20); return; }
	if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; outb(0x20, 0x20); return; }

	if (extended) {
		extended = 0;
		if (!(scancode & 0x80)) { // ignore key-release half
			switch (scancode) {
				case 0x48: last_char = KEY_UP;    break;
				case 0x50: last_char = KEY_DOWN;  break;
				case 0x4B: last_char = KEY_LEFT;  break;
				case 0x4D: last_char = KEY_RIGHT; break;
			}
		}
		outb(0x20, 0x20);
		return;
	}

	if (scancode >= 0x80) { outb(0x20, 0x20); return; } // release

	if (ctrl_pressed && scancode == 0x2E) { // 'c'
		last_char = KEY_CTRL_C;
		outb(0x20, 0x20);
		return;
	}

	char c = shift_pressed ? scancode_map_shift[scancode] : scancode_map[scancode];
	if (c && ((c >= 32 && c <= 126) || c == '\b' || c == '\n')) last_char = c;

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
