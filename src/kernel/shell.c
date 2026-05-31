#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"

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
		vga_print("	uptime - shows how long the systems been online\n");
	} else if (strcmp(cmd_buffer, "clear") == 0) {
		vga_clear();
		vga_set_colour(VGA_YELLOW, VGA_BLACK);
		vga_print("LanternOS Shell v0.1\n");
		vga_set_colour(VGA_WHITE, VGA_BLACK);
	} else if (strcmp(cmd_buffer, "shiggle") == 0) {
		vga_set_colour(VGA_GREEN, VGA_BLACK);
		vga_set_cursor(30, 1);
		vga_print("\n               #*%@%%##%%%%%%%%##*               ");
		vga_print("\n             ##%%#@%%%%%%%%%%%%%%#*              ");
		vga_print("\n           ##%%%%##%###**++===++#*####*          ");
		vga_print("\n          #%%%%###*#+===---:---===+*#%%#         ");
		vga_print("\n          #%%%%###*#+===---:---===+*#%%#         \n        #%%@#****#+=====-----:-==-==+#%%%   \n        ##%%******======-----:----===*%%%   \n        %%%#**#++========--==:----===*%%%       \n        %@%**###*+++++++=====++*+++==+%%%        \n         #%##**###%##**++***#%%%%****=#%@        \n         #@**%#*#@##%**=:+*%#*@++##+-*###       \n         #%+*###%*#%%#====*###*==+#+-=*%+         \n         #%***####**##*=-====++++===-*=#+        \n         %#**++==++*#**=-=+=++++====-=**-      \n         *%**+++****#*++=====++*#**++==--    \n          #***%#####**+==+**+=++**##+*+       \n           ##%%%########**++++*#%%#@*#*          \n           *#++*%%#####****#+=+-%+*++=           \n            **++#%*--:-:+.-.::.#++#+*            \n             ##++*#%*-::-::-*=**===+             \n               #***#**==#+---+*===               \n            %####***+#**++++++====+=*-           \n        %%*%#*@*##*****+++++=====*+==+==-:       ");	
	} else if (strcmp(cmd_buffer, "uptime") == 0) {
		uint32_t t = timer_get_ticks();
		vga_set_colour(VGA_WHITE, VGA_BLACK);
		vga_print("\nUptime: ");
		//print seconds (ticks / 100)
		uint32_t seconds = t / 100;
		char buf[16];
		int i = 0;
		if (seconds == 0) {
			buf[i++] = '0' + (seconds % 10);
			seconds /= 10;
		} else {
			while (seconds > 0) {
				buf[i++] = 0 + (seconds % 10);
				seconds /= 10;
			}

			//reverse
			int a, b;
			for (a = 0, b = i-1; a < b; a++, b--) {
				char tmp = buf[a];
				buf[a] = buf[b];
				buf[b] = tmp;
			}
		}
		buf[i] = 0;
		vga_print("\n");
		vga_print(buf);
		vga_print(" seconds.");
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
