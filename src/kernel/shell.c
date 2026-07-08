#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "clock.h"
#include "pmm.h"
#include "heap.h"
#include "process.h"
#include "fat32.h"
#include "elf.h"

#define MAX_CMD_LEN 256
#define MAX_ARGS 16

static char cmd_buffer[MAX_CMD_LEN];
static int cmd_len = 0;

#define MAX_HISTORY 16

static int cursor_pos = 0;
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_nav = -1;
static char saved_line[MAX_CMD_LEN];

static void cursor_left()  { if (cursor_pos > 0) { vga_putchar('\b'); cursor_pos--; } }
static void cursor_right() { if (cursor_pos < cmd_len) { vga_putchar(cmd_buffer[cursor_pos]); cursor_pos++; } }

static void insert_char(char c) {
	if (cmd_len >= MAX_CMD_LEN - 1) return;
	int cp = cursor_pos;
	for (int i = cmd_len; i > cp; i--) cmd_buffer[i] = cmd_buffer[i - 1];
	cmd_buffer[cp] = c;
	cmd_len++;
	for (int i = cp; i < cmd_len; i++) vga_putchar(cmd_buffer[i]);
	int back = cmd_len - (cp + 1);
	for (int i = 0; i < back; i++) vga_putchar('\b');
	cursor_pos = cp + 1;
}

static void delete_char_before_cursor() {
	if (cursor_pos == 0) return;
	for (int i = cursor_pos - 1; i < cmd_len - 1; i++) cmd_buffer[i] = cmd_buffer[i + 1];
	cmd_len--;
	cursor_pos--;
	vga_putchar('\b');
	for (int i = cursor_pos; i < cmd_len; i++) vga_putchar(cmd_buffer[i]);
	vga_putchar(' '); // erase old trailing char
	for (int i = 0; i < cmd_len - cursor_pos + 1; i++) vga_putchar('\b');
}

static void clear_line_display() {
	while (cursor_pos < cmd_len) cursor_right();
	while (cmd_len > 0) {
		vga_putchar('\b'); vga_putchar(' '); vga_putchar('\b');
		cmd_len--;
	}
	cursor_pos = 0;
}

static void load_line(const char* s) {
	clear_line_display();
	int i = 0;
	while (s[i] && i < MAX_CMD_LEN - 1) { cmd_buffer[i] = s[i]; vga_putchar(s[i]); i++; }
	cmd_len = i;
	cursor_pos = i;
}

static void history_add(const char* line) {
	if (line[0] == 0) return;
	if (history_count == MAX_HISTORY) {
		for (int i = 1; i < MAX_HISTORY; i++)
			for (int j = 0; j < MAX_CMD_LEN; j++) history[i - 1][j] = history[i][j];
		history_count--;
	}
	int i = 0;
	while (line[i] && i < MAX_CMD_LEN - 1) { history[history_count][i] = line[i]; i++; }
	history[history_count][i] = 0;
	history_count++;
}

static void shell_prompt() {
	vga_set_colour(VGA_WHITE, VGA_BLACK);
	vga_print("> ");
}

static int strcmp(const char* a, const char* b) {
	while(*a && *b && *a == *b) { a++; b++;}
	return *a - *b;
}

// prints an unsigned int in decimal - shared by uptime/meminfo instead of
// each hand-rolling their own reverse-digit loop
static void print_uint(uint32_t value) {
	char buf[16];
	int i = 0;
	if (value == 0) {
		buf[i++] = '0';
	} else {
		while (value > 0) {
			buf[i++] = '0' + (value % 10);
			value /= 10;
		}
		for (int a = 0, b = i - 1; a < b; a++, b--) {
			char tmp = buf[a];
			buf[a] = buf[b];
			buf[b] = tmp;
		}
	}
	buf[i] = 0;
	vga_print(buf);
}

// ---- tokenizer -----------------------------------------------------
//
// Splits cmd_buffer in place into argv[] (spaces become '\0'). This is
// what every command handler now gets called with, instead of digging
// fixed offsets out of the raw line - "cat " assuming the filename
// always starts at cmd_buffer+4 broke the moment a command name wasn't
// exactly 3 letters.
static int tokenize(char* line, char* argv[], int max_args) {
	int argc = 0;
	char* p = line;

	while (*p && argc < max_args) {
		while (*p == ' ') p++; // skip leading/extra spaces
		if (!*p) break;

		argv[argc++] = p;

		while (*p && *p != ' ') p++;
		if (*p) {
			*p = 0;
			p++;
		}
	}

	return argc;
}

// ---- command handlers ------------------------------------------------
// every command gets (argc, argv) - argv[0] is the command name itself,
// same convention as a normal argv.

static void cmd_help(int argc, char** argv);

static void cmd_clear(int argc, char** argv) {
	(void)argc; (void)argv;
	vga_clear();
	vga_set_colour(VGA_YELLOW, VGA_BLACK);
	vga_print("SolOS Shell v0.3\n");
	vga_set_colour(VGA_WHITE, VGA_BLACK);
}

static void cmd_shiggle(int argc, char** argv) {
	(void)argc; (void)argv;
	vga_set_colour(VGA_GREEN, VGA_BLACK);
	vga_set_cursor(30, 1);
	vga_print("\n               #*%@%%##%%%%%%%%##*               ");
	vga_print("\n             ##%%#@%%%%%%%%%%%%%%#*              ");
	vga_print("\n           ##%%%%##%###**++===++#*####*          ");
	vga_print("\n          #%%%%###*#+===---:---===+*#%%#         ");
	vga_print("\n          #%%%%###*#+===---:---===+*#%%#         \n        #%%@#****#+=====-----:-==-==+#%%%   \n        ##%%******======-----:----===*%%%   \n        %%%#**#++========--==:----===*%%%       \n        %@%**###*+++++++=====++*+++==+%%%        \n         #%##**###%##**++***#%%%%****=#%@        \n         #@**%#*#@##%**=:+*%#*@++##+-*###       \n         #%+*###%*#%%#====*###*==+#+-=*%+         \n         #%***####**##*=-====++++===-*=#+        \n         %#**++==++*#**=-=+=++++====-=**-      \n         *%**+++****#*++=====++*#**++==--    \n          #***%#####**+==+**+=++**##+*+       \n           ##%%%########**++++*#%%#@*#*          \n           *#++*%%#####****#+=+-%+*++=           \n            **++#%*--:-:+.-.::.#++#+*            \n             ##++*#%*-::-::-*=**===+             \n               #***#**==#+---+*===               \n            %####***+#**++++++====+=*-           \n        %%*%#*@*##*****+++++=====*+==+==-:       ");
}

static void cmd_uptime(int argc, char** argv) {
	(void)argc; (void)argv;
	uint32_t t = timer_get_ticks();
	vga_set_colour(VGA_WHITE, VGA_BLACK);
	vga_print("\nUptime: ");
	print_uint(t / 100);
	vga_print(" seconds.");
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static void cmd_settime(int argc, char** argv) {
	// expects argv[1] == "HH:MM:SS"
	if (argc < 2) {
		vga_print("\nUsage: settime HH:MM:SS");
		return;
	}

	char* t = argv[1];
	if (!is_digit(t[0]) || !is_digit(t[1]) || t[2] != ':' ||
	    !is_digit(t[3]) || !is_digit(t[4]) || t[5] != ':' ||
	    !is_digit(t[6]) || !is_digit(t[7]) || t[8] != 0) {
		vga_print("\nInvalid format, use: settime HH:MM:SS");
		return;
	}

	uint8_t h = (t[0]-'0') * 10 + (t[1]-'0');
	uint8_t m = (t[3]-'0') * 10 + (t[4]-'0');
	uint8_t s = (t[6]-'0') * 10 + (t[7]-'0');

	if (h > 23 || m > 59 || s > 59) {
		vga_print("\nInvalid time. Hours 0-23, Minutes 0-59, Seconds 0-59.");
		return;
	}

	clock_set(h, m, s);
	clock_draw();
	vga_print("\nTime set.");
}

static void cmd_meminfo(int argc, char** argv) {
	(void)argc; (void)argv;
	uint32_t free = pmm_free_pages();
	uint32_t free_mb = (free * 4096) / (1024 * 1024);
	vga_print("\nFree memory: ");
	print_uint(free_mb);
	vga_print(" MB");
}

static void cmd_memtest(int argc, char** argv) {
	(void)argc; (void)argv;
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
}

static void cmd_ps(int argc, char** argv) {
	(void)argc; (void)argv;
	vga_print("\nPID   STATE\n");
	for (int i = 0; i < MAX_PROCESSES; i++) {
		process_t* p = process_get_by_id(i);
		if (p->state != PROCESS_DEAD) {
			vga_print("\n");
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
}

static void cmd_ls(int argc, char** argv) {
	(void)argc; (void)argv;
	fat32_list_dir();
}

static void cmd_cat(int argc, char** argv) {
	if (argc < 2) {
		vga_print("\nUsage: cat <file>");
		return;
	}

	uint8_t* buf = (uint8_t*)kmalloc(4096);
	int bytes = fat32_read_file(argv[1], buf, 4096);
	vga_print("\nLooking for: ");
	vga_print(argv[1]);
	if (bytes < 0) {
		vga_print("\nFile not found!");
	} else {
		vga_putchar('\n');
		for (int i = 0; i < bytes; i++)
			vga_putchar(buf[i]);
	}
	kfree(buf);
}

static void cmd_syscalltest(int argc, char** argv) {
	(void)argc; (void)argv;
	vga_print("\n Testing syscall...");
	char* msg = "syscall works.";
	__asm__ volatile (
		"int $0x80"
		:: "a"(1), "b"(msg)
	);
}

static void cmd_run(int argc, char** argv) {
	if (argc < 2) {
		vga_print("\nUsage: run <file>");
		return;
	}
	elf_exec(argv[1]);
}

// command table - add a new command by adding one line here, no need to
// touch shell_execute() itself.
typedef void (*shell_cmd_fn)(int argc, char** argv);

typedef struct {
	const char* name;
	const char* help;
	shell_cmd_fn fn;
} shell_command_t;

static shell_command_t commands[] = {
	{ "help",        "show this menu",                                cmd_help },
	{ "clear",       "clear this screen",                             cmd_clear },
	{ "uptime",      "shows how long the systems been online",        cmd_uptime },
	{ "settime",     "settime HH:MM:SS - set the clock",              cmd_settime },
	{ "meminfo",     "displays info on memory usage",                 cmd_meminfo },
	{ "memtest",     "tests heaps and memory",                        cmd_memtest },
	{ "ps",          "shows the process status",                      cmd_ps },
	{ "ls",          "lists directory",                               cmd_ls },
	{ "cat",         "cat <file> - reads a file",                     cmd_cat },
	{ "syscalltest", "tests syscalls (mostly for dev use)",           cmd_syscalltest },
	{ "run",         "run <file> - loads and runs an ELF executable", cmd_run },
	{ "shiggle",     0 /* easter egg, not shown in help */,           cmd_shiggle },
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(int argc, char** argv) {
	(void)argc; (void)argv;
	vga_print("\nAvailable commands:\n");
	for (unsigned int i = 0; i < NUM_COMMANDS; i++) {
		if (!commands[i].help) continue; // hidden/easter-egg commands
		vga_print("	");
		vga_print(commands[i].name);
		vga_print(" - ");
		vga_print(commands[i].help);
		vga_print("\n");
	}
}

static void shell_execute() {
	cmd_buffer[cmd_len] = 0; //null terminate

	if (cmd_len == 0) {
		shell_prompt();
		return;
	}

	char* argv[MAX_ARGS];
	int argc = tokenize(cmd_buffer, argv, MAX_ARGS);

	if (argc == 0) {
		shell_prompt();
		cmd_len = 0;
		return;
	}

	int found = 0;
	for (unsigned int i = 0; i < NUM_COMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0) {
			commands[i].fn(argc, argv);
			found = 1;
			break;
		}
	}

	if (!found) {
		vga_print("\nUnknown command: ");
		vga_print(argv[0]);
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

	switch (c) {
	case '\n':
		vga_putchar('\n');
		history_add(cmd_buffer);
		history_nav = -1;
		shell_execute();
		return;
	case '\b':
		delete_char_before_cursor();
		return;
	case KEY_LEFT:  cursor_left();  return;
	case KEY_RIGHT: cursor_right(); return;
	case KEY_CTRL_C:
		vga_print("^C\n");
		cmd_len = 0; cursor_pos = 0; history_nav = -1;
		shell_prompt();
		return;
	case KEY_UP:
		if (history_count == 0) return;
		if (history_nav == -1) {
			int i = 0;
			while (i < cmd_len) { saved_line[i] = cmd_buffer[i]; i++; }
			saved_line[i] = 0;
			history_nav = history_count;
		}
		if (history_nav > 0) load_line(history[--history_nav]);
		return;
	case KEY_DOWN:
		if (history_nav == -1) return;
		history_nav++;
		if (history_nav >= history_count) { history_nav = -1; load_line(saved_line); }
		else load_line(history[history_nav]);
		return;
	default:
		insert_char(c);
		return;
	}
}
