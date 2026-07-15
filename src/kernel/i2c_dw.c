#include "i2c_dw.h"

// ---- DW_apb_i2c register offsets (word index, since we access via a
// volatile uint32_t* base and index by DWORD, not byte) -------------------
#define IC_CON            0x00
#define IC_TAR            0x01   // 0x04 / 4
#define IC_DATA_CMD       0x04   // 0x10 / 4
#define IC_INTR_STAT      0x0B   // 0x2C / 4
#define IC_INTR_MASK      0x0C   // 0x30 / 4
#define IC_RAW_INTR_STAT  0x0D   // 0x34 / 4
#define IC_CLR_INTR       0x10   // 0x40 / 4
#define IC_CLR_TX_ABRT    0x15   // 0x54 / 4
#define IC_CLR_STOP_DET   0x18   // 0x60 / 4
#define IC_ENABLE         0x1B   // 0x6C / 4
#define IC_STATUS         0x1C   // 0x70 / 4
#define IC_TXFLR          0x1D   // 0x74 / 4
#define IC_RXFLR          0x1E   // 0x78 / 4
#define IC_TX_ABRT_SOURCE 0x20   // 0x80 / 4

// IC_CON bits
#define IC_CON_MASTER_MODE   (1u << 0)
#define IC_CON_SPEED_STD     (1u << 1)
#define IC_CON_SPEED_FAST    (2u << 1)
#define IC_CON_RESTART_EN    (1u << 5)
#define IC_CON_SLAVE_DISABLE (1u << 6)

// IC_DATA_CMD bits
#define DATA_CMD_READ    (1u << 8)
#define DATA_CMD_STOP    (1u << 9)
#define DATA_CMD_RESTART (1u << 10)

// IC_STATUS bits
#define STATUS_ACTIVITY   (1u << 0)
#define STATUS_TFNF       (1u << 1) // transmit FIFO not full
#define STATUS_RFNE       (1u << 3) // receive FIFO not empty

static inline uint32_t rd(i2c_dw_t* bus, int reg) { return bus->base[reg]; }
static inline void wr(i2c_dw_t* bus, int reg, uint32_t val) { bus->base[reg] = val; }

void i2c_dw_init(i2c_dw_t* bus, uint32_t mmio_base) {
	bus->base = (volatile uint32_t*)mmio_base;

	// Disable controller before touching IC_CON (required by the spec).
	wr(bus, IC_ENABLE, 0);
	for (volatile int i = 0; i < 10000; i++); // let it settle

	// Master mode, fast-speed (400kHz - matches the 0x00061A80 Hz from the
	// Elan's I2cSerialBusV2 resource), restart enabled (needed for the
	// write-then-read register access pattern), slave mode disabled.
	// NOTE: we deliberately do NOT touch IC_FS_SCL_HCNT/LCNT - firmware
	// (coreboot, per the DSDT OEM ID) already programmed valid timing for
	// this bus since Linux's elan_i2c driver uses it successfully.
	uint32_t con = IC_CON_MASTER_MODE | IC_CON_SPEED_FAST |
	               IC_CON_RESTART_EN | IC_CON_SLAVE_DISABLE;
	wr(bus, IC_CON, con);

	// Clear any stale interrupt/abort state left over from firmware.
	(void)rd(bus, IC_CLR_TX_ABRT);
	(void)rd(bus, IC_CLR_STOP_DET);
	(void)rd(bus, IC_CLR_INTR);

	wr(bus, IC_ENABLE, 1);
	for (volatile int i = 0; i < 10000; i++);
}

static int wait_tfnf(i2c_dw_t* bus) {
	int timeout = 1000000;
	while (timeout-- && !(rd(bus, IC_STATUS) & STATUS_TFNF));
	return timeout > 0 ? 0 : -1;
}

static int wait_rfne(i2c_dw_t* bus) {
	int timeout = 1000000;
	while (timeout-- && !(rd(bus, IC_STATUS) & STATUS_RFNE));
	return timeout > 0 ? 0 : -1;
}

static int wait_tx_empty_and_idle(i2c_dw_t* bus) {
	int timeout = 1000000;
	while (timeout--) {
		if (rd(bus, IC_TXFLR) == 0 && !(rd(bus, IC_STATUS) & STATUS_ACTIVITY))
			return 0;
	}
	return -1;
}

static int check_abort(i2c_dw_t* bus) {
	if (rd(bus, IC_RAW_INTR_STAT) & (1u << 6)) { // TX_ABRT bit
		(void)rd(bus, IC_TX_ABRT_SOURCE);
		(void)rd(bus, IC_CLR_TX_ABRT);
		return -1;
	}
	return 0;
}

int i2c_dw_write(i2c_dw_t* bus, uint8_t addr7, const uint8_t* data, int len) {
	wr(bus, IC_TAR, addr7 & 0x7F);

	for (int i = 0; i < len; i++) {
		if (wait_tfnf(bus) != 0) return -1;
		uint32_t cmd = data[i];
		if (i == len - 1) cmd |= DATA_CMD_STOP;
		wr(bus, IC_DATA_CMD, cmd);
		if (check_abort(bus) != 0) return -1;
	}
	if (wait_tx_empty_and_idle(bus) != 0) return -1;
	return check_abort(bus);
}

int i2c_dw_read(i2c_dw_t* bus, uint8_t addr7, uint8_t* data, int len) {
	wr(bus, IC_TAR, addr7 & 0x7F);

	for (int i = 0; i < len; i++) {
		if (wait_tfnf(bus) != 0) return -1;
		uint32_t cmd = DATA_CMD_READ;
		if (i == len - 1) cmd |= DATA_CMD_STOP;
		wr(bus, IC_DATA_CMD, cmd);
		if (check_abort(bus) != 0) return -1;

		if (wait_rfne(bus) != 0) return -1;
		data[i] = (uint8_t)(rd(bus, IC_DATA_CMD) & 0xFF);
	}
	return 0;
}

int i2c_dw_write_read(i2c_dw_t* bus, uint8_t addr7,
                       const uint8_t* wbuf, int wlen,
                       uint8_t* rbuf, int rlen) {
	wr(bus, IC_TAR, addr7 & 0x7F);

	// Write phase - no STOP, so a repeated START precedes the read phase.
	for (int i = 0; i < wlen; i++) {
		if (wait_tfnf(bus) != 0) return -1;
		wr(bus, IC_DATA_CMD, wbuf[i]);
		if (check_abort(bus) != 0) return -1;
	}

	// Read phase.
	for (int i = 0; i < rlen; i++) {
		if (wait_tfnf(bus) != 0) return -1;
		uint32_t cmd = DATA_CMD_READ;
		if (i == 0) cmd |= DATA_CMD_RESTART;
		if (i == rlen - 1) cmd |= DATA_CMD_STOP;
		wr(bus, IC_DATA_CMD, cmd);
		if (check_abort(bus) != 0) return -1;

		if (wait_rfne(bus) != 0) return -1;
		rbuf[i] = (uint8_t)(rd(bus, IC_DATA_CMD) & 0xFF);
	}
	return 0;
}
