#include "gpt.h"
#include "disk.h"
#include "vga.h"

#define SECTOR_SIZE 512

// GPT header, LBA1. All multi-byte fields are little-endian, which
// matches x86 natively, so a packed struct cast straight onto the
// sector buffer works - same trick fat32.c already uses for the BPB.
typedef struct {
	uint8_t  signature[8];      // "EFI PART"
	uint32_t revision;
	uint32_t header_size;
	uint32_t header_crc32;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_usable_lba;
	uint64_t last_usable_lba;
	uint8_t  disk_guid[16];
	uint64_t partition_entry_lba;
	uint32_t num_partition_entries;
	uint32_t partition_entry_size;
	uint32_t partition_array_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
	uint8_t  type_guid[16];
	uint8_t  unique_guid[16];
	uint64_t starting_lba;
	uint64_t ending_lba;
	uint64_t attributes;
	uint16_t name[36]; // UTF-16LE, not needed for matching by type
} __attribute__((packed)) gpt_entry_t;

// EFI System Partition type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B.
// GPT stores GUIDs in "mixed-endian" form (first three fields
// little-endian, last two big-endian), so this is NOT the same byte
// order you'd get from just writing the GUID out left to right - this
// is the actual on-disk byte sequence for that GUID.
static const uint8_t ESP_TYPE_GUID[16] = {
	0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
	0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static int guid_eq(const uint8_t* a, const uint8_t* b) {
	for (int i = 0; i < 16; i++) if (a[i] != b[i]) return 0;
	return 1;
}

int gpt_find_esp(uint32_t* out_lba) {
	uint8_t buf[SECTOR_SIZE];

	disk_read_sector(1, buf);
	gpt_header_t* hdr = (gpt_header_t*)buf;

	static const uint8_t sig[8] = { 'E','F','I',' ','P','A','R','T' };
	for (int i = 0; i < 8; i++) {
		if (hdr->signature[i] != sig[i]) {
			vga_print("\ngpt: no GPT header at LBA1 (not a GPT disk?)");
			return 0;
		}
	}

	uint32_t entry_lba   = (uint32_t)hdr->partition_entry_lba;
	uint32_t entry_size  = hdr->partition_entry_size;
	uint32_t num_entries = hdr->num_partition_entries;

	if (entry_size == 0 || entry_size > SECTOR_SIZE) {
		vga_print("\ngpt: implausible partition entry size, bailing out");
		return 0;
	}

	uint32_t entries_per_sector = SECTOR_SIZE / entry_size;
	uint32_t sectors_to_scan = (num_entries + entries_per_sector - 1) / entries_per_sector;

	uint8_t entry_buf[SECTOR_SIZE];
	for (uint32_t s = 0; s < sectors_to_scan; s++) {
		disk_read_sector(entry_lba + s, entry_buf);
		for (uint32_t i = 0; i < entries_per_sector; i++) {
			gpt_entry_t* e = (gpt_entry_t*)(entry_buf + i * entry_size);
			if (guid_eq(e->type_guid, ESP_TYPE_GUID)) {
				*out_lba = (uint32_t)e->starting_lba;
				return 1;
			}
		}
	}

	vga_print("\ngpt: valid GPT found but no EFI System Partition entry");
	return 0;
}
