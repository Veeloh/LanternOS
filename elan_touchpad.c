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

#define ELAN_ADDR7          0x15
#define ELAN_DESC_LEN       30
#define ELAN_REPORT_MAX     40  // generous upper bound on report length

static i2c_dw_t elan_bus;

static int mouse_x = 0, mouse_y = 0;
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
	// HID-over-I2C style access: reading straight from the device (no
	// command byte written first) returns a 2-byte little-endian length
	// prefix followed by that many bytes of report data. If nothing is
	// ready, length reads back as 0.
	uint8_t buf[ELAN_REPORT_MAX];
	if (i2c_dw_read(&elan_bus, ELAN_ADDR7, buf, 2) != 0) return;

	int len = buf[0] | (buf[1] << 8);
	if (len == 0 || len > ELAN_REPORT_MAX - 2) return; // nothing ready / bogus

	if (i2c_dw_read(&elan_bus, ELAN_ADDR7, buf + 2, len) != 0) return;

	if (dump_raw) {
		vga_print("\nelan report: ");
		for (int i = 0; i < len + 2 && i < 16; i++) { print_hex8(buf[i]); vga_print(" "); }
	}

	// --- BEST-EFFORT PARSE - verify against the raw dump above and adjust. ---
	// Common Elan absolute-mode layout (offsets from start of report body,
	// i.e. buf[2] onward): byte0 = report id, byte1 bits0-2 = button state,
	// then per-finger 16-bit X, 16-bit Y. This assumes single-finger,
	// first-finger-slot data starting at body offset 1.
	if (len >= 5) {
		uint8_t* body = buf + 2;
		btn_left  = body[0] & 0x01;
		btn_right = body[0] & 0x02;
		int x = body[1] | (body[2] << 8);
		int y = body[3] | (body[4] << 8);
		if (x != 0 || y != 0) { mouse_x = x; mouse_y = y; }
	}
}

int elan_get_x(void) { return mouse_x; }
int elan_get_y(void) { return mouse_y; }
int elan_left_pressed(void) { return btn_left != 0; }
int elan_right_pressed(void) { return btn_right != 0; }
