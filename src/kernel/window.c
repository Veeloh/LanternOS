#include "window.h"
#include "vga.h"
#include "mouse.h"
#include "cursor.h"
#include "keyboard.h"
#include "heap.h"

#define TITLE_H 20
#define BG_COLOUR 0xE67E22
#define WIN_BODY_COLOUR 0xFFF8DC
#define WIN_TITLE_COLOUR 0xF39C12
#define WIN_BORDER_COLOUR 0x6E2C00
#define WIN_TITLE_TEXT_COLOUR 0x4A2511

#define MAX_WINDOWS 4

typedef struct {
	int x, y, w, h;
	const char* title;
} window_t;

static window_t windows[MAX_WINDOWS];
static int z_order[MAX_WINDOWS];
static int window_count = 0;

static void draw_window(window_t* win) {
	vga_fill_rect(win->x, win->y, win->w, TITLE_H, WIN_TITLE_COLOUR);
	vga_fill_rect(win->x, win->y + TITLE_H, win->w, win->h - TITLE_H, WIN_BODY_COLOUR);
	vga_draw_rect(win->x, win->y, win->w, win->h, WIN_BORDER_COLOUR);
	vga_draw_text(win->x + 6, win->y + 4, win->title, WIN_TITLE_TEXT_COLOUR, WIN_TITLE_COLOUR);
}

static int point_in_titlebar(window_t* win, int px, int py) {
	return px >= win->x && px < win->x + win->w && py >= win->y && py < win->y + TITLE_H;
}

static int rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
	return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void repaint_region(int rx, int ry, int rw, int rh) {
	vga_fill_rect(rx, ry, rw, rh, BG_COLOUR);
	for (int i = 0; i < window_count; i++) {
		window_t* win = &windows[z_order[i]];
		if (rects_intersect(win->x, win->y, win->w, win->h, rx, ry, rw, rh))
			draw_window(win);
	}
}

static void expand_rect(int* x1, int* y1, int* x2, int* y2, int x, int y, int w, int h) {
	if (x < *x1) *x1 = x;
	if (y < *y1) *y1 = y;
	if (x + w > *x2) *x2 = x + w;
	if (y + h > *y2) *y2 = y + h;
}

static void bring_to_front(int idx) {
	int pos = -1;
	for (int i = 0; i < window_count; i++) {
		if (z_order[i] == idx) { pos = i; break; }
	}
	if (pos == -1 || pos == window_count - 1) return;
	for (int i = pos; i < window_count - 1; i++) z_order[i] = z_order[i + 1];
	z_order[window_count - 1] = idx;
}

void window_demo() {
	int fb_w = (int)vga_get_fb_width();
	int fb_h = (int)vga_get_fb_height();

	uint32_t* backbuffer = (uint32_t*)kmalloc(fb_w * fb_h * sizeof(uint32_t));
	if (!backbuffer) return;

	vga_set_draw_target(backbuffer, fb_w, fb_h);
	vga_fill_rect(0, 0, fb_w, fb_h, BG_COLOUR);
	cursor_reset();

	window_count = 4;
	windows[0] = (window_t){ fb_w / 2 - 180, fb_h / 2 - 100, 200, 120, "SolOS" };
	windows[1] = (window_t){ fb_w / 2 - 120, fb_h / 2 - 60,  200, 120, "Files" };
	windows[2] = (window_t){ fb_w / 2 - 60,  fb_h / 2 - 20,  200, 120, "Notes" };
	windows[3] = (window_t){ fb_w / 2,       fb_h / 2 + 20,  200, 120, "Terminal" };
	for (int i = 0; i < window_count; i++) z_order[i] = i;

	for (int i = 0; i < window_count; i++) draw_window(&windows[z_order[i]]);
	cursor_update();
	vga_present(backbuffer);

	int dragging_index = -1;
	int drag_dx = 0;
	int drag_dy = 0;
	int last_mouse_x = mouse_get_x();
	int last_mouse_y = mouse_get_y();

	while (1) {
		int mx = mouse_get_x();
		int my = mouse_get_y();
		int pressed = mouse_left_pressed();

		if (pressed && dragging_index == -1) {
			for (int i = window_count - 1; i >= 0; i--) {
				window_t* win = &windows[z_order[i]];
				if (point_in_titlebar(win, mx, my)) {
					dragging_index = z_order[i];
					drag_dx = mx - win->x;
					drag_dy = my - win->y;
					bring_to_front(dragging_index);
					break;
				}
			}
		} else if (!pressed) {
			dragging_index = -1;
		}

		int dirty = 0;
		int dx1 = fb_w, dy1 = fb_h, dx2 = 0, dy2 = 0;
		int window_moved = 0;

		if (dragging_index != -1) {
			window_t* win = &windows[dragging_index];
			int nx = mx - drag_dx;
			int ny = my - drag_dy;
			if (nx != win->x || ny != win->y) {
				expand_rect(&dx1, &dy1, &dx2, &dy2, win->x, win->y, win->w, win->h);
				win->x = nx;
				win->y = ny;
				expand_rect(&dx1, &dy1, &dx2, &dy2, win->x, win->y, win->w, win->h);
				window_moved = 1;
				dirty = 1;
			}
		}

		int cursor_moved = (mx != last_mouse_x || my != last_mouse_y);
		if (cursor_moved) {
			expand_rect(&dx1, &dy1, &dx2, &dy2, last_mouse_x, last_mouse_y, CURSOR_W, CURSOR_H);
			expand_rect(&dx1, &dy1, &dx2, &dy2, mx, my, CURSOR_W, CURSOR_H);
			dirty = 1;
		}

		if (window_moved) repaint_region(dx1, dy1, dx2 - dx1, dy2 - dy1);

		if (cursor_moved) {
			cursor_update();
			last_mouse_x = mx;
			last_mouse_y = my;
		}

		if (dirty) vga_present_rect(backbuffer, dx1, dy1, dx2 - dx1, dy2 - dy1);

		char c = keyboard_getchar();
		if (c == KEY_CTRL_C) break;
	}

	vga_clear_draw_target();
	kfree(backbuffer);
	vga_clear();
	cursor_reset();
}
