bits 32

global _start
extern kernel_main

section .text
_start:
	mov esp, 0x90000
	xor ebp, ebp
	call kernel_main
	cli
	hlt
