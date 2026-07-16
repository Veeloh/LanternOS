#pragma once
#include <stdint.h>
#include "multiboot.h"

// NOTE: VGA_WIDTH/VGA_HEIGHT are now computed at runtime from the actual
// framebuffer resolution GRUB gives us (see vga_init). These are just
// fallback defaults some old code might reference.
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

typedef enum {
	VGA_BLACK			= 0,
	VGA_BLUE			= 1,
	VGA_GREEN			= 2,
	VGA_CYAN			= 3,
	VGA_RED				= 4,
	VGA_MAGENTA			= 5,
	VGA_BROWN			= 6,
	VGA_LIGHT_GREY		= 7,
	VGA_DARK_GREY		= 8,
	VGA_LIGHT_BLUE		= 9,
	VGA_LIGHT_GREEN		= 10,
	VGA_LIGHT_CYAN		= 11,
	VGA_LIGHT_RED		= 12,
	VGA_LIGHT_MAGENTA	= 13, 
	VGA_YELLOW			= 14,
	VGA_WHITE			= 15,
} vga_colour;

// Same public API as before - every existing caller (shell.c, elf.c,
// syscall.c, clock.c, main.c...) needs ZERO changes.
void vga_init(uint32_t mb_info_addr); // <-- only signature that changed
void vga_clear(void);
void vga_putchar(char c);
void vga_print(const char* str);
void vga_set_colour(vga_colour fg, vga_colour bg);
void vga_set_cursor(int x, int y);
void vga_hide_cursor(void);
void vga_get_cursor(int* x, int* y);

uint32_t vga_get_fb_width(void);
uint32_t vga_get_fb_height(void);
void vga_put_pixel(int x, int y, uint32_t rgb);
uint32_t vga_get_pixel(int x, int y);
void vga_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void vga_draw_rect(int x, int y, int w, int h, uint32_t rgb);
void vga_draw_text(int x, int y, const char* str, uint32_t fg, uint32_t bg);
void vga_set_draw_target(uint32_t* buf, int w, int h);
void vga_clear_draw_target(void);
void vga_present(uint32_t* buf);
void vga_present_rect(uint32_t* buf, int x, int y, int w, int h);

// --- text capture mode ---
//
// While active, vga_putchar()/vga_print() append into `buf` (NUL-terminated,
// up to max_len-1 bytes) instead of drawing glyphs to the real screen/back-
// buffer. This lets any code that already prints through vga_print - like
// shell.c's commands[] dispatcher - be reused verbatim to fill an app's own
// window buffer instead of the fullscreen text console. '\b' pops the last
// captured byte, same as the real console's backspace behaviour.
//
// Not re-entrant (one capture at a time, no nesting) - fine for shell.c's
// use, which begins/ends a capture within a single dispatch call.
void vga_capture_begin(char* buf, int max_len);
int  vga_capture_end(void); // returns number of bytes captured
