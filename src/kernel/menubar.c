#include "menubar.h"
#include "taskbar.h"
#include "vga.h"

// Same warm palette as taskbar.c/window.c's chrome, so the menu bar reads
// as part of the same desktop instead of a bolted-on strip.
#define BAR_BG_COLOUR      0xF39C12   // WIN_TITLE_COLOUR
#define PANEL_COLOUR       0xFFF8DC   // WIN_BODY_COLOUR
#define BORDER_COLOUR      0x6E2C00   // WIN_BORDER_COLOUR
#define TEXT_COLOUR        0x4A2511   // WIN_TITLE_TEXT_COLOUR
#define STUB_ROW_COLOUR    0xE9D9AE   // faint row divider inside a dropdown

#define PAD          6
#define BTN_W        44
#define BTN_GAP      2
#define ITEM_H       18
#define STUB_ROWS    4
#define DROPDOWN_W   130

typedef enum { MB_NONE = 0, MB_FILE, MB_EDIT } mb_menu_t;

static mb_menu_t s_open_menu = MB_NONE;

// --- app-supplied menu items (see menubar.h) ---
typedef struct {
	const char* title;
	menubar_cmd_t cmd;
} menu_item_t;

static menu_item_t s_file_items[MENUBAR_MAX_ITEMS];
static int         s_file_count = 0;
static menu_item_t s_edit_items[MENUBAR_MAX_ITEMS];
static int         s_edit_count = 0;

void menubar_add_item(menubar_menu_t menu, const char* title, menubar_cmd_t cmd) {
	if (menu == MENUBAR_FILE) {
		if (s_file_count >= MENUBAR_MAX_ITEMS) return;
		s_file_items[s_file_count].title = title;
		s_file_items[s_file_count].cmd   = cmd;
		s_file_count++;
	} else {
		if (s_edit_count >= MENUBAR_MAX_ITEMS) return;
		s_edit_items[s_edit_count].title = title;
		s_edit_items[s_edit_count].cmd   = cmd;
		s_edit_count++;
	}
}

void menubar_clear_menu(void) {
	s_file_count = 0;
	s_edit_count = 0;
	s_open_menu  = MB_NONE;
}

// rows a given menu should draw right now - real items if any were
// registered, else the old blank STUB_ROWS look so an unwired menu still
// opens as *something* rather than a zero-height panel
static int menu_row_count(mb_menu_t m) {
	int n = (m == MB_FILE) ? s_file_count : s_edit_count;
	return n > 0 ? n : STUB_ROWS;
}

static int file_btn_x(void) { return TASKBAR_W + PAD; }
static int edit_btn_x(void) { return file_btn_x() + BTN_W + BTN_GAP; }

static int in_box(int mx, int my, int x, int y, int w, int h) {
	return mx >= x && mx < x + w && my >= y && my < y + h;
}

static int text_len(const char* s) {
	int n = 0;
	while (s && s[n]) n++;
	return n;
}

static void draw_menu_button(int x, const char* label, int open) {
	uint32_t bg = open ? PANEL_COLOUR : BAR_BG_COLOUR;
	uint32_t fg = open ? TEXT_COLOUR : PANEL_COLOUR;
	vga_fill_rect(x, 0, BTN_W, MENUBAR_H, bg);
	vga_draw_text(x + 6, 2, label, fg, bg);
}

// dropdown panel: one row per registered item (title drawn in it), or
// STUB_ROWS blank rows with just a faint divider if nothing's registered
// for this menu yet
static void draw_dropdown(int x, mb_menu_t m) {
	menu_item_t* items = (m == MB_FILE) ? s_file_items : s_edit_items;
	int count = (m == MB_FILE) ? s_file_count : s_edit_count;
	int rows = menu_row_count(m);

	int y = MENUBAR_H;
	int h = rows * ITEM_H;
	vga_fill_rect(x, y, DROPDOWN_W, h, PANEL_COLOUR);
	vga_draw_rect(x, y, DROPDOWN_W, h, BORDER_COLOUR);

	if (count > 0) {
		for (int i = 0; i < count; i++) {
			vga_draw_text(x + 6, y + i * ITEM_H + 2, items[i].title, TEXT_COLOUR, PANEL_COLOUR);
		}
	} else {
		for (int i = 1; i < rows; i++) {
			vga_fill_rect(x + 4, y + i * ITEM_H, DROPDOWN_W - 8, 1, STUB_ROW_COLOUR);
		}
	}
}

void menubar_draw(int fb_w, const char* app_name) {
	vga_fill_rect(TASKBAR_W, 0, fb_w - TASKBAR_W, MENUBAR_H, BAR_BG_COLOUR);
	vga_fill_rect(TASKBAR_W, MENUBAR_H - 1, fb_w - TASKBAR_W, 1, BORDER_COLOUR);

	draw_menu_button(file_btn_x(), "File", s_open_menu == MB_FILE);
	draw_menu_button(edit_btn_x(), "Edit", s_open_menu == MB_EDIT);

	if (app_name && app_name[0]) {
		int len = text_len(app_name);
		int tx = fb_w - PAD - len * 8;
		if (tx > edit_btn_x() + BTN_W + PAD) {
			vga_draw_text(tx, 2, app_name, PANEL_COLOUR, BAR_BG_COLOUR);
		}
	}

	if (s_open_menu == MB_FILE) draw_dropdown(file_btn_x(), MB_FILE);
	else if (s_open_menu == MB_EDIT) draw_dropdown(edit_btn_x(), MB_EDIT);
}

int menubar_handle_click(int mx, int my) {
	if (s_open_menu != MB_NONE) {
		int dx = (s_open_menu == MB_FILE) ? file_btn_x() : edit_btn_x();
		int rows = menu_row_count(s_open_menu);
		if (in_box(mx, my, dx, MENUBAR_H, DROPDOWN_W, rows * ITEM_H)) {
			int row = (my - MENUBAR_H) / ITEM_H;
			menu_item_t* items = (s_open_menu == MB_FILE) ? s_file_items : s_edit_items;
			int count = (s_open_menu == MB_FILE) ? s_file_count : s_edit_count;
			if (row >= 0 && row < count && items[row].cmd) items[row].cmd();
			s_open_menu = MB_NONE; // picking an item closes the menu, same as real menu bars
			return 1;
		}
	}

	if (in_box(mx, my, file_btn_x(), 0, BTN_W, MENUBAR_H)) {
		s_open_menu = (s_open_menu == MB_FILE) ? MB_NONE : MB_FILE;
		return 1;
	}
	if (in_box(mx, my, edit_btn_x(), 0, BTN_W, MENUBAR_H)) {
		s_open_menu = (s_open_menu == MB_EDIT) ? MB_NONE : MB_EDIT;
		return 1;
	}

	if (s_open_menu != MB_NONE) {
		s_open_menu = MB_NONE; // click-away closes whichever dropdown was open
		return in_box(mx, my, TASKBAR_W, 0, 1 << 30, MENUBAR_H); // still absorb if it hit the bar itself
	}

	return 0;
}

void menubar_full_rect(int fb_w, int* x, int* y, int* w, int* h) {
	*x = TASKBAR_W;
	*y = 0;
	*w = fb_w - TASKBAR_W;
	*h = MENUBAR_H;
	if (s_open_menu != MB_NONE) {
		*h = MENUBAR_H + menu_row_count(s_open_menu) * ITEM_H;
	}
}

int  menubar_is_open(void) { return s_open_menu != MB_NONE; }
void menubar_close(void)   { s_open_menu = MB_NONE; }
