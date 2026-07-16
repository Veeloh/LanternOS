#include "taskbar.h"
#include "window.h"
#include "vga.h"
#include "menubar.h"

// Same warm palette as window.c's chrome, so the taskbar/start menu look
// like they belong to the same desktop rather than a bolted-on strip.
#define BG_COLOUR              0xE67E22
#define PANEL_COLOUR           0xFFF8DC   // WIN_BODY_COLOUR
#define ACCENT_COLOUR          0xF39C12   // WIN_TITLE_COLOUR
#define BORDER_COLOUR          0x6E2C00   // WIN_BORDER_COLOUR
#define TEXT_DARK_COLOUR       0x4A2511   // WIN_TITLE_TEXT_COLOUR
#define TASKBAR_BG_COLOUR      BORDER_COLOUR
#define ICON_CHIP_COLOUR       PANEL_COLOUR
#define TEXT_LIGHT_COLOUR      PANEL_COLOUR

#define PAD          6
#define ICON_SZ      28
#define ICON_GAP     8
#define DIV_GAP      6
#define MAX_PINNED         3
#define MAX_VISIBLE_OPEN   3   // beyond this we show a "+N" overflow chip

#define SM_PANEL_X      (TASKBAR_W + 8)
#define SM_PANEL_Y      (MENUBAR_H + PAD) // sits just under the menu bar, was PAD (clipped into it)
#define SM_PANEL_W       360
#define SM_PANEL_H       220
#define SM_HEADER_H      36
#define SM_ICON_SZ       36

static int s_start_menu_open = 0;

// ---- shared layout (draw + hit-test both call this so they can't drift) ----
typedef struct {
	int icon_x;
	int power_y, terminal_y, battery_y;
	int start_y;
	int pinned_y[MAX_PINNED];
	int open_y[MAX_VISIBLE_OPEN];
	int open_slots;      // how many open-window icons are actually shown
	int overflow_y;      // -1 if there's no overflow chip
	int overflow_count;
	int date_y;
	int clock_y;
} tb_layout_t;

static void compute_layout(int fb_h, tb_layout_t* L) {
	L->icon_x = (TASKBAR_W - ICON_SZ) / 2;

	int y = PAD;
	y += ICON_SZ + ICON_GAP;         // sun logo (not interactive)
	y += DIV_GAP;                    // divider under sun logo

	L->power_y = y;    y += ICON_SZ + ICON_GAP;
	L->terminal_y = y; y += ICON_SZ + ICON_GAP;
	L->battery_y = y;  y += ICON_SZ + ICON_GAP;
	y += DIV_GAP;                    // divider

	L->start_y = y; y += ICON_SZ + ICON_GAP;

	for (int i = 0; i < MAX_PINNED; i++) {
		L->pinned_y[i] = y;
		y += ICON_SZ + ICON_GAP;
	}
	y += DIV_GAP;                    // divider

	int total_open = window_count();
	int show = total_open;
	int overflow = 0;
	if (show > MAX_VISIBLE_OPEN) {
		show = MAX_VISIBLE_OPEN - 1; // reserve one slot for the "+N" chip
		overflow = total_open - show;
	}
	L->open_slots = show;
	for (int i = 0; i < show; i++) {
		L->open_y[i] = y;
		y += ICON_SZ + ICON_GAP;
	}
	if (overflow > 0) {
		L->overflow_y = y;
		L->overflow_count = overflow;
		y += ICON_SZ + ICON_GAP;
	} else {
		L->overflow_y = -1;
		L->overflow_count = 0;
	}
	y += DIV_GAP;

	L->date_y = y;

	// clock is bottom-anchored: three stacked lines (HH / MM / SS)
	L->clock_y = fb_h - PAD - (3 * 14);
}

static int in_box(int mx, int my, int x, int y, int w, int h) {
	return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void fmt2(char* buf, int v) {
	if (v < 0) v = 0;
	if (v > 99) v = 99;
	buf[0] = '0' + (v / 10);
	buf[1] = '0' + (v % 10);
	buf[2] = 0;
}

// ---------------------------- icon drawing ----------------------------

static void draw_sun(int x, int y) {
	int cx = x + ICON_SZ / 2, cy = y + ICON_SZ / 2;
	vga_fill_rect(cx - 5, cy - 5, 10, 10, ACCENT_COLOUR);
	vga_fill_rect(cx - 1, y, 2, 4, ACCENT_COLOUR);
	vga_fill_rect(cx - 1, y + ICON_SZ - 4, 2, 4, ACCENT_COLOUR);
	vga_fill_rect(x, cy - 1, 4, 2, ACCENT_COLOUR);
	vga_fill_rect(x + ICON_SZ - 4, cy - 1, 4, 2, ACCENT_COLOUR);
}

static void draw_power_icon(int x, int y, uint32_t colour) {
	vga_draw_rect(x + 4, y + 6, ICON_SZ - 8, ICON_SZ - 10, colour);
	// gap at the top of the ring so it reads as a power glyph, not a box
	vga_fill_rect(x + ICON_SZ / 2 - 2, y + 5, 4, 3, TASKBAR_BG_COLOUR);
	vga_fill_rect(x + ICON_SZ / 2 - 1, y + 2, 2, 7, colour);
}

static void draw_terminal_icon(int x, int y, uint32_t colour) {
	vga_draw_rect(x + 2, y + 4, ICON_SZ - 4, ICON_SZ - 10, colour);
	vga_fill_rect(x + 5, y + 9, 2, 2, colour);
	vga_fill_rect(x + 8, y + 12, 2, 2, colour);
	vga_fill_rect(x + 5, y + 15, 2, 2, colour);
	vga_fill_rect(x + 12, y + 16, 8, 2, colour);
}

static void draw_battery_icon(int x, int y, int pct, int charging, uint32_t colour) {
	int bx = x + 3, by = y + 9, bw = ICON_SZ - 8, bh = 10;
	vga_draw_rect(bx, by, bw, bh, colour);
	vga_fill_rect(bx + bw, by + 3, 2, bh - 6, colour); // battery nub
	int fill_w = ((bw - 4) * (pct < 0 ? 0 : (pct > 100 ? 100 : pct))) / 100;
	if (fill_w > 0) vga_fill_rect(bx + 2, by + 2, fill_w, bh - 4, colour);
	if (charging) {
		// tiny stepped "bolt" drawn from rects since there's no line primitive yet
		vga_fill_rect(bx + bw / 2, by - 4, 2, 3, ACCENT_COLOUR);
		vga_fill_rect(bx + bw / 2 - 2, by - 2, 2, 3, ACCENT_COLOUR);
		vga_fill_rect(bx + bw / 2, by + 1, 2, 3, ACCENT_COLOUR);
	}
}

static void draw_start_icon(int x, int y, int open) {
	uint32_t bg = open ? ACCENT_COLOUR : TASKBAR_BG_COLOUR;
	uint32_t dot = open ? TASKBAR_BG_COLOUR : ICON_CHIP_COLOUR;
	vga_fill_rect(x, y, ICON_SZ, ICON_SZ, bg);
	vga_draw_rect(x, y, ICON_SZ, ICON_SZ, dot);
	int gx = x + 7, gy = y + 7, s = 6, gap = 4;
	vga_fill_rect(gx, gy, s, s, dot);
	vga_fill_rect(gx + s + gap, gy, s, s, dot);
	vga_fill_rect(gx, gy + s + gap, s, s, dot);
	vga_fill_rect(gx + s + gap, gy + s + gap, s, s, dot);
}

// small filled disc, built from vga_put_pixel since there's no circle
// primitive yet - used by the gear icon below
static void draw_filled_circle(int cx, int cy, int r, uint32_t colour) {
	for (int dy = -r; dy <= r; dy++) {
		for (int dx = -r; dx <= r; dx++) {
			if (dx * dx + dy * dy <= r * r) vga_put_pixel(cx + dx, cy + dy, colour);
		}
	}
}

// start-menu "Files" icon: a manila-folder silhouette (tab + body), drawn
// into an SM_ICON_SZ chip that the caller has already filled/outlined
static void draw_folder_icon(int x, int y, uint32_t colour) {
	int tab_x = x + 8, tab_y = y + 9;
	vga_fill_rect(tab_x, tab_y, 11, 4, colour);

	int body_x = x + 5, body_y = y + 13;
	int body_w = SM_ICON_SZ - 10, body_h = SM_ICON_SZ - 19;
	vga_fill_rect(body_x, body_y, body_w, body_h, colour);
}

// start-menu "Settings" icon: a gear - filled disc, 8 stubby teeth, and a
// punched-out centre hole (drawn in the chip's own background colour so
// it reads as a hole rather than a solid dot)
static void draw_gear_icon(int x, int y, uint32_t colour) {
	int cx = x + SM_ICON_SZ / 2, cy = y + SM_ICON_SZ / 2;
	int r = 8;

	draw_filled_circle(cx, cy, r, colour);

	// cardinal + diagonal teeth
	vga_fill_rect(cx - 2, y + 2,  4, 4, colour);              // N
	vga_fill_rect(cx - 2, y + SM_ICON_SZ - 6, 4, 4, colour);  // S
	vga_fill_rect(x + 2, cy - 2, 4, 4, colour);               // W
	vga_fill_rect(x + SM_ICON_SZ - 6, cy - 2, 4, 4, colour);  // E
	vga_fill_rect(cx - r - 1, cy - r - 1, 4, 4, colour);      // NW
	vga_fill_rect(cx + r - 2, cy - r - 1, 4, 4, colour);      // NE
	vga_fill_rect(cx - r - 1, cy + r - 2, 4, 4, colour);      // SW
	vga_fill_rect(cx + r - 2, cy + r - 2, 4, 4, colour);      // SE

	draw_filled_circle(cx, cy, 3, ICON_CHIP_COLOUR);          // centre hole
}

// start-menu "Text Editor" icon: a page silhouette (folded top-right
// corner) with a few horizontal "text line" strokes, drawn in the chip's
// own background colour so the lines read as gaps rather than solid bars
static void draw_document_icon(int x, int y, uint32_t colour) {
	int px = x + 9, py = y + 5;
	int pw = SM_ICON_SZ - 18, ph = SM_ICON_SZ - 10;
	vga_fill_rect(px, py, pw, ph, colour);
	vga_fill_rect(px + pw - 5, py, 5, 5, ICON_CHIP_COLOUR); // folded corner notch

	for (int i = 0; i < 3; i++) {
		vga_fill_rect(px + 3, py + 7 + i * 5, pw - 6, 2, ICON_CHIP_COLOUR);
	}
}

static void draw_app_chip(int x, int y, char letter, int highlighted) {
	vga_fill_rect(x, y, ICON_SZ, ICON_SZ, ICON_CHIP_COLOUR);
	vga_draw_rect(x, y, ICON_SZ, ICON_SZ, highlighted ? ACCENT_COLOUR : TASKBAR_BG_COLOUR);
	char s[2] = { letter, 0 };
	vga_draw_text(x + ICON_SZ / 2 - 4, y + ICON_SZ / 2 - 8, s, TEXT_DARK_COLOUR, ICON_CHIP_COLOUR);
}

// -------------------------------- draw ---------------------------------

void taskbar_draw(int fb_h, int battery_pct, int charging,
                   const char* weekday, int day,
                   uint8_t hh, uint8_t mm, uint8_t ss) {
	tb_layout_t L;
	compute_layout(fb_h, &L);

	vga_fill_rect(0, 0, TASKBAR_W, fb_h, TASKBAR_BG_COLOUR);

	draw_sun(L.icon_x, PAD);
	draw_power_icon(L.icon_x, L.power_y, TEXT_LIGHT_COLOUR);
	draw_terminal_icon(L.icon_x, L.terminal_y, TEXT_LIGHT_COLOUR);
	draw_battery_icon(L.icon_x, L.battery_y, battery_pct, charging, TEXT_LIGHT_COLOUR);

	int div_y = L.power_y - DIV_GAP - 1;
	vga_fill_rect(6, div_y, TASKBAR_W - 12, 1, TEXT_DARK_COLOUR);

	draw_start_icon(L.icon_x, L.start_y, s_start_menu_open);

	for (int i = 0; i < MAX_PINNED; i++) {
		vga_draw_rect(L.icon_x, L.pinned_y[i], ICON_SZ, ICON_SZ, TEXT_LIGHT_COLOUR);
	}

	int div2_y = L.pinned_y[0] - DIV_GAP - 1;
	vga_fill_rect(6, div2_y, TASKBAR_W - 12, 1, TEXT_DARK_COLOUR);

	int top_idx = window_top_index();
	for (int i = 0; i < L.open_slots; i++) {
		int raw_idx = window_z_order(i);
		window_t* w = window_at(raw_idx);
		char letter = (w && w->title && w->title[0]) ? w->title[0] : '?';
		draw_app_chip(L.icon_x, L.open_y[i], letter, raw_idx == top_idx);
	}
	if (L.overflow_y >= 0) {
		vga_fill_rect(L.icon_x, L.overflow_y, ICON_SZ, ICON_SZ, ICON_CHIP_COLOUR);
		char buf[4] = { '+', '0' + (L.overflow_count > 9 ? 9 : L.overflow_count), 0 };
		vga_draw_text(L.icon_x + 3, L.overflow_y + ICON_SZ / 2 - 8, buf, TEXT_DARK_COLOUR, ICON_CHIP_COLOUR);
	}

	int div3_y = L.date_y - DIV_GAP + 2;
	vga_fill_rect(6, div3_y, TASKBAR_W - 12, 1, TEXT_DARK_COLOUR);

	char daybuf[4];
	fmt2(daybuf, day);
	vga_draw_text(L.icon_x - 6, L.date_y, weekday, TEXT_LIGHT_COLOUR, TASKBAR_BG_COLOUR);
	vga_draw_text(L.icon_x - 2, L.date_y + 14, daybuf, TEXT_LIGHT_COLOUR, TASKBAR_BG_COLOUR);

	char hbuf[3], mbuf[3], sbuf[3];
	fmt2(hbuf, hh); fmt2(mbuf, mm); fmt2(sbuf, ss);
	vga_draw_text(L.icon_x - 2, L.clock_y,      hbuf, TEXT_LIGHT_COLOUR, TASKBAR_BG_COLOUR);
	vga_draw_text(L.icon_x - 2, L.clock_y + 14, mbuf, TEXT_LIGHT_COLOUR, TASKBAR_BG_COLOUR);
	vga_draw_text(L.icon_x - 2, L.clock_y + 28, sbuf, TEXT_LIGHT_COLOUR, TASKBAR_BG_COLOUR);

	if (s_start_menu_open) {
		vga_fill_rect(SM_PANEL_X, SM_PANEL_Y, SM_PANEL_W, SM_PANEL_H, PANEL_COLOUR);
		vga_draw_rect(SM_PANEL_X, SM_PANEL_Y, SM_PANEL_W, SM_PANEL_H, BORDER_COLOUR);

		// header: back-to-shell button (square chip with "/>" glyph), search bar, sun logo
		vga_fill_rect(SM_PANEL_X, SM_PANEL_Y, SM_PANEL_W, SM_HEADER_H, ACCENT_COLOUR);
		int hx = SM_PANEL_X + 7, hy = SM_PANEL_Y + 5;
		int hsz = 26;
		vga_fill_rect(hx, hy, hsz, hsz, PANEL_COLOUR);
		vga_draw_rect(hx, hy, hsz, hsz, BORDER_COLOUR);
		vga_draw_text(hx + (hsz - 16) / 2, hy + (hsz - 16) / 2, "/>", TEXT_DARK_COLOUR, PANEL_COLOUR);

		int search_x = SM_PANEL_X + 40, search_w = SM_PANEL_W - 40 - 40;
		vga_fill_rect(search_x, SM_PANEL_Y + 8, search_w, SM_HEADER_H - 16, PANEL_COLOUR);
		vga_draw_rect(search_x, SM_PANEL_Y + 8, search_w, SM_HEADER_H - 16, BORDER_COLOUR);
		vga_draw_text(search_x + 6, SM_PANEL_Y + 12, "search apps...", TEXT_DARK_COLOUR, PANEL_COLOUR);

		draw_sun(SM_PANEL_X + SM_PANEL_W - 34, SM_PANEL_Y + 3);

		// left icon column: file browser, settings
		int col_x = SM_PANEL_X + 12, col_y = SM_PANEL_Y + SM_HEADER_H + 12;
		vga_fill_rect(col_x, col_y, SM_ICON_SZ, SM_ICON_SZ, ICON_CHIP_COLOUR);
		vga_draw_rect(col_x, col_y, SM_ICON_SZ, SM_ICON_SZ, BORDER_COLOUR);
		draw_folder_icon(col_x, col_y, TEXT_DARK_COLOUR);

		int col2_y = col_y + SM_ICON_SZ + 10;
		vga_fill_rect(col_x, col2_y, SM_ICON_SZ, SM_ICON_SZ, ICON_CHIP_COLOUR);
		vga_draw_rect(col_x, col2_y, SM_ICON_SZ, SM_ICON_SZ, BORDER_COLOUR);
		draw_gear_icon(col_x, col2_y, TEXT_DARK_COLOUR);

		int col3_y = col2_y + SM_ICON_SZ + 10;
		vga_fill_rect(col_x, col3_y, SM_ICON_SZ, SM_ICON_SZ, ICON_CHIP_COLOUR);
		vga_draw_rect(col_x, col3_y, SM_ICON_SZ, SM_ICON_SZ, BORDER_COLOUR);
		draw_document_icon(col_x, col3_y, TEXT_DARK_COLOUR);

		// right panel: reserved for pinned/suggested apps
		int rp_x = col_x + SM_ICON_SZ + 12;
		int rp_w = SM_PANEL_X + SM_PANEL_W - 12 - rp_x;
		int rp_h = SM_PANEL_H - SM_HEADER_H - 24;
		vga_fill_rect(rp_x, col_y, rp_w, rp_h, ICON_CHIP_COLOUR);
		vga_draw_rect(rp_x, col_y, rp_w, rp_h, BORDER_COLOUR);
	}
}

// ------------------------------ hit-test --------------------------------

tb_action_t taskbar_handle_click(int mx, int my, int* out_window_idx) {
	int in_taskbar_column = (mx >= 0 && mx < TASKBAR_W);

	if (s_start_menu_open) {
		if (in_box(mx, my, SM_PANEL_X, SM_PANEL_Y, 40, SM_HEADER_H)) {
			s_start_menu_open = 0;
			return TB_RETURN_TO_SHELL;
		}
		int col_x = SM_PANEL_X + 12, col_y = SM_PANEL_Y + SM_HEADER_H + 12;
		if (in_box(mx, my, col_x, col_y, SM_ICON_SZ, SM_ICON_SZ)) {
			s_start_menu_open = 0; // launching an app closes the menu, like the back button does
			return TB_OPEN_FILES;
		}
		int col2_y = col_y + SM_ICON_SZ + 10;
		if (in_box(mx, my, col_x, col2_y, SM_ICON_SZ, SM_ICON_SZ)) {
			s_start_menu_open = 0;
			return TB_OPEN_SETTINGS;
		}
		int col3_y = col2_y + SM_ICON_SZ + 10;
		if (in_box(mx, my, col_x, col3_y, SM_ICON_SZ, SM_ICON_SZ)) {
			s_start_menu_open = 0;
			return TB_OPEN_TEXTEDIT;
		}

		if (in_box(mx, my, SM_PANEL_X, SM_PANEL_Y, SM_PANEL_W, SM_PANEL_H)) {
			return TB_NONE; // clicked elsewhere inside the panel - absorb it
		}
		if (!in_taskbar_column) {
			s_start_menu_open = 0; // click-away outside both taskbar and panel closes it
			return TB_NONE;
		}
		// else: fall through - let a taskbar-column click be handled normally below
	}

	if (!in_taskbar_column) return TB_NONE;

	// fb_h isn't known here, but every layout value we need (except the
	// bottom-anchored clock, which isn't clickable) doesn't depend on it -
	// pass a generously large fb_h so clock_y lands off in unclickable space.
	tb_layout_t L;
	compute_layout(100000, &L);

	if (in_box(mx, my, L.icon_x, L.power_y, ICON_SZ, ICON_SZ)) return TB_POWER;
	if (in_box(mx, my, L.icon_x, L.terminal_y, ICON_SZ, ICON_SZ)) return TB_OPEN_TERMINAL;
	if (in_box(mx, my, L.icon_x, L.start_y, ICON_SZ, ICON_SZ)) {
		s_start_menu_open = !s_start_menu_open;
		return TB_NONE;
	}
	for (int i = 0; i < L.open_slots; i++) {
		if (in_box(mx, my, L.icon_x, L.open_y[i], ICON_SZ, ICON_SZ)) {
			if (out_window_idx) *out_window_idx = window_z_order(i);
			return TB_SELECT_WINDOW;
		}
	}
	return TB_NONE;
}

int taskbar_start_menu_open(void) { return s_start_menu_open; }
void taskbar_close_start_menu(void) { s_start_menu_open = 0; }

void taskbar_full_rect(int fb_h, int* x, int* y, int* w, int* h) {
	if (s_start_menu_open) {
		*x = 0; *y = 0;
		*w = SM_PANEL_X + SM_PANEL_W;
		*h = fb_h;
	} else {
		*x = 0; *y = 0; *w = TASKBAR_W; *h = fb_h;
	}
}
