#pragma once
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

typedef enum {
	VGA_BLACK			= 0,
	VGA_BLUE			= 1,
	VGA_GREEN			= 2,
	VGA_CYAN			= 3,
	VGA_RED				= 4,
	VGA_MAGENTA			= 5,
	VGA_BROWN			= 6,
	VGA_LIGHT_GREY		= 7,
	VGA_DARK_GREY		= 8,
	VGA_LIGHT_BLUE		= 9,
	VGA_LIGHT_GREEN		= 10,
	VGA_LIGHT_CYAN		= 11,
	VGA_LIGHT_RED		= 12,
	VGA_LIGHT_MAGENTA	= 13, 
	VGA_YELLOW			= 14,
	VGA_WHITE			= 15,
} vga_colour;

void vga_init();
void vga_clear();
void vga_putchar(char c);
void vga_print(const char* str);
void vga_set_colour(vga_colour fg, vga_colour bg);
void vga_set_cursor(int x, int y);
