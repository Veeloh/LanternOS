ENTRY(_start)

SECTIONS {
	/* must match ELF_USER_MIN_ADDR in elf.h - the loader refuses to
	   load segments outside 0x400000-0x800000 */
	. = 0x400000;

	.text : {
		*(.text)
	}

	.rodata : {
		*(.rodata)
	}

	.data : {
		*(.data)
	}

	.bss : {
		*(COMMON)
		*(.bss)
	}
}
