#include "ahci.h"
#include "vga.h"

#define SECTOR_SIZE 512
#define AHCI_PORT_MAX 32

// --- HBA memory register layout (AHCI 1.3 spec) ---
typedef volatile struct {
	uint32_t clb, clbu, fb, fbu, is, ie, cmd, rsv0;
	uint32_t tfd, sig, ssts, sctl, serr, sact, ci, sntf, fbs;
	uint32_t rsv1[11];
	uint32_t vendor[4];
} __attribute__((packed)) hba_port_t;

typedef volatile struct {
	uint32_t cap, ghc, is, pi, vs, ccc_ctl, ccc_pts, em_loc, em_ctl, cap2, bohc;
	uint8_t  rsv[0xA0 - 0x2C];
	uint8_t  vendor[0x100 - 0xA0];
	hba_port_t ports[AHCI_PORT_MAX];
} __attribute__((packed)) hba_mem_t;

typedef struct {
	uint8_t  fis_type;
	uint8_t  pmport_c;
	uint8_t  command;
	uint8_t  featurel;
	uint8_t  lba0, lba1, lba2, device;
	uint8_t  lba3, lba4, lba5, featureh;
	uint8_t  countl, counth, icc, control;
	uint8_t  rsv[4];
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
	uint32_t dba, dbau, rsv0, dbc_rsv1;
} __attribute__((packed)) prdt_entry_t;

typedef struct {
	uint8_t cfis[64];
	uint8_t acmd[16];
	uint8_t rsv[48];
	prdt_entry_t prdt[1]; // single PRDT entry, one sector at a time for now
} __attribute__((packed)) cmd_table_t;

typedef struct {
	uint16_t flags;
	uint16_t prdtl;
	uint32_t prdbc;
	uint32_t ctba, ctbau;
	uint32_t rsv[4];
} __attribute__((packed)) cmd_header_t;

static hba_mem_t* hba = 0;
static int active_port = -1;

// Statically-allocated, aligned command structures - no heap/paging yet,
// so alignment attributes are all we need; physical == virtual address.
static cmd_header_t cmd_list[32] __attribute__((aligned(1024)));
static uint8_t fis_recv[256]     __attribute__((aligned(256)));
static cmd_table_t cmd_table     __attribute__((aligned(128)));

static void outl_(uint16_t port, uint32_t val) { __asm__ volatile ("outl %0, %1" :: "a"(val), "Nd"(port)); }

static int find_free_port(void) {
	uint32_t pi = hba->pi;
	for (int i = 0; i < AHCI_PORT_MAX; i++) {
		if (!(pi & (1 << i))) continue;
		uint32_t ssts = hba->ports[i].ssts;
		uint8_t det = ssts & 0x0F;
		uint8_t ipm = (ssts >> 8) & 0x0F;
		if (det == 3 && ipm == 1 && hba->ports[i].sig != 0xEB140101 /* skip ATAPI */)
			return i;
	}
	return -1;
}

static void port_stop(hba_port_t* p) {
	p->cmd &= ~0x0001; // ST
	p->cmd &= ~0x0010; // FRE
	while (p->cmd & 0x8000 /* CR */) ;
	while (p->cmd & 0x4000 /* FR */) ;
}

static void port_start(hba_port_t* p) {
	while (p->cmd & 0x8000);
	p->cmd |= 0x0010; // FRE
	p->cmd |= 0x0001; // ST
}

int ahci_init(pci_device_t* dev) {
	// enable Memory Space + Bus Master in the PCI command register
	uint32_t cmdreg = pci_config_read32(dev->bus, dev->device, dev->function, 0x04);
	cmdreg |= (1 << 1) | (1 << 2);
	pci_config_write32(dev->bus, dev->device, dev->function, 0x04, cmdreg);

	uint32_t abar = dev->bar[5] & 0xFFFFFFF0;
	if (!abar) { vga_print("\nahci: no ABAR"); return 0; }
	hba = (hba_mem_t*)abar;

	hba->ghc |= (1 << 31); // AE - AHCI enable

	active_port = find_free_port();
	if (active_port < 0) { vga_print("\nahci: no active SATA port found"); return 0; }

	hba_port_t* p = &hba->ports[active_port];
	port_stop(p);

	for (int i = 0; i < 32; i++) cmd_list[i] = (cmd_header_t){0};
	p->clb  = (uint32_t)cmd_list;
	p->clbu = 0;
	p->fb   = (uint32_t)fis_recv;
	p->fbu  = 0;

	p->serr = p->serr; // clear errors (write-1-to-clear, harmless here)
	p->is = p->is;
	hba->is = hba->is;

	port_start(p);

	vga_print("\nahci: port ");
	char c = '0' + active_port;
	char buf2[2] = {c, 0};
	vga_print(buf2);
	vga_print(" ready");
	return 1;
}

void ahci_read_sector(uint32_t lba, uint8_t* buf) {
	if (!hba || active_port < 0) return;
	hba_port_t* p = &hba->ports[active_port];

	while (p->tfd & 0x88); // wait BSY/DRQ clear

	cmd_header_t* hdr = &cmd_list[0];
	hdr->flags = 5; // FIS length in dwords (5) for a Register H2D FIS
	hdr->flags |= 0; // not a write
	hdr->prdtl = 1;
	hdr->prdbc = 0;
	hdr->ctba  = (uint32_t)&cmd_table;
	hdr->ctbau = 0;

	for (int i = 0; i < (int)sizeof(cmd_table_t); i++) ((uint8_t*)&cmd_table)[i] = 0;

	cmd_table.prdt[0].dba  = (uint32_t)buf;
	cmd_table.prdt[0].dbau = 0;
	cmd_table.prdt[0].dbc_rsv1 = (SECTOR_SIZE - 1) | (1u << 31); // byte count -1, IOC bit

	fis_reg_h2d_t* fis = (fis_reg_h2d_t*)cmd_table.cfis;
	fis->fis_type = 0x27; // Register H2D
	fis->pmport_c = 0x80; // "command" bit set
	fis->command  = 0x25; // READ DMA EXT
	fis->device   = 1 << 6; // LBA mode
	fis->lba0 = lba & 0xFF;
	fis->lba1 = (lba >> 8) & 0xFF;
	fis->lba2 = (lba >> 16) & 0xFF;
	fis->lba3 = (lba >> 24) & 0xFF;
	fis->lba4 = 0;
	fis->lba5 = 0;
	fis->countl = 1; // 1 sector
	fis->counth = 0;

	p->ci |= 1; // issue command slot 0

	while (p->ci & 1) {
		if (p->is & (1 << 30)) { vga_print("\nahci: task file error"); return; }
	}
}

void ahci_write_sector(uint32_t lba, uint8_t* buf) {
	if (!hba || active_port < 0) return;
	hba_port_t* p = &hba->ports[active_port];

	while (p->tfd & 0x88); // wait BSY/DRQ clear

	cmd_header_t* hdr = &cmd_list[0];
	hdr->flags = 5;       // FIS length in dwords (5) for a Register H2D FIS
	hdr->flags |= (1 << 6); // W - this is a write, host->device data flow
	hdr->prdtl = 1;
	hdr->prdbc = 0;
	hdr->ctba  = (uint32_t)&cmd_table;
	hdr->ctbau = 0;

	for (int i = 0; i < (int)sizeof(cmd_table_t); i++) ((uint8_t*)&cmd_table)[i] = 0;

	cmd_table.prdt[0].dba  = (uint32_t)buf;
	cmd_table.prdt[0].dbau = 0;
	cmd_table.prdt[0].dbc_rsv1 = (SECTOR_SIZE - 1) | (1u << 31); // byte count -1, IOC bit

	fis_reg_h2d_t* fis = (fis_reg_h2d_t*)cmd_table.cfis;
	fis->fis_type = 0x27; // Register H2D
	fis->pmport_c = 0x80; // "command" bit set
	fis->command  = 0x35; // WRITE DMA EXT
	fis->device   = 1 << 6; // LBA mode
	fis->lba0 = lba & 0xFF;
	fis->lba1 = (lba >> 8) & 0xFF;
	fis->lba2 = (lba >> 16) & 0xFF;
	fis->lba3 = (lba >> 24) & 0xFF;
	fis->lba4 = 0;
	fis->lba5 = 0;
	fis->countl = 1; // 1 sector
	fis->counth = 0;

	p->ci |= 1; // issue command slot 0

	while (p->ci & 1) {
		if (p->is & (1 << 30)) { vga_print("\nahci: task file error (write)"); return; }
	}
}
