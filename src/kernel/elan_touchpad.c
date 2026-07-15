#include "elan_touchpad.h"
#include "i2c_dw.h"
#include "vga.h"

// ---- Fixed Elan I2C protocol command words --------------------------
// These are protocol constants for Elan's native I2C touchpad interface
// (distinct from generic HID-over-I2C - your dmesg showed the "elan_i2c"
// driver bound, not "i2c_hid", confirming this device uses Elan's own
// command set). Sent little-endian (low byte first).
#define ETP_I2C_RESET       0x0100
#define ETP_I2C_WAKE_UP     0x0800
#define ETP_I2C_SLEEP       0x0801
#define ETP_I2C_DESC_CMD    0x0001 // read HID descriptor (30 bytes)
#define ETP_I2C_SET_CMD     0x0300 // set mode: 3rd byte = mode value
#define ETP_ENABLE_ABS      0x0001 // absolute-mode value for SET_CMD
#define ETP_I2C_REPORT_LEN  34     // fixed body length, confirmed against
                                   // torvalds/linux drivers/input/mouse/elan_i2c_i2c.c:
                                   // get_report() is a PLAIN i2c_master_recv() of
                                   // exactly report_len bytes - no command word sent
                                   // first, and no length prefix on the response.

#define ELAN_ADDR7          0x15
#define ELAN_DESC_LEN       30
#define ELAN_REPORT_MAX     40  // generous upper bound on report length

// GUESS - this pad's physical raw-unit range. 12-bit fields max out at
// 4095, but real touchpads rarely use the full range. From your capture,
// observed X spanned roughly 300-1700 and Y roughly 450-2300 across a
// modest drag, so the true corner-to-corner range is likely somewhat
// wider than that. Tune these against your own min/max if the cursor
// clips before reaching screen edges, or never reaches them.
#define ELAN_MAX_X 3000
#define ELAN_MAX_Y 2000

static i2c_dw_t elan_bus;

static int raw_x = 0, raw_y = 0;
static int btn_left = 0, btn_right = 0;

static void le16_cmd(uint8_t* buf, uint16_t cmd) {
	buf[0] = cmd & 0xFF;
	buf[1] = (cmd >> 8) & 0xFF;
}

static void print_hex8(uint8_t v) {
	const char* hex = "0123456789ABCDEF";
	char b[3] = { hex[(v >> 4) & 0xF], hex[v & 0xF], 0 };
	vga_print(b);
}

int elan_init(void) {
	// I2CC MMIO base, confirmed from this machine's DSDT (\_SB.I2CC, AMD0010 UID 2).
	i2c_dw_init(&elan_bus, 0xFEDC4000);

	uint8_t cmd[3];

	// Reset. Real hardware needs a delay afterwards for the controller to
	// come back up (Linux waits ~100ms here) - our busy-wait is a rough
	// stand-in since this kernel has no sleep() yet.
	le16_cmd(cmd, ETP_I2C_RESET);
	if (i2c_dw_write(&elan_bus, ELAN_ADDR7, cmd, 2) != 0) {
		vga_print("\nelan: reset write failed (no ACK from 0x15?)");
		return -1;
	}
	for (volatile int i = 0; i < 30000000; i++); // crude delay, tune if flaky

	// Wake up.
	le16_cmd(cmd, ETP_I2C_WAKE_UP);
	if (i2c_dw_write(&elan_bus, ELAN_ADDR7, cmd, 2) != 0) {
		vga_print("\nelan: wake-up write failed");
		return -1;
	}
	for (volatile int i = 0; i < 5000000; i++);

	// Sanity check: read the HID descriptor. We don't parse it yet, just
	// confirm the bus round-trip works and print the first few bytes -
	// if these come back as 0xFF or garbage, the write_read sequencing
	// or slave address is wrong.
	uint8_t desc[ELAN_DESC_LEN];
	le16_cmd(cmd, ETP_I2C_DESC_CMD);
	if (i2c_dw_write_read(&elan_bus, ELAN_ADDR7, cmd, 2, desc, ELAN_DESC_LEN) != 0) {
		vga_print("\nelan: HID descriptor read failed");
		return -1;
	}
	vga_print("\nelan: descriptor bytes: ");
	for (int i = 0; i < 8; i++) { print_hex8(desc[i]); vga_print(" "); }

	// Enable absolute reporting mode.
	cmd[0] = ETP_I2C_SET_CMD & 0xFF;
	cmd[1] = (ETP_I2C_SET_CMD >> 8) & 0xFF;
	cmd[2] = ETP_ENABLE_ABS & 0xFF;
	if (i2c_dw_write(&elan_bus, ELAN_ADDR7, cmd, 3) != 0) {
		vga_print("\nelan: enable-absolute-mode write failed");
		return -1;
	}

	vga_print("\nelan: init sequence complete");
	return 0;
}

void elan_poll(int dump_raw) {
	// Confirmed against torvalds/linux drivers/input/mouse/elan_i2c_i2c.c:
	// elan_i2c_get_report() is a PLAIN i2c_master_recv() of exactly
	// report_len (34) bytes - no command word sent first, no length
	// prefix on the response. Two earlier versions of this function got
	// this wrong in opposite directions: the original treated the first
	// 2 bytes as a length header that doesn't exist (silently discarding
	// every report and leaving raw_x/raw_y stuck at 0,0); the next one
	// "fixed" that by sending a report-request command word first - but
	// that command word was accidentally set to the same value as
	// ETP_I2C_RESET, so every poll was quietly re-resetting the device
	// instead of reading it, which is why the dump looked identical on
	// every single line regardless of touch.
	uint8_t body[ETP_I2C_REPORT_LEN];
	if (i2c_dw_read(&elan_bus, ELAN_ADDR7, body, ETP_I2C_REPORT_LEN) != 0)
		return;

	if (dump_raw) {
		vga_print("\nelan report: ");
		for (int i = 0; i < ETP_I2C_REPORT_LEN && i < 16; i++) { print_hex8(body[i]); vga_print(" "); }
	}

	// --- Parse confirmed against real captured hardware output. ---
	// body[0] = status byte: bit3 = finger 1 present, bit4 = finger 2
	// present, bit5 = finger 3 present; bits0-2 = button state.
	// Each present finger is a 5-byte packet immediately following (in
	// finger-slot order): byte0 = (x_hi<<4)|y_hi (top nibbles of 12-bit
	// X/Y), byte1 = x_low, byte2 = y_low, byte3 = pressure, byte4 = width.
	uint8_t status = body[0];

	int finger1_present = status & 0x08;
	int finger2_present = status & 0x10;
	int finger3_present = status & 0x20;
	int physical_click  = status & 0x01;

	// This is a clickpad: one mechanical switch under the whole surface,
	// not separate left/right buttons. Confirmed against a real capture -
	// status 0x19 (bit0 click + finger1 + finger2) showed up consistently
	// during an actual two-finger click. So left vs. right isn't read from
	// the hardware at all; it's synthesized from how many fingers were
	// down at the moment of the click, same convention Windows/Linux
	// touchpad drivers use: 1 (or 0) fingers = left, 2+ fingers = right.
	int finger_count = (finger1_present ? 1 : 0) + (finger2_present ? 1 : 0) + (finger3_present ? 1 : 0);

	if (physical_click) {
		if (finger_count >= 2) { btn_left = 0; btn_right = 1; }
		else                   { btn_left = 1; btn_right = 0; }
	} else {
		btn_left = 0;
		btn_right = 0;
	}

	if (finger1_present) {
		uint8_t* f = body + 1;
		int x_hi = (f[0] >> 4) & 0xF;
		int y_hi = f[0] & 0xF;
		raw_x = (x_hi << 8) | f[1];
		raw_y = (y_hi << 8) | f[2];
	}

	// Two-finger data (finger2_present) is scroll/gesture input on this
	// pad rather than pointer movement - not wired up to anything yet.
	// Left as a hook: body + 6 is where finger2's 5-byte packet starts
	// when finger2_present is set, same byte layout as finger1 above.
}

int elan_get_raw_x(void) { return raw_x; }
int elan_get_raw_y(void) { return raw_y; }

int elan_get_x(void) {
	int screen_w = (int)vga_get_fb_width();
	if (screen_w <= 0) screen_w = 320;
	int x = (raw_x * screen_w) / ELAN_MAX_X;
	if (x < 0) x = 0;
	if (x >= screen_w) x = screen_w - 1;
	return x;
}

int elan_get_y(void) {
	int screen_h = (int)vga_get_fb_height();
	if (screen_h <= 0) screen_h = 200;
	int y = (raw_y * screen_h) / ELAN_MAX_Y;
	if (y < 0) y = 0;
	if (y >= screen_h) y = screen_h - 1;
	return y;
}

int elan_left_pressed(void) { return btn_left != 0; }
int elan_right_pressed(void) { return btn_right != 0; }
