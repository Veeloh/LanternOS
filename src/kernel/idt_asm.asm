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
extern process_get_current

global timer_isr
timer_isr:
	pusha ; save reg

	;save esp to currenr process
;	call process_get_current
;	mov [eax + 36], esp

	call timer_handler

	; get new current process
;	call process_get_current
;	mov esp, [eax + 36]
	
	popa
	iret

extern syscall_handler

global syscall_isr
syscall_isr:
	pusha
	push edx
	push ecx
	push ebx
	push eax
	call syscall_handler
	add esp, 16
	popa
	iret
