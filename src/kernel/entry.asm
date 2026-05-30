bits 32
global _start
extern kernel_main

section .text
_start:
	mov eax, 0xB8000
	mov dword [eax], 0x2F412F41

	call kernel_main

hang:
	jmp hang
