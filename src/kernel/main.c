#define VIDEO_MEMORY 0xB8000
#define WHITE_ON_BLACK 0x0F
#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"

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
	vga_set_cursor(38, 13);
	vga_print("v0.1");
	
	pic_remap();
	idt_init();
	keyboard_init();

	//enable interupts
	__asm__ volatile ("sti");

	print("LanternOS kernel loaded!"); //finally works yippie

	// trigger a software interrupt to test IDT (works great :P)
	//	__asm__ volatile ("int $0x80");

	while(1);
}
