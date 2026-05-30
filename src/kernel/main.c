#define VIDEO_MEMORY 0xB8000
#define WHITE_ON_BLACK 0x0F
#include "vga.h"

void print(const char* str) {
	unsigned char*video = (unsigned char*)VIDEO_MEMORY;

	while (*str) {
		*video++ = *str++; //character
		*video++ = WHITE_ON_BLACK; //colour atribute :P
		
	}
}

void kernel_main() {
	vga_init();
	vga_set_colour(VGA_YELLOW, VGA_BLACK);
	vga_set_cursor(30, 12);
	vga_print("Welcome to LanternOS");
	


	print("LanternOS kernel loaded!"); //finally works yippie

	while(1);
}
