bits 32

; multi boot header - grub looks for this magic signature
MULTIBOOT_MAGIC		equ 0x1BADB002
MULTIBOOT_FLAGS		equ 0x00000003
MULTIBOOT_CHECKSUM	equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
align 4
	dd MULTIBOOT_MAGIC
	dd MULTIBOOT_FLAGS
	dd MULTIBOOT_CHECKSUM

section .text
global _start
extern kernel_main

_start:
	;test vga
	mov dword [0xB8000], 0x2F4F2F4B ;ok in green

	mov esp, stack_top
	xor ebp, ebp
;	push eax ; multiboot magic
;	push ebx ; multiboot info pointer
	call kernel_main
	cli
	hlt

section .bss
align 16
stack_bottom:
	resb 16384	; 16kb stack
stack_top:
