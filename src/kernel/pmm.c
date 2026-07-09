#include "pmm.h"
#include "multiboot.h"

extern uint32_t kernel_end;

static uint32_t* bitmap = 0;
static uint32_t total_pages = 0;
static uint32_t free_page_count = 0;


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
	mb2_tag_mmap_t* mmap_tag = (mb2_tag_mmap_t*)mb2_find_tag(multiboot_addr, MB2_TAG_MMAP);
	uint32_t entry_count = mmap_tag ? (mmap_tag->size - sizeof(mb2_tag_mmap_t)) / mmap_tag->entry_size : 0;

	//pass 1 - find highest ram address
	uint32_t highest = 0;
	for (uint32_t i = 0; i < entry_count; i++) {
		mb2_mmap_entry_t* e = (mb2_mmap_entry_t*)((uint8_t*)mmap_tag + sizeof(mb2_tag_mmap_t) + i * mmap_tag->entry_size);
		if (e->type == 1 && (e->addr >> 32) == 0 && ((e->addr + e->len) >> 32) == 0) {
			uint32_t top = (uint32_t)e->addr + (uint32_t)e->len;
			if (top > highest) highest = top;
		}
	}

	total_pages = highest / PAGE_SIZE;
	bitmap = (uint32_t*)&kernel_end;
	uint32_t bitmap_size = (total_pages / 32 + 1) * 4;

	for (uint32_t i = 0; i < total_pages / 32 + 1; i++) {
		bitmap[i] = 0xFFFFFFFF;
	}

	//pass 2; mark usable areas as free
	for (uint32_t i = 0; i < entry_count; i++) {
		mb2_mmap_entry_t* e = (mb2_mmap_entry_t*)((uint8_t*)mmap_tag + sizeof(mb2_tag_mmap_t) + i * mmap_tag->entry_size);
		if (e->type == 1 && (e->addr >> 32) == 0 && ((e->addr + e->len) >> 32) == 0) {
			uint32_t start = (uint32_t)e->addr / PAGE_SIZE;
			uint32_t count = (uint32_t)e->len / PAGE_SIZE;
			for (uint32_t j = start; j < start + count && j < total_pages; j++) {
				bitmap_clear(j);
				free_page_count++;
			}
		}
	}
	// ...rest (mark first 1MB used, mark kernel used) is unchanged

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


void pmm_reserve_region(uint32_t start_addr, uint32_t end_addr) {
	uint32_t start_page = start_addr / PAGE_SIZE;
	uint32_t end_page = (end_addr + PAGE_SIZE - 1) / PAGE_SIZE;

	if (end_page > total_pages) end_page = total_pages;

	for (uint32_t i = start_page; i < end_page; i++) {
		if (!bitmap_test(i)) {
			bitmap_set(i);
			free_page_count--;
		}
	}
}
