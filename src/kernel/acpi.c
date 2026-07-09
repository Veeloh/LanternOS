#include "acpi.h"
#include "vga.h"
#include "multiboot.h"

static void outb(uint16_t port, uint8_t val) { __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port)); }
static uint16_t inw(uint16_t port) { uint16_t v; __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static void outw(uint16_t port, uint16_t val) { __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port)); }

// ---- ACPI table layouts (only the fields we actually touch) ----------

typedef struct {
	char     signature[8]; // "RSD PTR "
	uint8_t  checksum;
	char     oem_id[6];
	uint8_t  revision;
	uint32_t rsdt_address;
} __attribute__((packed)) rsdp_t;

typedef struct {
	char     signature[4];
	uint32_t length;
	uint8_t  revision;
	uint8_t  checksum;
	char     oem_id[6];
	char     oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed)) sdt_header_t;

typedef struct {
	sdt_header_t header;
	uint32_t     other_sdt[]; // (header.length - sizeof(header)) / 4 entries
} __attribute__((packed)) rsdt_t;

// Only the FADT fields up through PM1b control block - everything past
// that (PM timer, reset register, etc.) we don't need for a poweroff.
typedef struct {
	sdt_header_t header;
	uint32_t firmware_ctrl;
	uint32_t dsdt;
	uint8_t  reserved0;
	uint8_t  preferred_pm_profile;
	uint16_t sci_interrupt;
	uint32_t smi_command_port;
	uint8_t  acpi_enable;
	uint8_t  acpi_disable;
	uint8_t  s4bios_req;
	uint8_t  pstate_control;
	uint32_t pm1a_event_block;
	uint32_t pm1b_event_block;
	uint32_t pm1a_control_block;
	uint32_t pm1b_control_block;
	// (fields after this point are unused by us and intentionally omitted)
} __attribute__((packed)) fadt_t;

#define SLP_EN (1u << 13)

static uint32_t pm1a_cnt = 0;
static uint32_t pm1b_cnt = 0;
static uint16_t slp_typa = 0;
static uint16_t slp_typb = 0;
static uint32_t smi_cmd = 0;
static uint8_t  acpi_enable_val = 0;
static int      ready = 0;

static int bytes_eq(const uint8_t* a, const char* b, int len) {
	for (int i = 0; i < len; i++) if (a[i] != (uint8_t)b[i]) return 0;
	return 1;
}

static uint8_t checksum(const uint8_t* p, uint32_t len) {
	uint8_t sum = 0;
	for (uint32_t i = 0; i < len; i++) sum += p[i];
	return sum;
}

// RSDP v1 and v2 both start with the same 20-byte layout our rsdp_t
// covers (signature/checksum/oem_id/revision/rsdt_address) - v2 just
// tacks on more fields after that (length, xsdt_address, ...) which we
// don't need for a poweroff, so treating either as an rsdp_t* is fine.
static rsdp_t* validated(void* candidate) {
	if (!candidate) return 0;
	if (!bytes_eq((uint8_t*)candidate, "RSD PTR ", 8)) return 0;
	if (checksum((uint8_t*)candidate, 20) != 0) return 0;
	return (rsdp_t*)candidate;
}

static rsdp_t* find_rsdp_legacy_scan(void) {
	// Legacy-BIOS-only fallback: RSDP lives on a 16-byte boundary
	// somewhere in the BIOS read-only area (0xE0000-0xFFFFF), or in the
	// first 1KB of the EBDA - check both. SeaBIOS populates this region;
	// UEFI firmware (e.g. OVMF) does not, so this only works when GRUB
	// itself was booted via legacy BIOS.
	uint16_t ebda_seg = *(volatile uint16_t*)0x40E;
	uint32_t ebda_addr = (uint32_t)ebda_seg << 4;

	uint32_t ranges[2][2] = {
		{ ebda_addr, ebda_addr + 1024 },
		{ 0xE0000, 0x100000 },
	};

	for (int r = 0; r < 2; r++) {
		if (ranges[r][0] == 0) continue;
		for (uint32_t addr = ranges[r][0]; addr < ranges[r][1]; addr += 16) {
			rsdp_t* found = validated((void*)addr);
			if (found) return found;
		}
	}
	return 0;
}

static rsdp_t* find_rsdp(uint32_t mb_info_addr) {
	// Preferred path: Multiboot2 hands us a verified copy of the RSDP it
	// found, regardless of whether GRUB itself booted via legacy BIOS or
	// UEFI. Try the ACPI 2.0+ tag first, then the ACPI 1.0 tag.
	mb2_tag_rsdp_t* new_tag = (mb2_tag_rsdp_t*)mb2_find_tag(mb_info_addr, MB2_TAG_ACPI_NEW_RSDP);
	rsdp_t* found = new_tag ? validated(new_tag->rsdp) : 0;
	if (found) return found;

	mb2_tag_rsdp_t* old_tag = (mb2_tag_rsdp_t*)mb2_find_tag(mb_info_addr, MB2_TAG_ACPI_OLD_RSDP);
	found = old_tag ? validated(old_tag->rsdp) : 0;
	if (found) return found;

	// Fallback, in case we're ever run under a bootloader that doesn't
	// supply the tag (e.g. testing without going through grub.cfg).
	return find_rsdp_legacy_scan();
}


static fadt_t* find_fadt(rsdt_t* rsdt) {
	int count = (rsdt->header.length - sizeof(sdt_header_t)) / 4;
	for (int i = 0; i < count; i++) {
		sdt_header_t* hdr = (sdt_header_t*)rsdt->other_sdt[i];
		if (bytes_eq((uint8_t*)hdr->signature, "FACP", 4)) { // FADT's actual signature is "FACP"
			return (fadt_t*)hdr;
		}
	}
	return 0;
}

// AML PkgLength encoding: if the top two bits of the lead byte are 0,
// the whole length is the low 6 bits and the encoding is 1 byte long.
// Otherwise the low 4 bits are the low nibble of the length and (top
// bits) more bytes follow. We only need to know how many bytes to skip,
// not the actual length value.
static int pkglength_encoding_size(uint8_t lead_byte) {
	return ((lead_byte & 0xC0) >> 6) + 1;
}

// Scans the DSDT's raw AML bytecode for the \_S5 package and pulls out
// SLP_TYPa/SLP_TYPb without a real AML interpreter. This is the standard
// hobby-OS approach to ACPI poweroff - full AML parsing is a project of
// its own, and the \_S5 package's shape is fixed and simple enough to
// pick out by scanning for the name and walking the few bytes after it.
static int parse_s5(uint32_t dsdt_addr) {
	sdt_header_t* dsdt = (sdt_header_t*)dsdt_addr;
	uint8_t* table = (uint8_t*)dsdt;
	uint32_t len = dsdt->length;

	uint8_t* p = table + sizeof(sdt_header_t);
	uint8_t* end = table + len - 4;

	for (; p < end; p++) {
		if (bytes_eq(p, "_S5_", 4)) break;
	}
	if (p >= end) return 0; // not found

	p += 4; // skip the name itself

	if (*p != 0x12 /* PackageOp */) return 0; // unexpected shape, bail out safely
	p++;

	p += pkglength_encoding_size(*p); // skip PkgLength encoding
	p++; // skip element count byte

	// Each of the next two elements is either a raw byte (if small
	// enough to omit the prefix) or a BytePrefix (0x0A) + byte.
	if (*p == 0x0A) p++;
	slp_typa = *p;
	p++;

	if (*p == 0x0A) p++;
	slp_typb = *p;

	return 1;
}

static void print_hex8(uint8_t v) {
	const char* hex = "0123456789ABCDEF";
	char buf[3] = { hex[(v >> 4) & 0xF], hex[v & 0xF], 0 };
	vga_print(buf);
}

int acpi_init(uint32_t mb_info_addr) {
	ready = 0;

	rsdp_t* rsdp = find_rsdp(mb_info_addr);
	if (!rsdp) { vga_print("\nacpi: RSDP not found"); return 0; }
	// ...rest of the function is unchanged

	rsdt_t* rsdt = (rsdt_t*)rsdp->rsdt_address;
	if (!bytes_eq((uint8_t*)rsdt->header.signature, "RSDT", 4) ||
	    checksum((uint8_t*)rsdt, rsdt->header.length) != 0) {
		vga_print("\nacpi: RSDT invalid"); return 0;
	}

	fadt_t* fadt = find_fadt(rsdt);
	if (!fadt) { vga_print("\nacpi: FADT not found"); return 0; }

	if (!parse_s5(fadt->dsdt)) { vga_print("\nacpi: couldn't parse \\_S5 in DSDT"); return 0; }

	pm1a_cnt = fadt->pm1a_control_block;
	pm1b_cnt = fadt->pm1b_control_block;
	smi_cmd = fadt->smi_command_port;
	acpi_enable_val = fadt->acpi_enable;

	ready = 1;
	vga_print("\nacpi: ready (SLP_TYPa=0x");
	print_hex8((uint8_t)slp_typa);
	vga_print(")");
	return 1;
}

void acpi_poweroff(void) {
	if (!ready) { vga_print("\nacpi: not initialized, can't power off"); return; }

	// If ACPI isn't already enabled (SCI_EN bit clear in PM1a event/status
	// setup), kick it on via the SMI command port and give it a moment.
	if (smi_cmd && acpi_enable_val && !(inw(pm1a_cnt) & 1)) {
		outb((uint16_t)smi_cmd, acpi_enable_val);
		for (volatile int i = 0; i < 1000000; i++) {
			if (inw(pm1a_cnt) & 1) break;
		}
	}

	outw((uint16_t)pm1a_cnt, (slp_typa << 10) | SLP_EN);
	if (pm1b_cnt) outw((uint16_t)pm1b_cnt, (slp_typb << 10) | SLP_EN);

	// If we're still executing, the write didn't take for some reason.
	// Nothing more we can portably do - halt so at least the CPU stops
	// spinning instead of running off into the weeds.
	vga_print("\nacpi: poweroff write sent but system is still running - halting");
	__asm__ volatile ("cli");
	while (1) __asm__ volatile ("hlt");
}
