#include "emmc.h"
#include "vga.h"

// SDHCI (Simplified Host Controller Interface) register offsets -
// this is the standard register layout basically every eMMC/SD
// controller exposes, whether it's a laptop's built-in eMMC or
// QEMU's -device sdhci-pci.
#define SDMA_SYSTEM_ADDRESS    0x00
#define SDHCI_BLOCK_SIZE       0x04
#define SDHCI_BLOCK_COUNT      0x06
#define SDHCI_ARGUMENT         0x08
#define SDHCI_TRANSFER_MODE    0x0C
#define SDHCI_COMMAND          0x0E
#define SDHCI_RESPONSE0        0x10
#define SDHCI_PRESENT_STATE    0x24
#define SDHCI_POWER_CONTROL    0x29
#define SDHCI_CLOCK_CONTROL    0x2C
#define SDHCI_TIMEOUT_CONTROL  0x2E
#define SDHCI_SOFTWARE_RESET   0x2F
#define SDHCI_NORMAL_INT_STAT  0x30

#define PSTATE_CMD_INHIBIT     (1u << 0)
#define PSTATE_DAT_INHIBIT     (1u << 1)

#define INT_CMD_COMPLETE       (1u << 0)
#define INT_TRANSFER_COMPLETE  (1u << 1)
#define INT_ERROR              (1u << 15)

#define CLOCK_INTERNAL_EN      (1u << 0)
#define CLOCK_INTERNAL_STABLE  (1u << 1)
#define CLOCK_SD_EN            (1u << 2)

#define SRESET_ALL              (1u << 0)

static volatile uint8_t* base = 0;
static uint32_t rca = 0; // relative card address, assigned by us during init

static inline uint8_t  r8 (uint32_t off) { return base[off]; }
static inline uint16_t r16(uint32_t off) { return *(volatile uint16_t*)(base + off); }
static inline uint32_t r32(uint32_t off) { return *(volatile uint32_t*)(base + off); }
static inline void w8 (uint32_t off, uint8_t  v) { base[off] = v; }
static inline void w16(uint32_t off, uint16_t v) { *(volatile uint16_t*)(base + off) = v; }
static inline void w32(uint32_t off, uint32_t v) { *(volatile uint32_t*)(base + off) = v; }

static int wait16_set(uint32_t off, uint16_t mask, uint32_t timeout) {
	while (timeout--) if (r16(off) & mask) return 1;
	return 0;
}
static int wait8_clear(uint32_t off, uint8_t mask, uint32_t timeout) {
	while (timeout--) if (!(r8(off) & mask)) return 1;
	return 0;
}
static int wait32_clear(uint32_t off, uint32_t mask, uint32_t timeout) {
	while (timeout--) if (!(r32(off) & mask)) return 1;
	return 0;
}

static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++); }


static void print_hex16(uint16_t v) {
	const char* hex = "0123456789ABCDEF";
	char buf[5] = { hex[(v>>12)&0xF], hex[(v>>8)&0xF], hex[(v>>4)&0xF], hex[v&0xF], 0 };
	vga_print(buf);
}
static void print_hex32(uint32_t v) {
	print_hex16((v >> 16) & 0xFFFF);
	print_hex16(v & 0xFFFF);
}
// Sends a command and waits for Command Complete. resp_type: 0=none,
// 1=R2 (136-bit, used for CID), 2=R1/R1b/R3/R6 (48-bit). data=1 for
// commands that also move a data block (sets Data Present + DMA bits).
static int emmc_send_cmd(uint8_t cmd_index, uint32_t arg, int resp_type, int data) {
	if (!wait32_clear(SDHCI_PRESENT_STATE, PSTATE_CMD_INHIBIT, 500000)) {
		vga_print("\nemmc: CMD_INHIBIT never cleared"); return 0;
	}
	if (data && !wait32_clear(SDHCI_PRESENT_STATE, PSTATE_DAT_INHIBIT, 500000)) {
		vga_print("\nemmc: DAT_INHIBIT never cleared"); return 0;
	}

	w32(SDHCI_ARGUMENT, arg);

	uint16_t xfer_mode = 0;
	if (data) xfer_mode = (1 << 0) /* DMA enable */ | (1 << 1) /* block count enable */ | (1 << 4) /* read */;
	w16(SDHCI_TRANSFER_MODE, xfer_mode);

	uint16_t cmd_reg = (cmd_index << 8);
	if (resp_type == 1) cmd_reg |= (1 << 0);                       // R2: 136-bit
	if (resp_type == 2) cmd_reg |= (2 << 0) | (1 << 4) | (1 << 3); // R1: 48-bit + index/crc check
	if (data) cmd_reg |= (1 << 5); // data present

	w16(SDHCI_NORMAL_INT_STAT, 0xFFFF); // clear stale status
	w16(SDHCI_COMMAND, cmd_reg);

	if (!wait16_set(SDHCI_NORMAL_INT_STAT, INT_CMD_COMPLETE | INT_ERROR, 500000)) {
		vga_print("\nemmc: cmd"); print_hex16(cmd_index); // cmd_index prints as hex but easy enough to read
		vga_print(" timed out. present_state="); print_hex32(r32(SDHCI_PRESENT_STATE));
		vga_print(" clock_ctrl=");   print_hex16(r16(SDHCI_CLOCK_CONTROL));
		vga_print(" int_stat=");     print_hex16(r16(SDHCI_NORMAL_INT_STAT));
		vga_print(" power_ctrl=");   print_hex16((uint16_t)r8(SDHCI_POWER_CONTROL));
		return 0;
	}

	if (r16(SDHCI_NORMAL_INT_STAT) & INT_ERROR) {
		vga_print("\nemmc: command error"); w16(SDHCI_NORMAL_INT_STAT, 0xFFFF); return 0;
	}
	w16(SDHCI_NORMAL_INT_STAT, INT_CMD_COMPLETE);
	return 1;
}

int emmc_init(pci_device_t* dev) {
	uint32_t cmdreg = pci_config_read32(dev->bus, dev->device, dev->function, 0x04);
	cmdreg |= (1 << 1) | (1 << 2); // memory space + bus master
	pci_config_write32(dev->bus, dev->device, dev->function, 0x04, cmdreg);

	uint32_t bar0 = dev->bar[0] & 0xFFFFFFF0;
	if (!bar0) { vga_print("\nemmc: no BAR0"); return 0; }
	base = (volatile uint8_t*)bar0;

	w8(SDHCI_SOFTWARE_RESET, SRESET_ALL);
	if (!wait8_clear(SDHCI_SOFTWARE_RESET, SRESET_ALL, 1000000)) {
		vga_print("\nemmc: reset never completed"); return 0;
	}

w8(SDHCI_POWER_CONTROL, 0x0F);   // bus power on, 3.3V - do this FIRST
	spin(100000);                     // let power rail settle

	// Identification-speed clock (~400kHz) - cards won't respond
	// reliably above this until CMD1-CMD3 have negotiated further.
	w16(SDHCI_CLOCK_CONTROL, 0);
	w16(SDHCI_CLOCK_CONTROL, (0x80 << 8) | CLOCK_INTERNAL_EN);
	if (!wait16_set(SDHCI_CLOCK_CONTROL, CLOCK_INTERNAL_STABLE, 1000000)) {
		vga_print("\nemmc: internal clock never stabilized"); return 0;
	}
	w16(SDHCI_CLOCK_CONTROL, r16(SDHCI_CLOCK_CONTROL) | CLOCK_SD_EN);

	w8(SDHCI_TIMEOUT_CONTROL, 0x0E); // max timeout value
	spin(50000); // let SD clock actually reach the card before the first command
	
	if (!emmc_send_cmd(0, 0, 0, 0)) { vga_print("\nemmc: CMD0 (GO_IDLE) failed"); return 0; }

	// CMD1 (SEND_OP_COND) is MMC-specific (SD cards don't use it).
	// Loop until the card clears its busy bit (OCR response bit 31).
	uint32_t ocr_arg = 0x40FF8080; // sector/high-capacity mode + full voltage window
	int ready = 0;
	for (int i = 0; i < 100; i++) {
		if (!emmc_send_cmd(1, ocr_arg, 2, 0)) { vga_print("\nemmc: CMD1 failed"); return 0; }
		if (r32(SDHCI_RESPONSE0) & (1u << 31)) { ready = 1; break; }
		spin(50000);
	}
	if (!ready) { vga_print("\nemmc: card never left busy state (CMD1)"); return 0; }

	if (!emmc_send_cmd(2, 0, 1, 0)) { vga_print("\nemmc: CMD2 (ALL_SEND_CID) failed"); return 0; }

	rca = 1; // unlike SD, MMC cards don't pick their own RCA - the host assigns one
	if (!emmc_send_cmd(3, rca << 16, 2, 0)) { vga_print("\nemmc: CMD3 (SET_RELATIVE_ADDR) failed"); return 0; }
	if (!emmc_send_cmd(7, rca << 16, 2, 0)) { vga_print("\nemmc: CMD7 (SELECT_CARD) failed"); return 0; }

	vga_print("\nemmc: card selected, ready for block reads (default speed only)");
	return 1;
}

void emmc_read_sector(uint32_t lba, uint8_t* buf) {
	if (!base) return;

	w32(SDMA_SYSTEM_ADDRESS, (uint32_t)buf);
	w16(SDHCI_BLOCK_SIZE, 512);
	w16(SDHCI_BLOCK_COUNT, 1);

	// eMMC is block-addressed (byte addressing only applies to very
	// old <2GB standard-capacity cards - not handled here).
	if (!emmc_send_cmd(17 /* READ_SINGLE_BLOCK */, lba, 2, 1)) return;

	if (!wait16_set(SDHCI_NORMAL_INT_STAT, INT_TRANSFER_COMPLETE | INT_ERROR, 1000000)) {
		vga_print("\nemmc: read timed out"); return;
	}
	if (r16(SDHCI_NORMAL_INT_STAT) & INT_ERROR) vga_print("\nemmc: read error");
	w16(SDHCI_NORMAL_INT_STAT, 0xFFFF);
}
