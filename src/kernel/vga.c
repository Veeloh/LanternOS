#include "vga.h"

static uint16_t* const video = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_colour = 0x0F; //white on black :{

static uint16_t vga_entry(char c, uint8_t colour) {
	return (uint16_t)c | ((uint16_t)colour << 8);
}

void vga_set_colour(vga_colour fg, vga_colour bg) {
	current_colour = (bg << 4) | fg;
}

void vga_clear() {
	for (int y = 0; y < VGA_HEIGHT; y++)
		for (int x = 0; x < VGA_WIDTH; x++)
			video[y * VGA_WIDTH + x] = vga_entry(' ', current_colour);
	cursor_x = 0;
	cursor_y = 0;
}

static void vga_scroll() {
	for (int y = 0; y < VGA_HEIGHT - 1; y++)
		for (int x = 0; x < VGA_WIDTH; x++)
			video[y * VGA_WIDTH + x] = 	video[(y + 1) * VGA_WIDTH + x];

	for (int x = 0; x < VGA_WIDTH; x++)
		video[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', current_colour);

	cursor_y = VGA_HEIGHT - 1;
			
}

void vga_putchar(char c) {
	if (c == '\n') {
		cursor_x = 0;
		cursor_y++;
	} else if (c == '\r') {
		cursor_x = 0;
	} else if (c == '\b') {
		if (cursor_x > 0) {
			cursor_x--;
		}
	} else {
		video[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, current_colour);
		cursor_x++;
		if (cursor_x >= VGA_WIDTH) {
			cursor_x = 0;
			cursor_y++;
		}
	}

	if (cursor_y >= VGA_HEIGHT) {
		vga_scroll();
	}


	
	
}

void vga_print(const char* str) {
	while (*str)
		vga_putchar(*str++);
}


void vga_init() {
	vga_clear();
}

void vga_set_cursor(int x, int y) {
	cursor_x = x;
	cursor_y = y;
}

static void outb(uint16_t port, uint8_t value) {
	__asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

void vga_hide_cursor() {
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);
}
