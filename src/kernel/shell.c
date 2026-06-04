#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "clock.h"
#include "pmm.h"
#include "heap.h"
#include "process.h"
#include "fat32.h"

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
		vga_print("	settime HH:MM:SS - set the clock\n");
		vga_print("	meminfo - displays info on memory usage\n");
		vga_print("	memtest - tests heaps and memory\n");
		vga_print("	ps - shows the process status'\n");
		vga_print("	ls - lists directory'\n");
		vga_print("	cat - reads files such as txt'\n");
	} else if (strcmp(cmd_buffer, "clear") == 0) {
		vga_clear();
		vga_set_colour(VGA_YELLOW, VGA_BLACK);
		vga_print("SolOS Shell v0.3\n");
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
				buf[i++] = '0' + (seconds % 10);
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
	} else if (cmd_buffer[0]=='s' && cmd_buffer[1]=='e' && cmd_buffer[2]=='t' && cmd_buffer[3]=='t' && cmd_buffer[4]=='i' && cmd_buffer[5]=='m' && cmd_buffer[6]=='e' && cmd_buffer[7]==' ') {
		//validate num
		if (cmd_buffer[8] < '0' || cmd_buffer[8] > '9' || cmd_buffer[9] < '0' || cmd_buffer[9] > '9' || cmd_buffer[11] < '0' || cmd_buffer[11] > '9' || cmd_buffer[12] < '0' || cmd_buffer[12] > '9' || cmd_buffer[14] < '0' || cmd_buffer[14] > '9' || cmd_buffer[15] < '0' || cmd_buffer[15] > '9' || cmd_buffer[10] != ':' || cmd_buffer[13] != ':') {
			vga_print("\nInvalid format, use: settime HH:MM:SS");
		} else {
			//parse HH:MM:SS starting at index 8
			uint8_t h = ((cmd_buffer[8]-'0') * 10) +  (cmd_buffer[9]-'0');
			uint8_t m = ((cmd_buffer[11]-'0') * 10) +  (cmd_buffer[12]-'0');
			uint8_t s = ((cmd_buffer[14]-'0') * 10) + (cmd_buffer[15]-'0');
			if ( h > 23 || m > 59 || s > 59) {
				vga_print("\nInvalid time. Hours 0-23, Minutes 0-59, Seconds 0-59.");
			} else {
				clock_set(h, m, s);
				clock_draw();
				vga_print("\nTime set.");
			}
		}
	} else if (strcmp(cmd_buffer, "meminfo") == 0) {
		uint32_t free = pmm_free_pages();
		uint32_t free_mb = (free * 4096) / (1024 * 1024);
		vga_print("\nFree memory: ");
		//print free_mb
		char buf[16];
		int i = 0;
		if (free_mb == 0) {
			buf[i++] = '0';
		} else {
			uint32_t tmp = free_mb;
			while (tmp > 0) {
				buf[i++] = '0' + (tmp % 10);
				tmp /= 10;
			}
			int a, b;
			for (a = 0, b = i-1; a < b; a++, b--) {
				char tmp2 = buf[a];
				buf[a] = buf[b];
				buf[b] = tmp2;
			}
		}
		buf[i] = 0;
		vga_print("\n");
		vga_print(buf);
		vga_print(" MB");
	} else if (strcmp(cmd_buffer, "memtest") == 0) {
		vga_print("\nTesting heap...");
		char* a = (char*)kmalloc(64);
		char* b = (char*)kmalloc(128);

		if (a && b) {
			vga_set_colour(VGA_WHITE, VGA_BLACK);
			vga_print("\nAllocated 2 blocks...");
			vga_set_colour(VGA_GREEN, VGA_BLACK);
			vga_print("OK");
			vga_set_colour(VGA_WHITE, VGA_BLACK);
			kfree(a);
			kfree(b);
			vga_print("\nFreed 2 blocks...");
			vga_set_colour(VGA_GREEN, VGA_BLACK);
			vga_print("OK");
			vga_set_colour(VGA_WHITE, VGA_BLACK);
			vga_print("\nHeap allocated and freed successfully!");
			vga_set_colour(VGA_WHITE, VGA_BLACK);
		} else {
			vga_set_colour(VGA_RED, VGA_BLACK);
			vga_print("\nHeap allocation failed, Try Again.");
			vga_set_colour(VGA_WHITE, VGA_BLACK);
		}
	} else if (strcmp(cmd_buffer, "ps") == 0) {
		vga_print("\nPID   STATE\n");
		for (int i = 0; i < MAX_PROCESSES; i++) {
			process_t* p = process_get_by_id(i);
			if (p->state != PROCESS_DEAD) {
				vga_print("\n");
				//print PID
				vga_putchar('0' + p->pid);
				vga_print("   ");
				if (p->state == PROCESS_RUNNING) {
					vga_set_colour(VGA_GREEN, VGA_BLACK);
					vga_print("RUNNING");
					vga_set_colour(VGA_WHITE, VGA_BLACK);
				} else if (p->state == PROCESS_READY) {
					vga_set_colour(VGA_LIGHT_MAGENTA, VGA_BLACK);
					vga_print("READY");
					vga_set_colour(VGA_WHITE, VGA_BLACK);
				}
				
			}
		}
	} else if (strcmp(cmd_buffer, "ls") == 0) {
		fat32_list_dir();
	} else if (cmd_buffer[0]=='c' && cmd_buffer[1]=='a' && cmd_buffer[2]=='t' && cmd_buffer[3]==' ') {
		uint8_t* buf = (uint8_t*)kmalloc(4096);
		int bytes = fat32_read_file(cmd_buffer + 4, buf, 4096);
		vga_print("\nLooking for: ");
		vga_print(cmd_buffer + 4);
		if (bytes < 0) {
			vga_print("\nFile not found!");
		} else {
			vga_putchar('\n');
			for (int i = 0; i < bytes; i++)
				vga_putchar(buf[i]);
		}
		kfree(buf);
	}
	else {
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
	vga_print("SolOS Shell v0.3\n");
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
