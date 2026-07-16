#include "app.h"
#include "vga.h"
#include "heap.h"
#include "shell.h"
#include "fat32.h"
#include "keyboard.h"
#include "timer.h"
#include "window.h"
#include "menubar.h"

// Same warm palette as window.c's chrome / taskbar.c's chip colours -
// window.c doesn't expose its WIN_* defines, so (like taskbar.c already
// does) we keep our own copy here rather than plumbing a shared header
// through for two colour constants.
#define APP_TEXT_COLOUR      0x4A2511   // WIN_TITLE_TEXT_COLOUR
#define APP_BG_COLOUR        0xFFF8DC   // WIN_BODY_COLOUR
#define APP_HIGHLIGHT_COLOUR 0xF39C12   // WIN_TITLE_COLOUR - selected-row highlight

// forward decl - files_on_click (below) opens a double-clicked file into a
// Text Edit window; the real definition lives down in the text-edit section
// since it needs that app's own state layout.
static void textedit_load_file(window_t* win, const char* filename);

// ---------------------------- shared helpers ----------------------------

// tiny strlen/strcpy - no libc in this freestanding build
static int str_len(const char* s) {
	int n = 0;
	while (s[n]) n++;
	return n;
}

static void str_copy(char* dst, const char* src, int dst_max) {
	int i = 0;
	while (src[i] && i < dst_max - 1) { dst[i] = src[i]; i++; }
	dst[i] = 0;
}

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

// Word-wraps `text` (len bytes, '\n' allowed) at `cols` characters per line,
// filling line_start[]/line_len[] (each sized APP_TEXT_MAX_LINES) and
// returning how many lines it produced. Shared by draw_wrapped_text and
// the terminal's scrollbar layout so line-splitting can never drift
// between what's drawn and what the scrollbar's geometry is based on.
#define APP_TEXT_MAX_LINES 64
static int wrap_text_lines(int cols, const char* text, int len, int* line_start, int* line_len) {
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
	return nlines;
}

// Draws `text` (len bytes, '\n' allowed) into a window's content area,
// word-wrapping at the window's width. scroll_offset is how many lines
// back from the latest the view is scrolled (0 = pinned to the newest
// output/tail, i.e. the original always-show-the-tail behaviour every
// existing caller still gets by passing 0). Any app with a scrolling
// text buffer (terminal, editor, about panel) can reuse this instead of
// reinventing line layout.
static void draw_wrapped_text(window_t* win, const char* text, int len, uint32_t fg, uint32_t bg, int scroll_offset) {
	int cols = (win->w - 12) / 8;
	if (cols < 4) cols = 4;
	int rows = (win->h - TITLE_H - 8) / 16;
	if (rows < 1) rows = 1;

	int line_start[APP_TEXT_MAX_LINES];
	int line_len[APP_TEXT_MAX_LINES];
	int nlines = wrap_text_lines(cols, text, len, line_start, line_len);

	int max_offset = (nlines > rows) ? (nlines - rows) : 0;
	if (scroll_offset > max_offset) scroll_offset = max_offset;
	if (scroll_offset < 0) scroll_offset = 0;

	int last = nlines - scroll_offset;
	int first = (last > rows) ? (last - rows) : 0;
	int y = win->y + TITLE_H + 4;
	for (int l = first; l < last; l++) {
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
// Real command wiring: on Enter, whatever's been typed since the last
// prompt gets handed to shell_exec_line() (shell.h), which tokenizes it
// and dispatches through the exact same commands[] table the fullscreen
// console uses - the output gets captured straight into this window's own
// scrollback buffer instead of the real screen. Everything else (typing,
// backspace, scrollback rendering) is unchanged from the stub.
#define TERMINAL_BUF_MAX   1024
#define TERMINAL_LINE_MAX  128
#define TERMINAL_CAP_MAX   512
typedef struct {
	char buf[TERMINAL_BUF_MAX];
	int len;
	int input_start;    // index in buf where the line currently being typed begins
	int scroll_offset;  // wrapped lines scrolled back from the tail; 0 = pinned to latest output
} terminal_state_t;

// scrollbar - a fixed-width strip reserved along the right edge of the
// content area. No drag yet: window.c only forwards discrete clicks to
// apps (see app.h's on_click doc), not held-mouse motion, so this is a
// classic click-the-track-to-page scrollbar rather than a draggable one.
#define TERM_SCROLLBAR_W       8
#define TERM_SCROLLBAR_MARGIN  2
#define TERM_SCROLLBAR_TRACK_COLOUR 0xF3D9A0
#define TERM_SCROLLBAR_THUMB_COLOUR 0x6E2C00

// Appends `text` (len bytes) to the scrollback, dropping the oldest bytes
// first if it would overflow TERMINAL_BUF_MAX - keeps the buffer a rolling
// window instead of just refusing to append once full (which would make a
// long-running terminal go silent). input_start is shifted to match so the
// "where does the current typed line start" bookkeeping stays correct.
static void terminal_append(terminal_state_t* st, const char* text, int len) {
	if (len <= 0) return;
	if (len >= TERMINAL_BUF_MAX) {
		text += (len - (TERMINAL_BUF_MAX - 1));
		len = TERMINAL_BUF_MAX - 1;
	}
	if (st->len + len > TERMINAL_BUF_MAX - 1) {
		int drop = st->len + len - (TERMINAL_BUF_MAX - 1);
		for (int i = drop; i < st->len; i++) st->buf[i - drop] = st->buf[i];
		st->len -= drop;
		st->input_start -= drop;
		if (st->input_start < 0) st->input_start = 0;
	}
	for (int i = 0; i < len; i++) st->buf[st->len++] = text[i];
}

static void terminal_print_prompt(terminal_state_t* st) {
	const char* cwd = fat32_get_cwd();
	terminal_append(st, cwd, str_len(cwd));
	terminal_append(st, "> ", 2);
	st->input_start = st->len;
}

static void terminal_on_open(window_t* win) {
	terminal_state_t* st = (terminal_state_t*)kmalloc(sizeof(terminal_state_t));
	if (!st) return;
	st->len = 0;
	st->input_start = 0;
	st->scroll_offset = 0;
	const char* banner = "SolOS terminal\n";
	terminal_append(st, banner, str_len(banner));
	terminal_print_prompt(st);
	win->app_state = st;
}

static void terminal_on_close(window_t* win) {
	if (win->app_state) kfree(win->app_state);
	win->app_state = 0;
}

// Scrollback + scrollbar geometry, computed once and shared by the draw
// and click handlers so they can never disagree about where the thumb
// is - same shared-bounding-box pattern window.c's close button uses.
// Coordinates are absolute (screen) to match draw_wrapped_text's own
// win->x/win->y-relative drawing.
typedef struct {
	int rows;                              // visible text rows
	int max_offset;                        // largest valid scroll_offset (0 = everything fits, nothing to scroll)
	int track_x, track_y, track_w, track_h;
	int thumb_y, thumb_h;
} terminal_scrollbar_t;

static void terminal_layout(window_t* win, terminal_state_t* st, terminal_scrollbar_t* sb) {
	int cols = (win->w - 12 - TERM_SCROLLBAR_W - TERM_SCROLLBAR_MARGIN) / 8;
	if (cols < 4) cols = 4;
	sb->rows = (win->h - TITLE_H - 8) / 16;
	if (sb->rows < 1) sb->rows = 1;

	int line_start[APP_TEXT_MAX_LINES];
	int line_len[APP_TEXT_MAX_LINES];
	int nlines = wrap_text_lines(cols, st->buf, st->len, line_start, line_len);
	sb->max_offset = (nlines > sb->rows) ? (nlines - sb->rows) : 0;

	sb->track_w = TERM_SCROLLBAR_W;
	sb->track_x = win->x + win->w - TERM_SCROLLBAR_MARGIN - TERM_SCROLLBAR_W;
	sb->track_y = win->y + TITLE_H + 2;
	sb->track_h = win->h - TITLE_H - 4;

	if (sb->max_offset == 0) {
		sb->thumb_h = sb->track_h; // nothing to scroll - thumb fills the whole track
		sb->thumb_y = sb->track_y;
		return;
	}

	sb->thumb_h = sb->track_h * sb->rows / nlines;
	if (sb->thumb_h < 10) sb->thumb_h = 10; // stay grabbable/visible even in a long scrollback
	int off = st->scroll_offset;
	if (off > sb->max_offset) off = sb->max_offset;
	int range = sb->track_h - sb->thumb_h;
	sb->thumb_y = sb->track_y + range * (sb->max_offset - off) / sb->max_offset;
}

static void terminal_draw(window_t* win) {
	terminal_state_t* st = (terminal_state_t*)win->app_state;
	if (!st) return;

	terminal_scrollbar_t sb;
	terminal_layout(win, st, &sb);
	if (st->scroll_offset > sb.max_offset) st->scroll_offset = sb.max_offset;

	// narrow the wrapped-text area by the scrollbar's width so long lines
	// never render underneath it - same "fake a shorter window for this
	// call only" trick textedit_draw uses for its status strip above.
	window_t body = *win;
	body.w = win->w - TERM_SCROLLBAR_W - TERM_SCROLLBAR_MARGIN;
	draw_wrapped_text(&body, st->buf, st->len, APP_TEXT_COLOUR, APP_BG_COLOUR, st->scroll_offset);

	vga_fill_rect(sb.track_x, sb.track_y, sb.track_w, sb.track_h, TERM_SCROLLBAR_TRACK_COLOUR);
	vga_fill_rect(sb.track_x + 1, sb.thumb_y, sb.track_w - 2, sb.thumb_h, TERM_SCROLLBAR_THUMB_COLOUR);
}

// Clicking the scrollbar tracks pages the view: above the thumb scrolls
// back a page, below it scrolls forward a page (toward the live tail).
// Clicking the thumb itself, or anywhere in the text area, is a no-op.
static void terminal_on_click(window_t* win, int lx, int ly) {
	terminal_state_t* st = (terminal_state_t*)win->app_state;
	if (!st) return;

	terminal_scrollbar_t sb;
	terminal_layout(win, st, &sb);
	if (sb.max_offset == 0) return; // whole scrollback already fits, nothing to page

	int ax = win->x + lx;
	int ay = win->y + TITLE_H + ly; // on_click's lx/ly are body-relative (see app.h)
	if (ax < sb.track_x || ax >= sb.track_x + sb.track_w) return;

	if (ay < sb.thumb_y) {
		st->scroll_offset += sb.rows - 1;
		if (st->scroll_offset > sb.max_offset) st->scroll_offset = sb.max_offset;
	} else if (ay >= sb.thumb_y + sb.thumb_h) {
		st->scroll_offset -= sb.rows - 1;
		if (st->scroll_offset < 0) st->scroll_offset = 0;
	}
}

static void terminal_on_key(window_t* win, char c) {
	terminal_state_t* st = (terminal_state_t*)win->app_state;
	if (!st) return;

	// typing always snaps the view back to the live tail, same as any
	// normal terminal - otherwise a keystroke while scrolled up would
	// echo/run somewhere off-screen and look like it did nothing.
	st->scroll_offset = 0;

	if (c == '\n') {
		int line_len = st->len - st->input_start;
		char line[TERMINAL_LINE_MAX];
		if (line_len >= TERMINAL_LINE_MAX) line_len = TERMINAL_LINE_MAX - 1;
		for (int i = 0; i < line_len; i++) line[i] = st->buf[st->input_start + i];
		line[line_len] = 0;

		terminal_append(st, "\n", 1);

		char out[TERMINAL_CAP_MAX];
		shell_exec_line(line, out, TERMINAL_CAP_MAX);
		int out_len = str_len(out);
		if (out_len > 0) {
			terminal_append(st, out, out_len);
			terminal_append(st, "\n", 1);
		}

		terminal_print_prompt(st);
		return;
	}

	if (c == '\b') {
		if (st->len > st->input_start) st->len--;
		return;
	}

	// KEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT (keyboard.h) aren't handled here -
	// no in-window command history/cursor movement yet, same gap the
	// fullscreen console's history feature closed for itself already.
	// Falls through harmlessly since they're outside the printable-ASCII
	// range checked below.
	if (st->len < TERMINAL_BUF_MAX - 1 && (c >= 32 && c <= 126)) {
		st->buf[st->len++] = c;
	}
}

static const app_vtable_t terminal_vtable = {
	.on_open  = terminal_on_open,
	.on_close = terminal_on_close,
	.draw     = terminal_draw,
	.on_key   = terminal_on_key,
	.on_click = terminal_on_click,
};

// --------------------------------- files ----------------------------------
// fat32_list_dir_entries() (fat32.h) fills app_state's own array once on
// open/refresh; draw renders one row per entry; on_click hit-tests the row
// from ly and either just selects it (single click) or acts on it (double
// click, timer.c-timed): a directory cd's into it and refreshes the
// listing, a file opens into a Text Edit window via window_spawn_or_focus()
// (window.h) + textedit_load_file() further down this file.
#define FILES_MAX_ENTRIES 32
#define FILES_ROW_H       16
#define FILES_HEADER_H    18
#define FILES_DBLCLICK_TICKS 40 // timer.c runs at 100Hz -> ~0.4s between clicks

typedef struct {
	fat32_dirent_t entries[FILES_MAX_ENTRIES];
	int count;
	int selected; // row index, -1 = none
	uint32_t last_click_ticks;
	int last_click_row;
} files_state_t;

static void files_refresh(files_state_t* st) {
	st->count = fat32_list_dir_entries(st->entries, FILES_MAX_ENTRIES);
	st->selected = -1;
}

static void files_on_open(window_t* win) {
	files_state_t* st = (files_state_t*)kmalloc(sizeof(files_state_t));
	if (!st) return;
	st->last_click_ticks = 0;
	st->last_click_row = -1;
	files_refresh(st);
	win->app_state = st;
}

static void files_on_close(window_t* win) {
	if (win->app_state) kfree(win->app_state);
	win->app_state = 0;
}

static void files_draw(window_t* win) {
	files_state_t* st = (files_state_t*)win->app_state;
	if (!st) return;

	int y = win->y + TITLE_H + 4;
	vga_draw_text(win->x + 6, y, fat32_get_cwd(), APP_TEXT_COLOUR, APP_BG_COLOUR);
	y += FILES_HEADER_H;

	int max_rows = (win->y + win->h - y) / FILES_ROW_H;
	for (int i = 0; i < st->count && i < max_rows; i++) {
		uint32_t row_bg = (i == st->selected) ? APP_HIGHLIGHT_COLOUR : APP_BG_COLOUR;
		vga_fill_rect(win->x + 2, y, win->w - 4, FILES_ROW_H, row_bg);

		char line[48];
		int p = 0;
		line[p++] = st->entries[i].is_dir ? '/' : ' ';
		line[p++] = ' ';
		int name_len = str_len(st->entries[i].name);
		for (int k = 0; k < name_len && p < 40; k++) line[p++] = st->entries[i].name[k];
		line[p] = 0;
		vga_draw_text(win->x + 6, y + 1, line, APP_TEXT_COLOUR, row_bg);
		y += FILES_ROW_H;
	}

	if (st->count == 0) {
		vga_draw_text(win->x + 6, y, "(empty)", APP_TEXT_COLOUR, APP_BG_COLOUR);
	}
}

static void files_on_click(window_t* win, int lx, int ly) {
	(void)lx;
	files_state_t* st = (files_state_t*)win->app_state;
	if (!st) return;

	int row = (ly - FILES_HEADER_H) / FILES_ROW_H;
	if (row < 0 || row >= st->count) { st->selected = -1; return; }

	uint32_t now = timer_get_ticks();
	int is_double_click = (row == st->last_click_row) &&
	                       (now - st->last_click_ticks) <= FILES_DBLCLICK_TICKS;

	st->last_click_row = row;
	st->last_click_ticks = now;
	st->selected = row;

	if (!is_double_click) return;

	fat32_dirent_t* entry = &st->entries[row];
	if (entry->is_dir) {
		fat32_change_dir(entry->name);
		files_refresh(st);
		st->last_click_row = -1; // don't chain a double-click across a directory change
	} else {
		int idx = window_spawn_or_focus("Text Editor", 320, 220, APP_TEXTEDIT);
		if (idx != -1) textedit_load_file(window_at(idx), entry->name);
	}
}

static const app_vtable_t files_vtable = {
	.on_open  = files_on_open,
	.on_close = files_on_close,
	.draw     = files_draw,
	.on_click = files_on_click,
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
// Same typed-buffer-and-on_key shape as the terminal, but it's a plain
// editable buffer (Enter inserts a literal '\n' instead of dispatching a
// command) and it round-trips through fat32_read_file/fat32_write_file
// (fat32.h) for load/save. Save is wired up as a real File-menu item
// (menubar_add_item, registered from on_focus) rather than a keybinding -
// see app.h's on_focus and menubar.h's "items are global, not per-window"
// known gap, which is why we track the one focused Text Edit window in
// g_textedit_focus_win rather than passing it through the cmd callback.
#define TEXTEDIT_BUF_MAX  4096
#define TEXTEDIT_NAME_MAX 64

typedef struct {
	char buf[TEXTEDIT_BUF_MAX];
	int len;
	char filename[TEXTEDIT_NAME_MAX]; // empty = untitled, nothing to Save into yet
	int dirty;                        // modified since open/last save

	// --- inline "type a filename" prompt, used by Save (on an untitled
	// doc) and Save As - see textedit_start_save_as() below. While active,
	// on_key routes keys into save_as_buf instead of the document, Enter
	// commits the write, Ctrl-C cancels back to normal editing.
	int save_as_active;
	char save_as_buf[TEXTEDIT_NAME_MAX];
	int save_as_len;
} textedit_state_t;

// menubar_add_item's cmd_t takes no arguments, so on_focus (called each
// time a Text Edit window becomes frontmost) stashes which window here for
// the Save/Save As callbacks to act on. Single-window assumption is fine
// for now - same limitation menubar.h already documents for any app's
// menu items.
static window_t* g_textedit_focus_win = 0;

static void textedit_on_open(window_t* win) {
	textedit_state_t* st = (textedit_state_t*)kmalloc(sizeof(textedit_state_t));
	if (!st) return;
	st->len = 0;
	st->filename[0] = 0;
	st->dirty = 0;
	st->save_as_active = 0;
	st->save_as_len = 0;
	win->app_state = st;
}

static void textedit_on_close(window_t* win) {
	if (win->app_state) kfree(win->app_state);
	win->app_state = 0;
	if (g_textedit_focus_win == win) g_textedit_focus_win = 0;
}

// Loads `filename` (from the current fat32 directory) into an already-open
// Text Edit window - called by files_on_click above when a file gets
// double-clicked. Reuses the window's existing app_state (allocated back
// in on_open) rather than requiring a fresh window per file.
static void textedit_load_file(window_t* win, const char* filename) {
	textedit_state_t* st = (textedit_state_t*)win->app_state;
	if (!st) return;
	int bytes = fat32_read_file(filename, (uint8_t*)st->buf, TEXTEDIT_BUF_MAX - 1);
	st->len = (bytes < 0) ? 0 : bytes;
	st->buf[st->len] = 0;
	str_copy(st->filename, filename, TEXTEDIT_NAME_MAX);
	st->dirty = 0;
	st->save_as_active = 0;
}

// Drops into the inline filename prompt (pre-filled with the current
// filename, if any, so re-saving under the same name is just Enter).
static void textedit_start_save_as(textedit_state_t* st) {
	st->save_as_active = 1;
	str_copy(st->save_as_buf, st->filename, TEXTEDIT_NAME_MAX);
	st->save_as_len = str_len(st->save_as_buf);
}

static void textedit_cmd_save(void) {
	if (!g_textedit_focus_win) return;
	textedit_state_t* st = (textedit_state_t*)g_textedit_focus_win->app_state;
	if (!st) return;
	if (st->filename[0] == 0) {
		// untitled - fall into the same "type a name" prompt Save As uses,
		// instead of silently doing nothing (the old known gap).
		textedit_start_save_as(st);
		return;
	}
	int written = fat32_write_file(st->filename, (const uint8_t*)st->buf, (uint32_t)st->len);
	if (written >= 0) st->dirty = 0;
}

static void textedit_cmd_save_as(void) {
	if (!g_textedit_focus_win) return;
	textedit_state_t* st = (textedit_state_t*)g_textedit_focus_win->app_state;
	if (!st) return;
	textedit_start_save_as(st);
}

static void textedit_on_focus(window_t* win) {
	g_textedit_focus_win = win;
	menubar_add_item(MENUBAR_FILE, "Save", textedit_cmd_save);
	menubar_add_item(MENUBAR_FILE, "Save As", textedit_cmd_save_as);
}

static void textedit_draw(window_t* win) {
	textedit_state_t* st = (textedit_state_t*)win->app_state;
	if (!st) return;

	// filename/status strip above the wrapped text body, same warm chrome
	// colours as the rest of the app - draw_wrapped_text starts its own
	// text a fixed offset below the title bar, so this needs to live in
	// that same first row rather than overlapping it. While the filename
	// prompt is active it takes over this strip entirely.
	char status[TEXTEDIT_NAME_MAX + 24];
	int p = 0;
	if (st->save_as_active) {
		const char* prefix = "Save as: ";
		for (int i = 0; prefix[i]; i++) status[p++] = prefix[i];
		for (int i = 0; i < st->save_as_len; i++) status[p++] = st->save_as_buf[i];
		status[p++] = '_'; // crude text-cursor so it's obvious typing goes here
	} else {
		const char* name = st->filename[0] ? st->filename : "untitled";
		for (int i = 0; name[i] && p < TEXTEDIT_NAME_MAX; i++) status[p++] = name[i];
		if (st->dirty) { status[p++] = ' '; status[p++] = '*'; }
	}
	status[p] = 0;
	vga_draw_text(win->x + 6, win->y + TITLE_H + 4, status, APP_TEXT_COLOUR, APP_BG_COLOUR);

	// body text starts one row down so it doesn't collide with the status
	// strip above - draw_wrapped_text always measures from win->y+TITLE_H,
	// so fake a slightly-shorter/shifted window for that call only.
	window_t body = *win;
	body.y += 16;
	body.h -= 16;
	draw_wrapped_text(&body, st->buf, st->len, APP_TEXT_COLOUR, APP_BG_COLOUR, 0);
}

static void textedit_on_key(window_t* win, char c) {
	textedit_state_t* st = (textedit_state_t*)win->app_state;
	if (!st) return;

	if (st->save_as_active) {
		if (c == KEY_CTRL_C) { st->save_as_active = 0; return; } // cancel, back to editing
		if (c == '\n') {
			if (st->save_as_len > 0) {
				st->save_as_buf[st->save_as_len] = 0;
				int written = fat32_write_file(st->save_as_buf, (const uint8_t*)st->buf, (uint32_t)st->len);
				if (written >= 0) {
					str_copy(st->filename, st->save_as_buf, TEXTEDIT_NAME_MAX);
					st->dirty = 0;
				}
			}
			st->save_as_active = 0;
			return;
		}
		if (c == '\b') {
			if (st->save_as_len > 0) st->save_as_len--;
			return;
		}
		if (st->save_as_len < TEXTEDIT_NAME_MAX - 1 && c >= 32 && c <= 126) {
			st->save_as_buf[st->save_as_len++] = c;
		}
		return; // swallow every key while prompting - none of it edits the document
	}

	if (c == '\b') {
		if (st->len > 0) { st->len--; st->dirty = 1; }
		return;
	}

	if (st->len < TEXTEDIT_BUF_MAX - 1 && ((c >= 32 && c <= 126) || c == '\n')) {
		st->buf[st->len++] = c;
		st->dirty = 1;
	}
}

static const app_vtable_t textedit_vtable = {
	.on_open  = textedit_on_open,
	.on_close = textedit_on_close,
	.draw     = textedit_draw,
	.on_key   = textedit_on_key,
	.on_focus = textedit_on_focus,
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
