bits 32

global idt_load
global isr_default

idt_load:
	mov eax, [esp + 4] ; get pointer to idt_ptr
	lidt [eax]
	ret

isr_default:
	pusha ; save all registers
	; send end of interrupt to PIC
	mov al, 0x20
	out 0x20, al
	popa
	iret
