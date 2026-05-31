#include "shell.h"
#include "vga.h"
#include "keyboard.h"

#define MAX_CMD_LEN 256

static char cmd_buffer[MAX_CMD_LEN];
static int cmd_len = 0;

static void shell_prompt() {
	vga_set_colour(VGA_WHITE, VGA_BLACK);
	vga_print("> ");
}

static int strcmp(const char* a, const char* b) {
	while(*a && *b && *a == *b) { a++; b++;}
	return *a - *b;
}

static void shell_execute() {
	cmd_buffer[cmd_len] = 0; //null terminate

	if (cmd_len == 0) {
		shell_prompt();
		return;
	}

	if (strcmp(cmd_buffer, "help") == 0) {
		vga_print("\nAvailable commands:\n");
		vga_print("	help - show this menu\n");
		vga_print("	clear - clear this screen\n");
	} else if (strcmp(cmd_buffer, "clear") == 0) {
		vga_clear();
		vga_set_colour(VGA_YELLOW, VGA_BLACK);
		vga_print("LanternOS Shell v0.1\n");
		vga_set_colour(VGA_WHITE, VGA_BLACK);
	} else {
		vga_print("\nUnknown command: ");
		vga_print(cmd_buffer);
	}

	vga_putchar('\n');
	shell_prompt();
	cmd_len = 0;
	
}


void shell_init() {
	vga_clear();
	vga_set_colour(VGA_YELLOW, VGA_BLACK);
	vga_print("LanternOS Shell v0.1\n");
	vga_set_colour(VGA_WHITE, VGA_BLACK);
	shell_prompt();
}

void shell_run() {
	char c = keyboard_getchar();
	if (c == 0) return;

	if (c == '\n') {
		vga_putchar('\n');
		shell_execute();
	} else if (c == '\b') {
		if (cmd_len > 0) {
			cmd_len--;
			//erase character on screen lmao
			vga_putchar('\b');
			vga_putchar(' ');
			vga_putchar('\b');
		}
	} else if (cmd_len < MAX_CMD_LEN - 1) {
		cmd_buffer[cmd_len++] = c;
		vga_putchar(c);
	}
}
