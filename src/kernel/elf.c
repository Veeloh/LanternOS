#include "elf.h"
#include "fat32.h"
#include "heap.h"
#include "vga.h"

#define ELF_MAX_FILE_SIZE (256 * 1024) // bump if you start linking bigger programs

int elf_validate(Elf32_Ehdr* hdr) {
	if (hdr->e_ident[EI_MAG0] != ELFMAG0 || hdr->e_ident[EI_MAG1] != ELFMAG1 ||
	    hdr->e_ident[EI_MAG2] != ELFMAG2 || hdr->e_ident[EI_MAG3] != ELFMAG3) {
		vga_print("\nelf: bad magic, not an ELF file");
		return 0;
	}

	if (hdr->e_ident[EI_CLASS] != ELFCLASS32) {
		vga_print("\nelf: not 32-bit (SolOS only loads ELF32)");
		return 0;
	}

	if (hdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		vga_print("\nelf: not little-endian");
		return 0;
	}

	if (hdr->e_type != ET_EXEC) {
		vga_print("\nelf: not an ET_EXEC binary (no PIE/.so support yet)");
		return 0;
	}

	if (hdr->e_machine != EM_386) {
		vga_print("\nelf: wrong architecture, need x86 (EM_386)");
		return 0;
	}

	return 1;
}

uint32_t elf_load(uint8_t* image, uint32_t size) {
	if (size < sizeof(Elf32_Ehdr)) {
		vga_print("\nelf: file too small to be an ELF header");
		return 0;
	}

	Elf32_Ehdr* hdr = (Elf32_Ehdr*)image;
	if (!elf_validate(hdr)) return 0;

	if (hdr->e_phoff == 0 || hdr->e_phnum == 0) {
		vga_print("\nelf: no program headers");
		return 0;
	}

	if (hdr->e_phoff + (uint32_t)hdr->e_phnum * sizeof(Elf32_Phdr) > size) {
		vga_print("\nelf: program header table runs past end of file");
		return 0;
	}

	Elf32_Phdr* phdrs = (Elf32_Phdr*)(image + hdr->e_phoff);

	for (uint16_t i = 0; i < hdr->e_phnum; i++) {
		Elf32_Phdr* ph = &phdrs[i];

		if (ph->p_type != PT_LOAD) continue;
		if (ph->p_memsz == 0) continue;

		if (ph->p_offset + ph->p_filesz > size) {
			vga_print("\nelf: segment data runs past end of file");
			return 0;
		}

		if (ph->p_filesz > ph->p_memsz) {
			vga_print("\nelf: segment filesz > memsz, malformed binary");
			return 0;
		}

		// no paging yet -> p_vaddr is a real physical address.
		// keep it off the kernel and heap.
		uint32_t seg_end = ph->p_vaddr + ph->p_memsz;
		if (ph->p_vaddr < ELF_USER_MIN_ADDR || seg_end > ELF_USER_MAX_ADDR || seg_end < ph->p_vaddr) {
			vga_print("\nelf: segment address outside allowed user range (0x400000-0x800000)");
			return 0;
		}

		uint8_t* dest = (uint8_t*)ph->p_vaddr;
		uint8_t* src  = image + ph->p_offset;

		for (uint32_t b = 0; b < ph->p_filesz; b++)
			dest[b] = src[b];

		// zero .bss (the part of the segment with no file data)
		for (uint32_t b = ph->p_filesz; b < ph->p_memsz; b++)
			dest[b] = 0;
	}

	return hdr->e_entry;
}

int elf_exec(const char* filename) {
	uint8_t* buf = (uint8_t*)kmalloc(ELF_MAX_FILE_SIZE);
	if (!buf) {
		vga_print("\nelf: out of memory reading file");
		return -1;
	}

	int bytes = fat32_read_file(filename, buf, ELF_MAX_FILE_SIZE);
	if (bytes < 0) {
		vga_print("\nelf: file not found: ");
		vga_print(filename);
		kfree(buf);
		return -1;
	}

	uint32_t entry = elf_load(buf, (uint32_t)bytes);

	// segments have already been copied to their final addresses,
	// the raw file buffer isn't needed anymore.
	kfree(buf);

	if (entry == 0) {
		vga_print("\nelf: load failed");
		return -1;
	}

	vga_print("\nelf: jumping to entry point...\n");

	// NOTE: this is a direct call, not a real process launch.
	// process_spawn() exists for that, but context_switch (scheduler.asm)
	// is never actually invoked anywhere yet (process_schedule() only
	// flips state flags) and process_spawn()'s stack frame doesn't line
	// up with what context_switch reads (eip/eflags are never set in
	// the process_t struct). So for now we just call the entry point
	// like a function - it runs in ring 0, on the kernel's stack, and
	// the kernel resumes once it returns (or hangs forever if it loops,
	// same as test_process() does in main.c).
	void (*entry_point)() = (void (*)())entry;
	entry_point();

	vga_print("\nelf: program returned");
	return 0;
}
