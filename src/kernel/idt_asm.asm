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

extern keyboard_handler

global keyboard_isr
keyboard_isr:
	pusha
	call keyboard_handler
	popa
	iret

extern timer_handler

global timer_isr
timer_isr:
	pusha
	call timer_handler
	popa
	iret
