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

// dropdown panel: just STUB_ROWS empty rows with a faint divider between
// them - no labels/actions wired up yet, that's for whoever fills these in
static void draw_dropdown(int x) {
	int y = MENUBAR_H;
	int h = STUB_ROWS * ITEM_H;
	vga_fill_rect(x, y, DROPDOWN_W, h, PANEL_COLOUR);
	vga_draw_rect(x, y, DROPDOWN_W, h, BORDER_COLOUR);
	for (int i = 1; i < STUB_ROWS; i++) {
		vga_fill_rect(x + 4, y + i * ITEM_H, DROPDOWN_W - 8, 1, STUB_ROW_COLOUR);
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

	if (s_open_menu == MB_FILE) draw_dropdown(file_btn_x());
	else if (s_open_menu == MB_EDIT) draw_dropdown(edit_btn_x());
}

int menubar_handle_click(int mx, int my) {
	if (s_open_menu != MB_NONE) {
		int dx = (s_open_menu == MB_FILE) ? file_btn_x() : edit_btn_x();
		if (in_box(mx, my, dx, MENUBAR_H, DROPDOWN_W, STUB_ROWS * ITEM_H)) {
			return 1; // clicked a stub row - absorb it, nothing wired up yet
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
		*h = MENUBAR_H + STUB_ROWS * ITEM_H;
	}
}

int  menubar_is_open(void) { return s_open_menu != MB_NONE; }
void menubar_close(void)   { s_open_menu = MB_NONE; }
