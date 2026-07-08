#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"
#include "clock.h"
#include "pmm.h"
#include "heap.h"
#include "process.h"
#include "fat32.h"
#include "syscall.h"
#include "elf.h"

void test_process() {
	int i = 0;
	while(1) {
		i++;
		if ( i > 10000000) {
			i = 0;
			vga_set_colour(VGA_RED, VGA_BLACK);
			vga_print("P1");
			vga_set_colour(VGA_WHITE, VGA_BLACK);
		}
	}
}

void kernel_main(unsigned int multiboot_addr) {
	vga_init((multiboot_info_t*)multiboot_addr);
	pmm_init(multiboot_addr);
	pmm_reserve_region(ELF_USER_MIN_ADDR, ELF_USER_MAX_ADDR);
	heap_init();
	fat32_init();
	process_init();
//	process_spawn(test_process);
	vga_set_colour(VGA_YELLOW, VGA_BLACK);
	vga_set_cursor(32, 12);
	vga_print("Welcome to SolOS");
	vga_set_cursor(38, 13);
	vga_print("v0.3");
	vga_set_colour(VGA_WHITE, VGA_BLACK);
	vga_set_cursor(27, 15);
	vga_print("Press any key to continue...");
	

	
	pic_remap();
	idt_init();
	syscall_init();
	vga_hide_cursor();
	extern char scancode_map[];
	vga_set_colour(VGA_GREEN, VGA_BLACK);
	vga_set_cursor(0, 1);
	if (scancode_map[16] == 'q') {
		vga_print("map ok");
	} else {
		vga_set_colour(VGA_RED, VGA_BLACK);
		vga_print("map bad");
	}
	
	keyboard_init();
	timer_init(100);
	clock_init(0,0,0);

	//enable interupts
	__asm__ volatile ("sti");

	vga_print("SolOS kernel loaded!"); //finally works yippie

	// trigger a software interrupt to test IDT (works great :P)
	//	__asm__ volatile ("int $0x80");


	//wait for keypress
	while(keyboard_getchar() == 0);


	//clear and show shell (old less cool and more used name D:)
//	vga_clear();
//	vga_set_colour(VGA_YELLOW, VGA_BLACK);
//	vga_set_cursor(0, 0);
//	vga_print("LanternOS Shell v0.1");
//	vga_set_colour(VGA_WHITE, VGA_BLACK);
//	vga_set_cursor(0, 1);
//	vga_print("> ");
	shell_init();
	clock_draw();

	while(1) {
		shell_run();
	}

}
