#include "cursor.h"
#include "vga.h"
#include "mouse.h"
#include <stdint.h>

static const char cursor_shape[CURSOR_H][CURSOR_W] = {
	"B          ",
	"BB         ",
	"BWB        ",
	"BWWB       ",
	"BWWWB      ",
	"BWWWWB     ",
	"BWWWWWB    ",
	"BWWWWWWB   ",
	"BWWWWWWWB  ",
	"BWWWWWWWWB ",
	"BWWWWWBBBBB",
	"BWWBWWB    ",
	"BWB BWWB   ",
	"BB   BWWB  ",
	"B     BWWB ",
	"       BWWB",
	"        BB ",
};


static uint32_t saved[CURSOR_H][CURSOR_W];
static int saved_valid = 0;
static int last_x = -1000;
static int last_y = -1000;

static void restore_area(int x, int y) {
	for (int j = 0; j < CURSOR_H; j++) {
		for (int i = 0; i < CURSOR_W; i++) {
			vga_put_pixel(x + i, y + j, saved[j][i]);
		}
	}
}

static void save_area(int x, int y) {
	for (int j = 0; j < CURSOR_H; j++) {
		for (int i = 0; i < CURSOR_W; i++) {
			 saved[j][i] = vga_get_pixel(x + i, y + j);
		}
	}
}

static void draw_cursor(int x, int y) {
	for (int j = 0; j < CURSOR_H; j++) {
		for (int i = 0; i < CURSOR_W; i++) {
			char c = cursor_shape[j][i];
			if (c == 'B') vga_put_pixel(x + i, y + j, 0x000000);
			else if (c == 'W') vga_put_pixel(x + i, y + j, 0xFFEFD5);
		}
	}
}

void cursor_reset() {
	saved_valid = 0;
	last_x = -1000;
	last_y = -1000;
}

void cursor_invalidate() {
	saved_valid = 0;
}

void cursor_update() {
	int x = mouse_get_x();
	int y = mouse_get_y();

	if (x == last_x && y == last_y) return;

	if (saved_valid) restore_area(last_x, last_y);

	save_area(x, y);
	draw_cursor(x, y);

	saved_valid = 1;

	last_x = x;
	last_y = y;
}
