#include "pmm.h"

extern uint32_t kernel_end;

static uint32_t* bitmap = 0;
static uint32_t total_pages = 0;
static uint32_t free_page_count = 0;

typedef struct {
	uint32_t size;
	uint32_t addr_low;
	uint32_t addr_high;
	uint32_t len_low;
	uint32_t len_high;
	uint32_t type;
} __attribute__((packed)) mmap_entry_t;

typedef struct {
	uint32_t flags;
	uint32_t mem_lower;
	uint32_t mem_upper;
	uint32_t boot_device;
	uint32_t cmdline;
	uint32_t mods_count;
	uint32_t mods_addr;
	uint32_t syms[4];
	uint32_t mmap_length;
	uint32_t mmap_addr;
} __attribute__((packed)) multiboot_info_t;

static void bitmap_set(uint32_t page) {
	bitmap[page / 32] |= (1 << (page % 32));
}

static void bitmap_clear(uint32_t page) {
	bitmap[page / 32] &= ~(1 << (page % 32));
}

static int bitmap_test(uint32_t page) {
	return bitmap[page / 32] & (1 << (page % 32));
}

void pmm_init(uint32_t multiboot_addr) {
	multiboot_info_t* mb = (multiboot_info_t*)multiboot_addr;

	//pass 1 - find highest ram address
	uint32_t highest = 0;
	mmap_entry_t* mmap = (mmap_entry_t*)mb->mmap_addr;
	while ((uint32_t)mmap < mb->mmap_addr + mb->mmap_length) {
		if (mmap->type == 1 && mmap->addr_high == 0) {
			uint32_t top = mmap->addr_low + mmap->len_low;
			if (top > highest) highest = top;
		}
		mmap = (mmap_entry_t*)((uint32_t)mmap + mmap->size + 4);
	}

	//calculate total pages
	total_pages = highest / PAGE_SIZE;

	//place bitmap right after kernel
	bitmap = (uint32_t*)&kernel_end;
	uint32_t bitmap_size = (total_pages / 32 + 1) * 4;

	//mark everything used
	for (uint32_t i = 0; i < total_pages / 32 + 1; i++) {
		bitmap[i] = 0xFFFFFFFF;
	}

	//pass 2; mark usable areas as free
	mmap = (mmap_entry_t*)mb->mmap_addr;
	while ((uint32_t)mmap < mb->mmap_addr + mb->mmap_length) {
		if (mmap->type == 1 && mmap->addr_high == 0) {
			uint32_t start = mmap->addr_low / PAGE_SIZE;
			uint32_t count = mmap->len_low / PAGE_SIZE;
			for (uint32_t i = start; i < start + count && i < total_pages; i++) {
				bitmap_clear(i);
				free_page_count++;
			}
		}
		mmap = (mmap_entry_t*)((uint32_t)mmap + mmap->size + 4);
	}

	//mark first 1MB as used
	for (uint32_t i = 0; i < 256; i++) {
		if (!bitmap_test(i)) {
			bitmap_set(i);
			free_page_count--;
		}
	}

	//mark kernel and bitmap as used
	uint32_t kernel_start_page = 0x100000 / PAGE_SIZE;
	uint32_t bitmap_end = ((uint32_t)&kernel_end + bitmap_size) / PAGE_SIZE + 1;
	for (uint32_t i = kernel_start_page; i <= bitmap_end; i++) {
		if (!bitmap_test(i)) {
			bitmap_set(i);
			free_page_count--;
		}
	}
}

uint32_t pmm_alloc_page() {
	for (uint32_t i = 0; i < total_pages; i++) {
		if (!bitmap_test(i)) {
			bitmap_set(i);
			free_page_count--;
			return i * PAGE_SIZE;
		}
	}
	return 0;
}

void pmm_free_page(uint32_t addr) {
	uint32_t page = addr / PAGE_SIZE;
	if (bitmap_test(page)) {
		bitmap_clear(page);
		free_page_count++;
	}
}

uint32_t pmm_free_pages() {
	return free_page_count;
}
