#include "vga.h"
#include "font8x16.h"

#define FONT_W 8
#define FONT_H 16

static uint8_t* fb = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint8_t  fb_bpp = 32;

static int cols = 80;
static int rows = 25;
static int cursor_x = 0;
static int cursor_y = 0;

static uint8_t fg_colour = 15; // white
static uint8_t bg_colour = 0;  // black

// standard 16-colour VGA-ish palette, as 0xRRGGBB
static const uint32_t palette[16] = {
	0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
	0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
	0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
	0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t rgb) {
	if (x >= fb_width || y >= fb_height) return;
	uint8_t* p = fb + y * fb_pitch + x * (fb_bpp / 8);
	if (fb_bpp == 32) {
		*(uint32_t*)p = rgb;
	} else if (fb_bpp == 24) {
		p[0] = rgb & 0xFF;
		p[1] = (rgb >> 8) & 0xFF;
		p[2] = (rgb >> 16) & 0xFF;
	} else if (fb_bpp == 16) {
		uint16_t r = ((rgb >> 16) & 0xFF) >> 3;
		uint16_t g = ((rgb >> 8) & 0xFF) >> 2;
		uint16_t b = (rgb & 0xFF) >> 3;
		*(uint16_t*)p = (r << 11) | (g << 5) | b;
	}
}

static void draw_glyph(int cell_x, int cell_y, char c, uint8_t fg, uint8_t bg) {
	int index = (unsigned char)c - 32;
	const uint8_t* glyph;
	static const uint8_t blank[FONT_H] = {0};
	if (index < 0 || index >= 95) {
		glyph = blank;
	} else {
		glyph = font8x16[index];
	}

	uint32_t fgc = palette[fg & 0xF];
	uint32_t bgc = palette[bg & 0xF];

	uint32_t px = cell_x * FONT_W;
	uint32_t py = cell_y * FONT_H;

	for (int row = 0; row < FONT_H; row++) {
		uint8_t bits = glyph[row];
		for (int col = 0; col < FONT_W; col++) {
			int set = (bits >> (7 - col)) & 1;
			put_pixel(px + col, py + row, set ? fgc : bgc);
		}
	}
}

void vga_set_colour(vga_colour fg, vga_colour bg) {
	fg_colour = fg;
	bg_colour = bg;
}

void vga_clear() {
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < cols; x++)
			draw_glyph(x, y, ' ', fg_colour, bg_colour);
	cursor_x = 0;
	cursor_y = 0;
}

// Scroll the framebuffer up by one text row (FONT_H pixel rows) using a
// raw memmove of pixel data - much faster than redrawing every glyph.
static void vga_scroll() {
	uint32_t row_bytes = fb_pitch * FONT_H;
	uint8_t* dst = fb;
	uint8_t* src = fb + row_bytes;
	uint32_t bytes_to_move = fb_pitch * (fb_height - FONT_H);

	for (uint32_t i = 0; i < bytes_to_move; i++)
		dst[i] = src[i];

	// clear the newly exposed bottom text row
	for (int x = 0; x < cols; x++)
		draw_glyph(x, rows - 1, ' ', fg_colour, bg_colour);

	cursor_y = rows - 1;
}

void vga_putchar(char c) {
	if (c == '\n') {
		cursor_x = 0;
		cursor_y++;
	} else if (c == '\r') {
		cursor_x = 0;
	} else if (c == '\b') {
		if (cursor_x > 0) {
			cursor_x--;
			draw_glyph(cursor_x, cursor_y, ' ', fg_colour, bg_colour);
		}
	} else {
		draw_glyph(cursor_x, cursor_y, c, fg_colour, bg_colour);
		cursor_x++;
		if (cursor_x >= cols) {
			cursor_x = 0;
			cursor_y++;
		}
	}

	if (cursor_y >= rows) {
		vga_scroll();
	}
}

void vga_print(const char* str) {
	while (*str)
		vga_putchar(*str++);
}

// mbi = pointer to the multiboot info struct GRUB left in EBX (passed
// straight through from kernel_main's argument). Must have been requested
// via the video-mode bits in the multiboot header (see entry.asm), and
// GRUB must NOT be forced into gfxpayload=text (see grub.cfg).
void vga_init(multiboot_info_t* mbi) {
	if (mbi && (mbi->flags & MULTIBOOT_FLAG_FRAMEBUFFER) && mbi->framebuffer_addr) {
		fb = (uint8_t*)(uint32_t)mbi->framebuffer_addr;
		fb_pitch = mbi->framebuffer_pitch;
		fb_width = mbi->framebuffer_width;
		fb_height = mbi->framebuffer_height;
		fb_bpp = mbi->framebuffer_bpp;
	} else {
		// No framebuffer info from GRUB - fall back to a known-safe
		// default so we don't dereference garbage. This should not
		// happen once entry.asm/grub.cfg are updated correctly, but
		// it's a lot better than silently writing to nowhere.
		fb = 0;
		fb_pitch = 0;
		fb_width = 0;
		fb_height = 0;
	}

	cols = fb_width / FONT_W;
	rows = fb_height / FONT_H;
	if (cols <= 0) cols = 1;
	if (rows <= 0) rows = 1;

	fg_colour = VGA_WHITE;
	bg_colour = VGA_BLACK;

	if (fb) vga_clear();
}

void vga_set_cursor(int x, int y) {
	cursor_x = x;
	cursor_y = y;
}

void vga_hide_cursor() {
	// No hardware text-mode cursor exists on a linear framebuffer.
	// Left as a no-op so callers don't need to change. If you want a
	// blinking cursor, draw/erase a small filled rect at the cursor
	// cell from a timer callback instead.
}

void vga_get_cursor(int* x, int* y) {
	*x = cursor_x;
	*y = cursor_y;
}
