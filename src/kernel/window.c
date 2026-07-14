#include "window.h"
#include "vga.h"
#include "mouse.h"
#include "cursor.h"
#include "keyboard.h"
#include "heap.h"

#define TITLE_H 20
#define BG_COLOUR 0x2C3E50
#define WIN_BODY_COLOUR 0xECF0F1
#define WIN_TITLE_COLOUR 0x34495E
#define WIN_BORDER_COLOUR 0x000000
#define WIN_TITLE_TEXT_COLOUR 0xFFFFFF

typedef struct {
	int x, y, w, h;
	const char* title;
} window_t;

static void draw_window(window_t* win) {
	vga_fill_rect(win->x, win->y, win->w, TITLE_H, WIN_TITLE_COLOUR);
	vga_fill_rect(win->x, win->y + TITLE_H, win->w, win->h - TITLE_H, WIN_BODY_COLOUR);
	vga_draw_rect(win->x, win->y, win->w, win->h, WIN_BORDER_COLOUR);
	vga_draw_text(win->x + 6, win->y + 4, win->title, WIN_TITLE_TEXT_COLOUR, WIN_TITLE_COLOUR);
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

	uint32_t* backbuffer = (uint32_t*)kmalloc(fb_w * fb_h * sizeof(uint32_t));
	if (!backbuffer) return;

	vga_set_draw_target(backbuffer, fb_w, fb_h);

	vga_fill_rect(0, 0, fb_w, fb_h, BG_COLOUR);
	cursor_reset();

	window_t win = { fb_w / 2 - 100, fb_h / 2 - 60, 200, 120, "SolOS"};
	draw_window(&win);
	cursor_update();
	vga_present(backbuffer);

	int dragging = 0;
	int drag_dx = 0;
	int drag_dy = 0;
	int last_mouse_x = mouse_get_x();
	int last_mouse_y = mouse_get_y();

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

		int need_present = 0;

		if (dragging) {
			int nx = mx - drag_dx;
			int ny = my - drag_dy;
			if (nx != win.x || ny != win.y) {
				erase_window(&win);
				win.x = nx;
				win.y = ny;
				draw_window(&win);
				need_present = 1;
			}
		}

<<<<<<< HEAD
		cursor_update();
=======
		if (mx != last_mouse_x || my != last_mouse_y) {
			cursor_update();
			need_present = 1;
			last_mouse_x = mx;
			last_mouse_y = my;
		}

		if (need_present) vga_present(backbuffer);

>>>>>>> b52919363d68a5605785940bd755034101940a35
		char c = keyboard_getchar();
		if (c == KEY_CTRL_C) break;
	}

	vga_clear_draw_target();
	kfree(backbuffer);
	vga_clear();
	cursor_reset();
}
