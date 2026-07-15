#include "window.h"
#include "taskbar.h"
#include "vga.h"
#include "mouse.h"
#include "cursor.h"
#include "keyboard.h"
#include "heap.h"
#include "clock.h"
#include "acpi.h"

#define BG_COLOUR 0xE67E22
#define WIN_BODY_COLOUR 0xFFF8DC
#define WIN_TITLE_COLOUR 0xF39C12
#define WIN_BORDER_COLOUR 0x6E2C00
#define WIN_TITLE_TEXT_COLOUR 0x4A2511

#define DESKTOP_LOGO_SIZE    96
#define DESKTOP_LOGO_COLOUR  WIN_TITLE_COLOUR

static window_t windows[MAX_WINDOWS];
static int z_order[MAX_WINDOWS];
static int g_window_count = 0;
static int g_fb_w = 0, g_fb_h = 0;


static int g_battery_pct = 100;
static int g_charging = 0;
static const char* g_weekday = "TUE";
static int g_day = 15;

// ---------------------------- accessors (window.h) ----------------------------

int window_count(void) { return g_window_count; }

int window_z_order(int slot) {
	if (slot < 0 || slot >= g_window_count) return -1;
	return z_order[slot];
}

window_t* window_at(int idx) {
	if (idx < 0 || idx >= g_window_count) return 0;
	return &windows[idx];
}

int window_top_index(void) {
	if (g_window_count == 0) return -1;
	return z_order[g_window_count - 1];
}

// -------------------------------- internals ------------------------------

static void draw_window(window_t* win) {
	vga_fill_rect(win->x, win->y, win->w, TITLE_H, WIN_TITLE_COLOUR);
	vga_fill_rect(win->x, win->y + TITLE_H, win->w, win->h - TITLE_H, WIN_BODY_COLOUR);
	vga_draw_rect(win->x, win->y, win->w, win->h, WIN_BORDER_COLOUR);
	vga_draw_text(win->x + 6, win->y + 4, win->title, WIN_TITLE_TEXT_COLOUR, WIN_TITLE_COLOUR);
}

// bounding box of the desktop logo - shared by the draw call and the
// repaint dirty-rect check so they can never disagree about where it is.
static void desktop_logo_rect(int* x, int* y, int* d) {
	*d = DESKTOP_LOGO_SIZE;
	*x = TASKBAR_W + (g_fb_w - TASKBAR_W - *d) / 2;
	*y = (g_fb_h - *d) / 2;
}

// same ray/core proportions as taskbar.c's draw_sun (10/28 core, 4/28 ray
// length, 2/28 ray thickness relative to icon size), just scaled up to d.
static void draw_desktop_logo(void) {
	int x, y, d;
	desktop_logo_rect(&x, &y, &d);

	int core = d * 10 / 28;
	int ray_len = d * 4 / 28;
	int ray_th = d * 2 / 28;
	int cx = x + d / 2, cy = y + d / 2;

	vga_fill_rect(cx - core / 2, cy - core / 2, core, core, DESKTOP_LOGO_COLOUR);
	vga_fill_rect(cx - ray_th / 2, y,               ray_th, ray_len, DESKTOP_LOGO_COLOUR);
	vga_fill_rect(cx - ray_th / 2, y + d - ray_len, ray_th, ray_len, DESKTOP_LOGO_COLOUR);
	vga_fill_rect(x,               cy - ray_th / 2, ray_len, ray_th, DESKTOP_LOGO_COLOUR);
	vga_fill_rect(x + d - ray_len, cy - ray_th / 2, ray_len, ray_th, DESKTOP_LOGO_COLOUR);
}

static int point_in_titlebar(window_t* win, int px, int py) {
	return px >= win->x && px < win->x + win->w && py >= win->y && py < win->y + TITLE_H;
}

static int rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
	return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void repaint_region(int rx, int ry, int rw, int rh) {
	vga_fill_rect(rx, ry, rw, rh, BG_COLOUR);

	int lx, ly, ld;
	desktop_logo_rect(&lx, &ly, &ld);
	if (rects_intersect(lx, ly, ld, ld, rx, ry, rw, rh))
		draw_desktop_logo();

	for (int i = 0; i < g_window_count; i++) {
		window_t* win = &windows[z_order[i]];
		if (rects_intersect(win->x, win->y, win->w, win->h, rx, ry, rw, rh))
			draw_window(win);
	}

	// taskbar + start menu sit above ordinary windows and own the left strip
	int tx, ty, tw, th;
	taskbar_full_rect(g_fb_h, &tx, &ty, &tw, &th);
	if (rects_intersect(tx, ty, tw, th, rx, ry, rw, rh)) {
		uint8_t hh, mm, ss;
		clock_get(&hh, &mm, &ss);
		taskbar_draw(g_fb_h, g_battery_pct, g_charging, g_weekday, g_day, hh, mm, ss);
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
	for (int i = 0; i < g_window_count; i++) {
		if (z_order[i] == idx) { pos = i; break; }
	}
	if (pos == -1 || pos == g_window_count - 1) return;
	for (int i = pos; i < g_window_count - 1; i++) z_order[i] = z_order[i + 1];
	z_order[g_window_count - 1] = idx;
}

static int find_window_by_title(const char* title) {
	for (int i = 0; i < g_window_count; i++) {
		const char* t = windows[i].title;
		int match = 1;
		for (int j = 0; ; j++) {
			if (t[j] != title[j]) { match = 0; break; }
			if (t[j] == 0) break;
		}
		if (match) return i;
	}
	return -1;
}

// spawns a new window clear of the taskbar strip; returns its raw index, or
// -1 if there's no free slot. Newly spawned windows land on top.
static int spawn_window(const char* title, int w, int h) {
	if (g_window_count >= MAX_WINDOWS) return -1;
	int idx = g_window_count;
	int x = TASKBAR_W + 24 + (idx * 18);
	int y = 40 + (idx * 18);
	if (x + w > g_fb_w) x = g_fb_w - w;
	if (x < TASKBAR_W) x = TASKBAR_W;
	windows[idx] = (window_t){ x, y, w, h, title };
	z_order[g_window_count] = idx;
	g_window_count++;
	return idx;
}

void desktop() {
	g_fb_w = (int)vga_get_fb_width();
	g_fb_h = (int)vga_get_fb_height();
	int fb_w = g_fb_w, fb_h = g_fb_h;

	uint32_t* backbuffer = (uint32_t*)kmalloc(fb_w * fb_h * sizeof(uint32_t));
	if (!backbuffer) return;

	vga_set_draw_target(backbuffer, fb_w, fb_h);
	vga_fill_rect(0, 0, fb_w, fb_h, BG_COLOUR);
	draw_desktop_logo();

	cursor_reset();

	g_window_count = 0;
	spawn_window("SolOS", 220, 130);

	for (int i = 0; i < g_window_count; i++) draw_window(&windows[z_order[i]]);
	{
		uint8_t hh, mm, ss;
		clock_get(&hh, &mm, &ss);
		taskbar_draw(fb_h, g_battery_pct, g_charging, g_weekday, g_day, hh, mm, ss);
	}
	cursor_update();
	vga_present(backbuffer);

	int dragging_index = -1;
	int drag_dx = 0;
	int drag_dy = 0;
	int last_mouse_x = mouse_get_x();
	int last_mouse_y = mouse_get_y();
	int prev_pressed = 0;
	uint8_t last_ss = 255;

	while (1) {
		int mx = mouse_get_x();
		int my = mouse_get_y();
		int pressed = mouse_left_pressed();
		int clicked = pressed && !prev_pressed; // rising edge

		int dirty = 0;
		int dx1 = fb_w, dy1 = fb_h, dx2 = 0, dy2 = 0;

		int taskbar_consumed = 0;

		if (clicked) {
			int menu_open_before = taskbar_start_menu_open();
			int click_targets_taskbar = (mx >= 0 && mx < TASKBAR_W) || menu_open_before;

			// capture the taskbar/start-menu's on-screen footprint BEFORE the
			// click can mutate s_start_menu_open, so a closing click still
			// invalidates the full (wider) area the panel used to occupy.
			int tx0, ty0, tw0, th0;
			taskbar_full_rect(fb_h, &tx0, &ty0, &tw0, &th0);

			int out_idx = -1;
			tb_action_t action = taskbar_handle_click(mx, my, &out_idx);
			taskbar_consumed = click_targets_taskbar;

			switch (action) {
				case TB_POWER:
					acpi_poweroff(); // does not return on success
					break;

				case TB_OPEN_TERMINAL: {
					int idx = find_window_by_title("Terminal");
					if (idx == -1) idx = spawn_window("Terminal", 240, 150);
					if (idx != -1) {
						bring_to_front(idx);
						window_t* w = &windows[idx];
						expand_rect(&dx1, &dy1, &dx2, &dy2, w->x, w->y, w->w, w->h);
						dirty = 1;
					}
					break;
				}

				case TB_SELECT_WINDOW: {
					bring_to_front(out_idx);
					window_t* w = &windows[out_idx];
					expand_rect(&dx1, &dy1, &dx2, &dy2, w->x, w->y, w->w, w->h);
					dirty = 1;
					break;
				}

				case TB_RETURN_TO_SHELL:
				case TB_OPEN_FILES:
				case TB_OPEN_SETTINGS:
				case TB_NONE:
				default:
					break;
			}

			if (action == TB_RETURN_TO_SHELL) {
				break; // exit desktop() back to the text-mode shell
			}

			if (taskbar_consumed) {
				// the start menu opening/closing (or a files/settings stub
				// click, harmless no-ops for now) can change what's on
				// screen in the taskbar/menu column, so fold that in too.
				// Union the BEFORE and AFTER footprints: closing shrinks the
				// panel's rect, so using only the post-click (narrower) rect
				// would leave stale menu pixels on screen unrepainted.
				int tx1, ty1, tw1, th1;
				taskbar_full_rect(fb_h, &tx1, &ty1, &tw1, &th1);

				expand_rect(&dx1, &dy1, &dx2, &dy2, tx0, ty0, tw0, th0);
				expand_rect(&dx1, &dy1, &dx2, &dy2, tx1, ty1, tw1, th1);
				dirty = 1;
				repaint_region(dx1, dy1, dx2 - dx1, dy2 - dy1);
			}
		}

		if (pressed && dragging_index == -1 && !taskbar_consumed) {
			for (int i = g_window_count - 1; i >= 0; i--) {
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

		int window_moved = 0;

		if (dragging_index != -1) {
			window_t* win = &windows[dragging_index];
			int nx = mx - drag_dx;
			int ny = my - drag_dy;
			if (nx < TASKBAR_W) nx = TASKBAR_W; // keep windows clear of the taskbar
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

		// the clock ticks even with no mouse activity at all, so the taskbar
		// needs its own periodic dirty check independent of the above
		uint8_t hh, mm, ss;
		clock_get(&hh, &mm, &ss);
		if (ss != last_ss) {
			last_ss = ss;
			int tx, ty, tw, th;
			taskbar_full_rect(fb_h, &tx, &ty, &tw, &th);
			expand_rect(&dx1, &dy1, &dx2, &dy2, tx, ty, tw, th);
			repaint_region(tx, ty, tw, th);
			dirty = 1;
		}

		if (window_moved) {
			repaint_region(dx1, dy1, dx2 - dx1, dy2 - dy1);
			cursor_invalidate();
		}

		if (cursor_moved) {
			cursor_update();
			last_mouse_x = mx;
			last_mouse_y = my;
		}

		if (dirty) vga_present_rect(backbuffer, dx1, dy1, dx2 - dx1, dy2 - dy1);

		prev_pressed = pressed;

		
	}

	vga_clear_draw_target();
	kfree(backbuffer);
	vga_clear();
	cursor_reset();
}
