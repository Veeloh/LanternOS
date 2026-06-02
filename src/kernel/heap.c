#include "heap.h"
#include "pmm.h"

#define HEAP_START 0x200000 //2mb
#define HEAP_SIZE 0x100000 //1mb initall heap

typedef struct block_header {
	uint32_t size;
	uint8_t free;
	struct block_header* next;
} block_header_t;

static block_header_t* heap_start = 0;

void heap_init() {
	//allocate initial heap pages
	uint32_t pages = HEAP_SIZE / 4096;
	for (uint32_t i = 0; i < pages; i++) {
		pmm_alloc_page();
	}

	heap_start = (block_header_t*)HEAP_START;
	heap_start->size = HEAP_SIZE - sizeof(block_header_t);
	heap_start->free = 1;
	heap_start->next = 0;
}

void* kmalloc(uint32_t size) {
	
}
