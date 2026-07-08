bits 32

; multi boot header - grub looks for this magic signature
MULTIBOOT_MAGIC		equ 0x1BADB002
; bit0 = page-align modules, bit1 = provide memory info,
; bit2 = provide a video mode (REQUIRED so GRUB gives us a real
; linear framebuffer instead of assuming legacy VGA text mode exists)
MULTIBOOT_FLAGS		equ 0x00000007
MULTIBOOT_CHECKSUM	equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
align 4
	dd MULTIBOOT_MAGIC
	dd MULTIBOOT_FLAGS
	dd MULTIBOOT_CHECKSUM
	; extra fields required because bit2 of flags is set:
	dd 0        ; mode_type: 0 = linear graphics framebuffer (1 = EGA text)
	dd 1024     ; width
	dd 768      ; height
	dd 32       ; depth (bits per pixel)

section .text
global _start
extern kernel_main

_start:
	mov esp, stack_top
	xor ebp, ebp
	extern gdt_init
	call gdt_init
	push ebx ; multiboot info pointer (this is what kernel_main receives)
	call kernel_main
	cli
	hlt

section .bss
align 16
stack_bottom:
	resb 16384	; 16kb stack
stack_top:
