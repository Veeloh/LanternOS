#include "mouse.h"
#include "idt.h"
#include "vga.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_CMD 0x64

static uint8_t inb(uint16_t port) {
	uint8_t value;
	__asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
	return value;
}

static void outb(uint16_t port, uint8_t value) {
	__asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void ps2_wait_write() {
	int timeout = 100000;
	while (timeout-- && (inb(PS2_STATUS) & 0x02));
}

static void ps2_wait_read() {
	int timeout = 100000;
	while (timeout-- && !(inb(PS2_STATUS) & 0x01));
}

static void mouse_write(uint8_t value) {
	ps2_wait_write();
	outb(PS2_CMD, 0xD4);
	ps2_wait_write();
	outb(PS2_DATA, value);
}

static uint8_t mouse_read() {
	ps2_wait_read();
	return inb(PS2_DATA);
}

static uint8_t packet[3];
static int packet_index = 0;

static int mouse_x = 0;
static int mouse_y = 0;
static int screen_w = 320;
static int screen_h = 200;

static int left_btn = 0;
static int right_btn = 0;
static int middle_btn = 0;

void mouse_handler() {
	uint8_t data = inb(PS2_DATA);

	if (packet_index == 0 && !(data & 0x08)) {
		outb(0xA0, 0x20);
		outb(0x20, 0x20);
		return;
	}

	packet[packet_index++] = data;

	if (packet_index == 3) {
		packet_index = 0;

		left_btn = packet[0] & 0x01;
		right_btn = packet[0] & 0x02;
		middle_btn = packet[0] & 0x04;

		if (!(packet[0] & 0xC0)) {
			int dx = packet[1];
			int dy = packet[2];

			if (packet[0] & 0x10) dx -= 256;
			if (packet[0] & 0x20) dy -= 256;

			mouse_x += dx;
			mouse_y -= dy;

			if (mouse_x < 0) mouse_x = 0;
			if (mouse_y < 0) mouse_y = 0;
			if (mouse_x >= screen_w) mouse_x = screen_w - 1;
			if (mouse_y >= screen_h) mouse_y = screen_h - 1;
		}
	}

	outb(0xA0, 0x20);
	outb(0x20, 0x20);
}

extern void mouse_isr();

void mouse_init() {
	while (inb(PS2_STATUS) & 0x01) inb(PS2_DATA);
	screen_w = (int)vga_get_fb_width();
	screen_h = (int)vga_get_fb_height();
	if (screen_w <= 0) screen_w = 320;
	if (screen_h <= 0) screen_h = 200;

	mouse_x = screen_w / 2;
	mouse_y = screen_h / 2;

	ps2_wait_write();
	outb(PS2_CMD, 0xA8);

	ps2_wait_write();
	outb(PS2_CMD, 0x20);
	ps2_wait_read();
	uint8_t config = inb(PS2_DATA);

	config |= 0x02;
	config &= ~0x20;

	ps2_wait_write();
	outb(PS2_CMD, 0x60);
	ps2_wait_write();
	outb(PS2_DATA, config);

	mouse_write(0xF6);
	mouse_read();

	mouse_write(0xF4);
	mouse_read();

	idt_set_handler(44, (uint32_t)mouse_isr);

	uint8_t mask = inb(0xA1);
	mask &= ~0x10;
	outb(0xA1, mask);
}

int mouse_get_x() {
	return mouse_x;
}

int mouse_get_y() {
	return mouse_y;
}

int mouse_left_pressed() {
	return left_btn != 0;
}

int mouse_right_pressed() {
	return right_btn != 0;
}

int mouse_middle_pressed() {
	return middle_btn != 0;
}
