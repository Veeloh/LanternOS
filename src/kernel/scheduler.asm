bits 32

global context_switch
extern process_get_current

;context_swicth(process old, process new)
;saves old reg, loads new ones
context_switch:
	;get params
	mov eax, [esp + 4] ; old process
	mov edx, [esp + 8] ; new process

	;save old registers
	mov [eax], eax ; placeholder
	mov [eax + 4], ebx
	mov [eax + 8], ecx
	mov [eax + 12], edx
	mov [eax + 16], esi
	mov [eax + 20], edi
	mov [eax + 24], ebp
	mov [eax + 28], esp

	mov ecx, [esp]
	mov [eax + 36], ecx

	;load new process
	mov ebx, [edx + 4]
	mov ecx, [edx + 8]
	mov esi, [edx + 16]
	mov edi, [edx + 20]
	mov ebp, [edx + 24]
	mov esp, [edx + 28]

	;restore eflags
	push dword [edx + 36]
	popf

	;jmp to new process
	jmp dword [edx + 32]
