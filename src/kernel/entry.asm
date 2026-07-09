bits 32

; Multiboot 2 header - grub looks for this magic signature.
; We're on Multiboot 2 (not 1) specifically so GRUB gives us ACPI's RSDP
; as a tag (works under both legacy BIOS and UEFI/OVMF) instead of us
; having to find it ourselves via a legacy-BIOS-only memory scan.
; Spec: https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
MULTIBOOT2_MAGIC	equ 0xE85250D6
MULTIBOOT2_ARCH		equ 0            ; 0 = i386 protected mode
MULTIBOOT2_HDR_LEN	equ (multiboot_header_end - multiboot_header)
MULTIBOOT2_CHECKSUM	equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + MULTIBOOT2_HDR_LEN)

section .multiboot
align 8
multiboot_header:
	dd MULTIBOOT2_MAGIC
	dd MULTIBOOT2_ARCH
	dd MULTIBOOT2_HDR_LEN
	dd MULTIBOOT2_CHECKSUM

	; framebuffer request tag (type 5) - ask GRUB for a real linear
	; framebuffer instead of assuming legacy VGA text mode exists.
	align 8
	dw 5        ; type = framebuffer
	dw 0        ; flags
	dd 20       ; size (tag header + 3 dwords below)
	dd 0        ; width:  0 = no preference, let GRUB pick
	dd 0        ; height: 0 = no preference
	dd 32       ; depth:  prefer 32bpp, GRUB falls back if unavailable

	; end tag (type 0) - terminates the tag list
	align 8
	dw 0
	dw 0
	dd 8
multiboot_header_end:

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
