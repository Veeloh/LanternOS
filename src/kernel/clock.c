#include "clock.h"
#include "vga.h"

static uint8_t hours = 0;
static uint8_t minutes = 0;
static uint8_t seconds = 0;
static uint32_t tick_count = 0;

static void print_two_digits(uint8_t n) {
	vga_putchar('0' + (n / 10));
	vga_putchar('0' + (n % 10));
}

void clock_set(uint8_t h, uint8_t m, uint8_t s) {
	hours = h;
	minutes = m;
	seconds = s;
}

void clock_get(uint8_t* h, uint8_t* m, uint8_t* s) {
	*h = hours;
	*m = minutes;
	*s = seconds;
}

void clock_init(uint8_t h, uint8_t m, uint8_t s) {
	clock_set(h, m, s);
	tick_count = 0;
}

void clock_tick() {
	tick_count++;
	if (tick_count < 100) return;
	tick_count = 0;

	seconds++;
	if (seconds >= 60) {
		seconds = 0;
		minutes++;
	}
	if (minutes >= 60) {
		minutes = 0;
		hours++;
	}
	if (hours >= 24) {
		hours = 0;
	}
	
//	clock_draw();
}

void clock_draw() {
//	int saved_colour = 0x0B; //light cyan
	int old_x, old_y;
	vga_get_cursor(&old_x, &old_y);
	

	vga_set_cursor(71, 0);
	vga_set_colour(VGA_LIGHT_CYAN, VGA_BLACK);
	print_two_digits(hours);
	vga_putchar(':');
	print_two_digits(minutes);
	vga_putchar(':');
	print_two_digits(seconds);

	vga_set_cursor(old_x, old_y);
	vga_set_colour(VGA_WHITE, VGA_BLACK);
}
