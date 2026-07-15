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
#define ETP_I2C_REPORT_LEN  34     // body length. CONFIRMED against a real
                                   // capture: touch reports start with two
                                   // bytes that read as little-endian 0x0022
                                   // = 34 decimal - i.e. this device DOES send
                                   // a 2-byte length prefix in front of the
                                   // body (unlike the generic Linux elan_i2c
                                   // i2c_master_recv() path, which doesn't).
                                   // A prior pass here removed this prefix
                                   // based on the Linux driver read path and
                                   // that was wrong for this specific device -
                                   // put back based on real captured bytes.

#define ELAN_ADDR7          0x15
#define ELAN_DESC_LEN       30
#define ELAN_REPORT_MAX     40  // generous upper bound on report length

// raw_x/raw_y are now reconstructed 12-bit values (0-4095), per the real
// elan_i2c wire format - see the decode in elan_poll() below. 4095 is a
// generic upper bound; it's not this pad's true physical max_x/max_y
// (those come from the device itself via separate ETP_I2C_MAX_X_CMD /
// ETP_I2C_MAX_Y_CMD queries that this driver doesn't issue yet), so the
// cursor may not reach every screen edge until that's added. Re-run a
// corner-to-corner drag with elan_poll(1)'s raw dump to read off the
// real observed max and tighten this if needed.
#define ELAN_MAX_X 4095
#define ELAN_MAX_Y 4095

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
	// CONFIRMED against a real hardware capture: reports start with a
	// 2-byte little-endian length field (observed value 0x0022 = 34,
	// matching the body length below), THEN the body - read as one
	// continuous transaction so a re-latch can't tear the data across
	// two separate I2C transfers.
	uint8_t buf[2 + ETP_I2C_REPORT_LEN];
	if (i2c_dw_read(&elan_bus, ELAN_ADDR7, buf, sizeof(buf)) != 0)
		return;

	if (dump_raw) {
		vga_print("\nelan report: ");
		for (int i = 0; i < (int)sizeof(buf) && i < 16; i++) { print_hex8(buf[i]); vga_print(" "); }
	}

	int len = buf[0] | (buf[1] << 8);
	if (len != ETP_I2C_REPORT_LEN) return; // idle bus (0xFFFF) or a
	                                        // different report type/length
	                                        // (e.g. the 0x0040-length block
	                                        // also seen in captures) - skip
	                                        // it rather than misparse it.

	uint8_t* body = buf + 2;

	// --- Parse, matching the real elan_i2c wire format (verified against
	// the upstream Linux driver, elan_report_contact() in
	// elan_i2c_core.c):
	//
	//   body[0] = report ID (ETP_REPORT_ID, constant 0x5D) - this is what
	//             the earlier version of this function was reading as
	//             "status", which is why it always looked constant.
	//   body[1] = touch info byte (ETP_TOUCH_INFO_OFFSET) - real status.
	//   body[2..] = finger_data[0..4] (ETP_FINGER_DATA_OFFSET), 5 bytes
	//             per finger:
	//               finger_data[0] = (x_high_nibble << 4) | y_high_nibble
	//               finger_data[1] = x_low_byte
	//               finger_data[2] = y_low_byte
	//               finger_data[3] = width
	//               finger_data[4] = pressure
	//
	// The old code read finger_data[0] directly as raw_x (it's actually
	// the packed high-nibble byte, which only changes once every ~16
	// units of real movement - hence the "teleport" jumps) and
	// finger_data[1] as raw_y (it's actually X's low byte). Neither was
	// combined into the real 12-bit coordinate.
	uint8_t touch_info = body[1];
	uint8_t* finger_data = body + 2;

	// Verified against the real elan_i2c_core.c (elan_report_absolute()):
	// finger i's presence is bit (3+i) of the touch-info byte - so finger0
	// is bit3 (0x08) - NOT a finger-count value in the upper nibble (that
	// was wrong in the previous edit and is why the cursor froze: for an
	// ordinary single-finger touch bit3 is set but the upper nibble is 0,
	// so "finger_count >= 1" was always false and raw_x/raw_y never got
	// updated). Buttons are bit0=left, bit1=right, bit2=middle.
	int finger1_present = touch_info & 0x08;
	int btn_left_bit     = touch_info & 0x01;
	int btn_right_bit    = touch_info & 0x02;

	btn_left  = btn_left_bit  != 0;
	btn_right = btn_right_bit != 0;

	static int prev_finger_present = 0;

	// Defensive: this pad is polled with no interrupt and no "data ready"
	// handshake, so an occasional torn/misaligned I2C read can still pass
	// the length check above but hand back a garbage coordinate. A real
	// finger can't move more than a small fraction of the pad's full
	// range between two consecutive polls DURING A CONTINUOUS DRAG, so
	// treat any such single-sample jump as a suspect frame and hold the
	// last good position rather than snapping the cursor to it - this is
	// the direct cause of the "teleporting" cursor. A fresh touchdown
	// (finger wasn't present last poll) is exempt, since landing your
	// finger somewhere new on the pad is a legitimate large jump, not
	// corrupted data. If legit fast drags start getting rejected too,
	// raise MAX_JUMP.
	#define ELAN_MAX_JUMP (ELAN_MAX_X / 6)
	if (finger1_present) {
		int nx = ((finger_data[0] & 0xF0) << 4) | finger_data[1];
		int ny = ((finger_data[0] & 0x0F) << 8) | finger_data[2];
		if (!prev_finger_present) {
			raw_x = nx;
			raw_y = ny;
		} else {
			int dx = nx - raw_x, dy = ny - raw_y;
			if (dx < 0) dx = -dx;
			if (dy < 0) dy = -dy;
			if (dx <= ELAN_MAX_JUMP && dy <= ELAN_MAX_JUMP) {
				raw_x = nx;
				raw_y = ny;
			}
			// else: drop this sample, keep the last good raw_x/raw_y.
		}
	}
	prev_finger_present = finger1_present;

	// Two-finger data (finger2 present == touch_info & 0x10, i.e. bit4)
	// is scroll/gesture input on this pad rather than pointer movement -
	// not wired up to anything yet. Left as a hook: finger_data + 5
	// (i.e. body + 7) is where finger2's 5-byte packet starts, same
	// layout as finger1 above.
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
	// Elan's raw Y increases moving UP the pad (away from the user),
	// opposite of screen Y which increases downward - flip it here so
	// moving your finger up moves the cursor up.
	y = (screen_h - 1) - y;
	if (y < 0) y = 0;
	if (y >= screen_h) y = screen_h - 1;
	return y;
}

int elan_left_pressed(void) { return btn_left != 0; }
int elan_right_pressed(void) { return btn_right != 0; }
