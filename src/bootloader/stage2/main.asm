org 0x0
bits 16


%define ENDL 0x0D, 0x0A

start:
	;set up segments
	mov ax, 0x2000
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7000
	
	; print message
	mov si, msg_hello
	call puts


	
	; enable A20
	mov ax, 0x2401
	int 0x15

	;load kernel
	mov ax, 0x1000
	mov es, ax
	mov bx, 0
	mov ah,0x02
	mov al, 9 ; 9 sectors
	mov ch, 0 ; cynlinder 0
	mov cl, 17 ; sector 34
	mov dh, 1 ; head 0
	mov dl, 0 ; floppy drive 0
	int 0x13
	jnc .kernel_loaded

	mov si, msg_kernel_error
	call puts
	cli
	hlt

	.kernel_loaded:
	


	
	;disable int
	cli
	
	
	;patch gdt
	; physical = (ds << 4) + offset = 0x20000 + gdt_start
	xor eax, eax
	mov ax, ds
	shl eax, 4
	add eax, gdt_start
	mov [gdt_descriptor + 2], eax

	; load GDT
	lgdt [gdt_descriptor]

	;switch to protected
	mov eax, cr0
	or eax, 1
	mov cr0, eax

	;far jump toflush pipeline and enter 32 bit mode
	db 0x66, 0xEA
	dd protected_mode + 0x20000
	dw 0x08
	

; 32 bit protected mode entry lol :) 
bits 32
protected_mode:
	;set all data segments to the data descriptor (0x10)
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov esp, 0x90000

	;vga test
	mov dword [0xB8000], 0x2F4F2F4B ; OK in green

	;J for jump
	mov byte [0xB8004], 'J'
	mov byte [0xB8005], 0x0F

	jmp 0x10000

	cli
	hlt




puts:
        ; save registers we will modify
        push si
        push ax

.loop:
        lodsb   ; loads next character in al
        or al, al       ; verify if next character is null?
        jz .done

        mov ah, 0x0e    ; call bios interrupt
        mov bh, 0
        int 0x10

        jmp .loop

.done:
        pop ax
        pop si
        ret

;gdt

gdt_start:
	dq 0

gdt_code:
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10011010b
	db 11001111b
	db 0x00

gdt_data:
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10010010b
	db 11001111b
	db 0x00

gdt_end:

gdt_descriptor:
	dw gdt_end - gdt_start - 1 ; size
	dd 0



msg_hello: db 'Stage 2 loaded, entering protected mode...', ENDL, 0
msg_kernel_error: db 'kernel load failed :/', 0x0D, 0x0A, 0


