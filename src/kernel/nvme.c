#include "nvme.h"
#include "vga.h"

#define SECTOR_SIZE 512

typedef volatile struct {
	uint64_t cap;
	uint32_t vs, intms, intmc, cc, rsv0, csts, nssr, aqa;
	uint64_t asq, acq;
} __attribute__((packed)) nvme_regs_t;

typedef struct {
	uint32_t cdw0;
	uint32_t nsid;
	uint64_t rsv;
	uint64_t mptr;
	uint64_t prp1, prp2;
	uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} __attribute__((packed)) nvme_cmd_t;

typedef struct {
	uint32_t dw0, dw1;
	uint16_t sqhd, sqid;
	uint16_t cid, status;
} __attribute__((packed)) nvme_cpl_t;

static nvme_regs_t* regs = 0;
static uint32_t doorbell_stride = 4; // bytes, computed from CAP.DSTRD

static nvme_cmd_t  admin_sq[2] __attribute__((aligned(4096)));
static nvme_cpl_t  admin_cq[2] __attribute__((aligned(4096)));
static nvme_cmd_t  io_sq[2]    __attribute__((aligned(4096)));
static nvme_cpl_t  io_cq[2]    __attribute__((aligned(4096)));

static uint16_t admin_sq_tail = 0, admin_cq_head = 0, admin_cq_phase = 1;
static uint16_t io_sq_tail = 0, io_cq_head = 0, io_cq_phase = 1;

static volatile uint32_t* doorbell(int qid, int is_completion) {
	uint8_t* base = (uint8_t*)regs + 0x1000;
	return (volatile uint32_t*)(base + (2 * qid + is_completion) * doorbell_stride);
}

static void submit_admin(nvme_cmd_t* cmd) {
	admin_sq[admin_sq_tail] = *cmd;
	admin_sq_tail = (admin_sq_tail + 1) % 2;
	*doorbell(0, 0) = admin_sq_tail;
}

static uint16_t wait_admin_cpl(void) {
	while ((admin_cq[admin_cq_head].status & 1) == admin_cq_phase) ; // wait phase flip... see note below
	uint16_t status = admin_cq[admin_cq_head].status;
	admin_cq_head = (admin_cq_head + 1) % 2;
	if (admin_cq_head == 0) admin_cq_phase ^= 1;
	*doorbell(0, 1) = admin_cq_head;
	return status;
}

int nvme_init(pci_device_t* dev) {
	uint32_t cmdreg = pci_config_read32(dev->bus, dev->device, dev->function, 0x04);
	cmdreg |= (1 << 1) | (1 << 2);
	pci_config_write32(dev->bus, dev->device, dev->function, 0x04, cmdreg);

	uint32_t bar0 = dev->bar[0] & 0xFFFFFFF0;
	if (!bar0) { vga_print("\nnvme: no BAR0"); return 0; }
	regs = (nvme_regs_t*)bar0;

	doorbell_stride = 4 << ((regs->cap >> 32) & 0xF); // DSTRD field

	regs->cc &= ~1; // CC.EN = 0
	while (regs->csts & 1) ; // wait CSTS.RDY = 0

	for (int i = 0; i < 2; i++) { admin_sq[i] = (nvme_cmd_t){0}; admin_cq[i] = (nvme_cpl_t){0}; }

	regs->aqa = (1 << 16) | 1; // ACQS=2 entries (encoded as size-1), ASQS=2
	regs->asq = (uint32_t)admin_sq;
	regs->acq = (uint32_t)admin_cq;

	regs->cc = (0 << 4)  // CSS = NVM command set
	         | (0 << 7)  // MPS = 4KB pages
	         | (0 << 11) // AMS = round robin
	         | (6 << 16) // IOSQES = 2^6 = 64 bytes
	         | (4 << 20); // IOCQES = 2^4 = 16 bytes
	regs->cc |= 1; // EN = 1

	while (!(regs->csts & 1)) ; // wait RDY

	vga_print("\nnvme: controller ready, admin queue up");
	// NOTE: I/O queue creation (Create I/O CQ / Create I/O SQ admin
	// commands) still needs to be issued here before nvme_read_sector
	// will work - see comment below.
	return 1;
}

void nvme_read_sector(uint32_t lba, uint8_t* buf) {
	if (!regs) return;

	nvme_cmd_t cmd = {0};
	cmd.cdw0 = 0x02; // opcode: Read
	cmd.nsid = 1;
	cmd.prp1 = (uint32_t)buf;
	cmd.cdw10 = lba;
	cmd.cdw11 = 0;
	cmd.cdw12 = 0; // 1 block (NLB field = 0 means 1 block, zero-based)

	io_sq[io_sq_tail] = cmd;
	io_sq_tail = (io_sq_tail + 1) % 2;
	*doorbell(1, 0) = io_sq_tail;

	while ((io_cq[io_cq_head].status & 1) != io_cq_phase) ;
	io_cq_head = (io_cq_head + 1) % 2;
	if (io_cq_head == 0) io_cq_phase ^= 1;
	*doorbell(1, 1) = io_cq_head;
}

void nvme_write_sector(uint32_t lba, uint8_t* buf) {
	if (!regs) return;

	nvme_cmd_t cmd = {0};
	cmd.cdw0 = 0x01; // opcode: Write
	cmd.nsid = 1;
	cmd.prp1 = (uint32_t)buf;
	cmd.cdw10 = lba;
	cmd.cdw11 = 0;
	cmd.cdw12 = 0; // 1 block (NLB field = 0 means 1 block, zero-based)

	io_sq[io_sq_tail] = cmd;
	io_sq_tail = (io_sq_tail + 1) % 2;
	*doorbell(1, 0) = io_sq_tail;

	while ((io_cq[io_cq_head].status & 1) != io_cq_phase) ;
	io_cq_head = (io_cq_head + 1) % 2;
	if (io_cq_head == 0) io_cq_phase ^= 1;
	*doorbell(1, 1) = io_cq_head;
}
