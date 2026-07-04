bits 32

global context_switch

; void context_switch(registers_t* old, registers_t* new)
;
; Only registers_t.esp (offset 28) is actually used. Everything else
; a process needs preserved (ebp, ebx, esi, edi) lives on that
; process's own stack, pushed/popped here - not stored in the struct.
; This is the standard minimal thread-switch trick: switching esp to
; a different stack, then `ret`, is enough to resume anywhere, because
; the return address AND the saved registers are already sitting on
; that stack from the last time we switched away from it.
context_switch:
	mov eax, [esp + 4] ; old regs*
	mov edx, [esp + 8] ; new regs*

	; save the caller's (old process's) callee-saved registers on
	; its own stack, then save the stack pointer itself.
	push ebp
	push ebx
	push esi
	push edi
	mov [eax + 28], esp ; registers_t.esp

	; switch to the new process's stack and restore its registers.
	mov esp, [edx + 28] ; registers_t.esp
	pop edi
	pop esi
	pop ebx
	pop ebp

	; pops the return address that's sitting on the new stack -
	; either a real return into wherever that process last called
	; context_switch from, or (for a never-run process) the fake
	; address process_bootstrap set up in process_spawn().
	ret
	
