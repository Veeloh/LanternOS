#pragma once
#include <stdint.h>

// ---- Multiboot 2 boot information (tag-based) --------------------------
// Full spec: https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
//
// Unlike Multiboot 1's fixed-offset struct, GRUB hands us a pointer (in
// EBX) to a structure that starts with { total_size, reserved }, followed
// by a sequence of tags. Each tag starts with { type, size } and tags are
// padded to 8-byte alignment. A tag of type 0 marks the end of the list.
//
// We moved to Multiboot 2 specifically so ACPI's RSDP comes from a tag
// (types 14/15 below) instead of a legacy-BIOS memory scan - that scan
// silently fails when booting under UEFI firmware (e.g. OVMF), since UEFI
// doesn't populate the classic 0xE0000-0xFFFFF "F-segment" the way SeaBIOS
// does. Tags work the same regardless of what firmware GRUB booted under.

#define MULTIBOOT2_MAGIC 0x36D76289 // value GRUB leaves in EAX on Multiboot2 boot

typedef struct {
	uint32_t total_size;
	uint32_t reserved;
} __attribute__((packed)) multiboot2_info_t;

typedef struct {
	uint32_t type;
	uint32_t size;
} __attribute__((packed)) multiboot2_tag_t;

#define MB2_TAG_END           0
#define MB2_TAG_MMAP          6
#define MB2_TAG_FRAMEBUFFER   8
#define MB2_TAG_ACPI_OLD_RSDP 14
#define MB2_TAG_ACPI_NEW_RSDP 15

typedef struct {
	uint32_t type;
	uint32_t size;
	uint32_t entry_size;
	uint32_t entry_version;
	// followed by (size - 16) / entry_size entries
} __attribute__((packed)) mb2_tag_mmap_t;

typedef struct {
	uint64_t addr;
	uint64_t len;
	uint32_t type; // 1 = available RAM
	uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

typedef struct {
	uint32_t type;
	uint32_t size;
	uint64_t framebuffer_addr;
	uint32_t framebuffer_pitch;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint8_t  framebuffer_bpp;
	uint8_t  framebuffer_type;
	uint16_t reserved;
	// colour info follows - unused here
} __attribute__((packed)) mb2_tag_framebuffer_t;

typedef struct {
	uint32_t type;
	uint32_t size;
	uint8_t  rsdp[]; // raw copy of the ACPI RSDP GRUB found (v1 or v2 shape)
} __attribute__((packed)) mb2_tag_rsdp_t;

// Walks the tag list looking for the first tag of `type`. Returns 0 if
// the info pointer is null or the tag isn't present.
static inline void* mb2_find_tag(uint32_t info_addr, uint32_t type) {
	if (!info_addr) return 0;
	multiboot2_info_t* info = (multiboot2_info_t*)info_addr;
	uint8_t* p = (uint8_t*)info_addr + 8; // skip total_size/reserved
	uint8_t* end = (uint8_t*)info_addr + info->total_size;

	while (p + sizeof(multiboot2_tag_t) <= end) {
		multiboot2_tag_t* tag = (multiboot2_tag_t*)p;
		if (tag->type == MB2_TAG_END) break;
		if (tag->type == type) return tag;
		p += (tag->size + 7) & ~7u; // tags are 8-byte aligned
	}
	return 0;
}
