bits 32
global _start
extern kernel_main

section .text
_start:
    mov eax, _start
    add eax, (dummy_handler - _start)

    mov ecx, 256
    mov edi, idt
.fill_idt:
    mov [edi], ax
    mov word [edi+2], 0x08
    mov byte [edi+4], 0x00
    mov byte [edi+5], 0x8E
    shr eax, 16
    mov [edi+6], ax
    shl eax, 16
    add edi, 8
    loop .fill_idt

    mov eax, idt
    mov [idt_descriptor + 2], eax
    lidt [idt_descriptor]

    mov esp, 0x90000
    xor ebp, ebp
    call kernel_main
    cli
    hlt

dummy_handler:
    iret

idt_descriptor:
    dw 256*8 - 1
    dd 0

idt:
    times 256*8 db 0


