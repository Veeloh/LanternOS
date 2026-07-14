#include "window.h"
#include "vga.h"
#include "mouse.h"
#include "cursor.h"
#include "keyboard.h"

#define TITLE_H 20
#define BG_COLOUR 0x2C3E50
#define WIN_BODY_COLOUR 0xECF0F1
#define WIN_TITLE_COLOUR 0x34495E
#define WIN_BORDER_COLOUR 0x000000
#define WIN_TITLE_TEXT_COLOUR 0xFFFFFF

typdef struct {
	int x, y, w, h;
	const char* title;
} window_t;

static void draw_window(window_t* win) {
	vga_fill_rect(win->x, win->y, win->w, TITLE_H, WIN_TITLE_COLOUR);
	vga_fill_rect(win->x, win->y + TITLE_H, win->w, win->h - TITLE_H, WIN_BODY_COLOUR);
	vga_fill_rect(win->x, win->y, win->w, win->h, WIN_BORDER_COLOUR);
	vga_fill_rect(win->x + 6, win->y + 4, win->title, WIN_TITLE_TEXT_COLOUR, WIN_TITLE_COLOUR);
}

static void erase_window(window_t* win) {
	vga_fill_rect(win->x, win->y, win->w, win->h, BG_COLOUR);
}

static int point_in_titlebar(window_t* win, int px, int py) {
	return px >= win->x && px < win->x + win->w && py >= win->y && py < win->y + TITLE_H;
}

void window_demo() {
	int fb_w = (int)vga_get_fb_width();
	int fb_h = (int)vga_get_fb_height();

	vga_fill_rect(0, 0, fb_w, fb_h, BG_COLOUR);
	cursor_reset();

	window_t win = { fb_w / 2 - 100, fb_h / 2 - 60, 200, 120, "SolOS"};
	draw_window(&win);

	int dragging = 0;
	int drag_dx = 0;
	int drag_dy = 0;

	while(1) {
		int mx = mouse_get_x();
		int my = mouse_get_y();

		int pressed = mouse_left_pressed();

		if (pressed && !dragging && point_in_titlebar(&win, mx, my)) {
			dragging = 1;
			drag_dx = mx - win.x;
			drag_dy = my - win.y;
		}
		else if (!pressed) {
			dragging = 0;
		}

		if (dragging) {
			int nx = mx - drag_dx;
			int ny = my - drag_dy;
			if (nx != win.x || ny != win.y) {
				erase_window(&win);
				win.x = nx;
				win.y = ny;
				draw_window(&win);
			}
		}

		cursor_update
		char c = keyboard_getchar();
		if (c == KEY_CTRL_C) break;
	}

	vga_clear();
	cursor_reset();
}
