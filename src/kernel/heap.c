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

	block_header_t* current = heap_start;

	while(current) {
		if (current->free && current->size >= size) {
			if (current->size > size + sizeof(block_header_t) + 4) {
				//split block if large enough
				block_header_t* new_block = (block_header_t*)((uint8_t*)current + sizeof(block_header_t) + size);
				new_block->size = current->size - size - sizeof(block_header_t);
				new_block->free = 1;
				new_block->next = current->next;
				current->next = new_block;
				current->size = size;
			}
			current->free = 0;
			return (void*)((uint8_t*)current + sizeof(block_header_t));
		}
		current = current->next;
	}
	return 0; //out of memory
}

void kfree(void* ptr) {
	if (!ptr) return;

	block_header_t* header = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
	header->free = 1;

	//merge adjacent free blocks
	block_header_t* current = heap_start;
	while (current && current->next) {
		if (current->free && current->next->free) {
			current->size += sizeof(block_header_t) + current->next->size;
			current->next = current->next->next;
		} else {
			current = current->next;
		}
	}
}
