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
	; a.out kludge fields - GRUB's multiboot loader reads the header as a
	; fixed-offset struct and expects these 5 dwords to be physically
	; present even when bit16 (aout kludge) is NOT set - it just ignores
	; their values in that case. Omitting them shifts every field after,
	; so mode_type/width/height/depth get read from garbage past the
	; header. Values themselves don't matter here, just their presence.
	dd 0        ; header_addr
	dd 0        ; load_addr
	dd 0        ; load_end_addr
	dd 0        ; bss_end_addr
	dd 0        ; entry_addr
	; extra fields required because bit2 of flags is set:
	dd 0        ; mode_type: 0 = linear graphics framebuffer (1 = EGA text)
	dd 0        ; width: 0 = no preference, let GRUB pick what's available
	dd 0        ; height: 0 = no preference
	dd 32       ; depth: prefer 32bpp, but GRUB will fall back if unavailable

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
