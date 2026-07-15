#include "app.h"
#include "vga.h"
#include "heap.h"

// Same warm palette as window.c's chrome / taskbar.c's chip colours -
// window.c doesn't expose its WIN_* defines, so (like taskbar.c already
// does) we keep our own copy here rather than plumbing a shared header
// through for two colour constants.
#define APP_TEXT_COLOUR  0x4A2511   // WIN_TITLE_TEXT_COLOUR
#define APP_BG_COLOUR    0xFFF8DC   // WIN_BODY_COLOUR

// ---------------------------- shared helpers ----------------------------

// tiny uint->decimal-string helper - no snprintf in this freestanding build
static int uint_to_str(uint32_t v, char* out) {
	char tmp[10];
	int n = 0;
	if (v == 0) { out[0] = '0'; out[1] = 0; return 1; }
	while (v > 0 && n < 10) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
	for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
	out[n] = 0;
	return n;
}

// Draws `text` (len bytes, '\n' allowed) into a window's content area,
// word-wrapping at the window's width and showing only the tail once it
// overflows the available rows - i.e. basic terminal/log scrollback
// behaviour. Any app with a scrolling text buffer (terminal, editor,
// about panel) can reuse this instead of reinventing line layout.
#define APP_TEXT_MAX_LINES 64
static void draw_wrapped_text(window_t* win, const char* text, int len, uint32_t fg, uint32_t bg) {
	int cols = (win->w - 12) / 8;
	if (cols < 4) cols = 4;
	int rows = (win->h - TITLE_H - 8) / 16;
	if (rows < 1) rows = 1;

	int line_start[APP_TEXT_MAX_LINES];
	int line_len[APP_TEXT_MAX_LINES];
	int nlines = 0;

	int i = 0;
	while (i < len && nlines < APP_TEXT_MAX_LINES) {
		int start = i;
		int chars = 0;
		while (i < len && text[i] != '\n' && chars < cols) { i++; chars++; }
		line_start[nlines] = start;
		line_len[nlines] = i - start;
		nlines++;
		if (i < len && text[i] == '\n') i++; // consume the newline
	}

	int first = (nlines > rows) ? (nlines - rows) : 0;
	int y = win->y + TITLE_H + 4;
	for (int l = first; l < nlines; l++) {
		char line[80];
		int n = line_len[l];
		if (n > 79) n = 79;
		for (int k = 0; k < n; k++) line[k] = text[line_start[l] + k];
		line[n] = 0;
		vga_draw_text(win->x + 6, y, line, fg, bg);
		y += 16;
	}
}

// ------------------------------- terminal --------------------------------
// Real shell-command wiring is tomorrow's job (hook this into the same
// commands[] dispatcher shell.c already has). Tonight this just proves
// the pipeline: on_open allocates state, on_key appends typed characters,
// draw renders the scrolling buffer - so plugging in real command
// execution tomorrow is a drop-in swap of on_key's body.
#define TERMINAL_BUF_MAX 512
typedef struct {
	char buf[TERMINAL_BUF_MAX];
	int len;
} terminal_state_t;

static void terminal_on_open(window_t* win) {
	terminal_state_t* st = (terminal_state_t*)kmalloc(sizeof(terminal_state_t));
	if (!st) return;
	st->len = 0;
	const char* banner = "SolOS terminal (stub)\nreal command wiring: tomorrow\n> ";
	for (int i = 0; banner[i] && st->len < TERMINAL_BUF_MAX - 1; i++) st->buf[st->len++] = banner[i];
	win->app_state = st;
}

static void terminal_on_close(window_t* win) {
	if (win->app_state) kfree(win->app_state);
	win->app_state = 0;
}

static void terminal_draw(window_t* win) {
	terminal_state_t* st = (terminal_state_t*)win->app_state;
	if (!st) return;
	draw_wrapped_text(win, st->buf, st->len, APP_TEXT_COLOUR, APP_BG_COLOUR);
}

static void terminal_on_key(window_t* win, char c) {
	terminal_state_t* st = (terminal_state_t*)win->app_state;
	if (!st) return;
	if (c == '\b') {
		if (st->len > 0) st->len--;
	} else if (st->len < TERMINAL_BUF_MAX - 1 && ((c >= 32 && c <= 126) || c == '\n')) {
		st->buf[st->len++] = c;
	}
}

static const app_vtable_t terminal_vtable = {
	.on_open  = terminal_on_open,
	.on_close = terminal_on_close,
	.draw     = terminal_draw,
	.on_key   = terminal_on_key,
	.on_click = 0,
};

// --------------------------------- files ----------------------------------
// Placeholder for tonight. Tomorrow: call the new fat32_list_dir_entries()
// (fat32.h) into a small array in app_state, draw one row per entry, and
// use on_click's ly to figure out which row was clicked (cd/open it).
static void files_draw(window_t* win) {
	vga_draw_text(win->x + 6, win->y + TITLE_H + 6,  "Files - wire up tomorrow", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 22, "fat32_list_dir_entries()", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 38, "is ready in fat32.h", APP_TEXT_COLOUR, APP_BG_COLOUR);
}

static const app_vtable_t files_vtable = {
	.draw = files_draw,
};

// -------------------------------- settings --------------------------------
// Placeholder for tonight. Tomorrow: make the theme colours (currently
// #define'd in window.c/taskbar.c) into runtime variables an app can
// write to, plus wrap cmd_settime and acpi_poweroff for a power button.
static void settings_draw(window_t* win) {
	vga_draw_text(win->x + 6, win->y + TITLE_H + 6,  "Settings - wire up tomorrow", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 22, "theme + clock + power", APP_TEXT_COLOUR, APP_BG_COLOUR);
}

static const app_vtable_t settings_vtable = {
	.draw = settings_draw,
};

// ---------------------------------- about ----------------------------------
// Placeholder for tonight. Tomorrow: the real numbers already exist -
// pmm_free_pages() (used by cmd_meminfo) and process_get_by_id() (used by
// cmd_ps) in shell.c - just call them here instead of vga_print-ing them.
static void about_draw(window_t* win) {
	vga_draw_text(win->x + 6, win->y + TITLE_H + 6,  "About SolOS - wire up tomorrow", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 22, "meminfo/ps logic already", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 38, "exists in shell.c", APP_TEXT_COLOUR, APP_BG_COLOUR);
}

static const app_vtable_t about_vtable = {
	.draw = about_draw,
};

// -------------------------------- text edit --------------------------------
// Placeholder for tonight - but it can reuse terminal_state_t's pattern
// almost verbatim tomorrow (typed buffer + fat32_write_file on save),
// so no new plumbing needed here, just a dedicated vtable slot.
static void textedit_draw(window_t* win) {
	vga_draw_text(win->x + 6, win->y + TITLE_H + 6,  "Text Editor - wire up", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 22, "tomorrow (reuse the", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 38, "terminal's buffer + on_key)", APP_TEXT_COLOUR, APP_BG_COLOUR);
}

static const app_vtable_t textedit_vtable = {
	.draw = textedit_draw,
};

// -------------------------------- calculator --------------------------------
// Placeholder keypad for tonight, but on_click is wired for real so you
// can see click routing works end to end: it records exactly where inside
// the window you clicked. Tomorrow, swap this for real button hit-testing
// using the same lx/ly.
typedef struct {
	int last_lx, last_ly;
	int click_count;
} calculator_state_t;

static void calculator_on_open(window_t* win) {
	calculator_state_t* st = (calculator_state_t*)kmalloc(sizeof(calculator_state_t));
	if (!st) return;
	st->last_lx = -1;
	st->last_ly = -1;
	st->click_count = 0;
	win->app_state = st;
}

static void calculator_on_close(window_t* win) {
	if (win->app_state) kfree(win->app_state);
	win->app_state = 0;
}

static void calculator_on_click(window_t* win, int lx, int ly) {
	calculator_state_t* st = (calculator_state_t*)win->app_state;
	if (!st) return;
	st->last_lx = lx;
	st->last_ly = ly;
	st->click_count++;
}

static void calculator_draw(window_t* win) {
	vga_draw_text(win->x + 6, win->y + TITLE_H + 6,  "Calculator - wire up", APP_TEXT_COLOUR, APP_BG_COLOUR);
	vga_draw_text(win->x + 6, win->y + TITLE_H + 22, "a keypad here tomorrow", APP_TEXT_COLOUR, APP_BG_COLOUR);

	calculator_state_t* st = (calculator_state_t*)win->app_state;
	if (st && st->click_count > 0) {
		char line[48];
		int p = 0;
		const char* prefix = "clicks: ";
		for (int i = 0; prefix[i]; i++) line[p++] = prefix[i];
		char num[10];
		int n = uint_to_str((uint32_t)st->click_count, num);
		for (int i = 0; i < n; i++) line[p++] = num[i];
		const char* mid = "  at (";
		for (int i = 0; mid[i]; i++) line[p++] = mid[i];
		n = uint_to_str((uint32_t)(st->last_lx < 0 ? 0 : st->last_lx), num);
		for (int i = 0; i < n; i++) line[p++] = num[i];
		line[p++] = ',';
		n = uint_to_str((uint32_t)(st->last_ly < 0 ? 0 : st->last_ly), num);
		for (int i = 0; i < n; i++) line[p++] = num[i];
		line[p++] = ')';
		line[p] = 0;
		vga_draw_text(win->x + 6, win->y + TITLE_H + 40, line, APP_TEXT_COLOUR, APP_BG_COLOUR);
	}
}

static const app_vtable_t calculator_vtable = {
	.on_open  = calculator_on_open,
	.on_close = calculator_on_close,
	.draw     = calculator_draw,
	.on_click = calculator_on_click,
};

// -------------------------------- registry ----------------------------------

const app_vtable_t* app_get_vtable(app_type_t type) {
	switch (type) {
		case APP_TERMINAL:   return &terminal_vtable;
		case APP_FILES:      return &files_vtable;
		case APP_SETTINGS:   return &settings_vtable;
		case APP_ABOUT:      return &about_vtable;
		case APP_TEXTEDIT:   return &textedit_vtable;
		case APP_CALCULATOR: return &calculator_vtable;
		case APP_NONE:
		default:
			return 0;
	}
}
