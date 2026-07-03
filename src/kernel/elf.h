#pragma once
#include <stdint.h>

#define EI_NIDENT 16

// e_ident[] indices
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5

#define ELFMAG0    0x7F
#define ELFMAG1    'E'
#define ELFMAG2    'L'
#define ELFMAG3    'F'

#define ELFCLASS32  1
#define ELFDATA2LSB 1

// e_type
#define ET_EXEC 2

// e_machine
#define EM_386 3

// p_type
#define PT_NULL 0
#define PT_LOAD 1

// p_flags
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
	uint8_t  e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint32_t e_entry;
	uint32_t e_phoff;
	uint32_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
	uint32_t p_type;
	uint32_t p_offset;
	uint32_t p_vaddr;
	uint32_t p_paddr;
	uint32_t p_filesz;
	uint32_t p_memsz;
	uint32_t p_flags;
	uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

// SolOS has no paging yet, so "load address" == real physical address.
// Keep user images well clear of the kernel image (loaded at 0x100000)
// and the kernel heap (0x200000 - 0x300000). pmm_reserve_region() is
// called on boot to keep the page allocator from handing this range
// out to anything else.
#define ELF_USER_MIN_ADDR 0x400000u
#define ELF_USER_MAX_ADDR 0x800000u

// Validates the ELF header. Returns 1 if this is something we can load
// (32-bit, little endian, x86, ET_EXEC), 0 otherwise.
int elf_validate(Elf32_Ehdr* hdr);

// Walks the program headers of an ELF image already sitting in memory
// at `image` and copies each PT_LOAD segment to its p_vaddr, zeroing
// the .bss tail (p_memsz - p_filesz). Returns the entry point (e_entry)
// on success, or 0 on failure.
uint32_t elf_load(uint8_t* image, uint32_t size);

// Reads `filename` off the FAT32 disk, loads it with elf_load(), and
// runs it. Returns 0 on success (program ran and returned / halted),
// -1 on failure.
int elf_exec(const char* filename);
