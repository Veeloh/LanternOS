org 0x7C00
bits 16


%define ENDL 0x0D, 0x0A

;
; FAT12 Header
;
jmp short start
nop

bdb_oem:		db 'MSWIN4.1'	; 8 bytes
bdb_bytes_per_sector:		dw 512
bdb_sectors_per_cluster:		db 1
bdb_reserved_sectors:		dw 1
bdb_fat_count:		db 2
bdb_dir_entries_count:		dw 0E0h
bdb_total_secrors:		dw 2880	; 2880 * 512 = 1.44MB
bdb_media_descriptor_type: 		db 0F0h	; F0 = 3.5" floppy disk
bdb_sectors_per_fat:		dw 9	; 9 sectors/fat
bdb_sectors_per_track:		dw 18
bdb_heads:		dw 2
bdb_hidden_sectors:		dd 0
bdb_large_sector_count:		dd 0

; extended boot record
ebr_drive_number:		db 0	; 0x00 = floppy, 0x80 = hdd
				db 0	; reserved
ebr_signatre:		db 29h
ebr_volume_id:		db 12h, 34h, 56h, 78h	; serial number. value does not matter
ebr_volume_label:		db 'IAMCOOL HAHA'	; 11 bytes. padded with spaces
ebr_system_id:		db 'FAT12   '	; 8 bytes

;
; Code Goes Here
;

start:
	; setup data segments
	mov ax, 0	; cant write to ds/es directly
	mov ds, ax
	mov es, ax

	; setup stack
	mov ss, ax
	mov sp, 0x7C00	; stack grows downwards from where we are loaded in memory

	; some BIOSes might start at 07C0:0000 instead of 0000:7C00 make sure we are in the
	; expected location
	push es
	push word .after
	retf
.after:

	; read something from disk
	; BIOS should set DL to drive number
	mov [ebr_drive_number], dl

	; print loading message
	mov si, msg_loading
	call puts

	; read drive parameters
	push es
	mov ah, 08h
	int 13h
	jc floppy_error
	pop es

	and cl, 0x3F
	xor ch, ch
	mov [bdb_sectors_per_track], cx

	inc dh
	mov [bdb_heads], dh

	; read FAT root directory
	mov ax, [bdb_sectors_per_fat]
	mov bl, [bdb_fat_count]
	xor bh, bh
	mul bx
	add ax, [bdb_reserved_sectors]
	push ax

	mov ax, [bdb_sectors_per_fat]
	shl ax, 5
	xor dx, dx
	div word [bdb_bytes_per_sector]

	test dx, dx
	jz .root_dir_after
	inc ax


.root_dir_after:


	; read root directory
	mov cl, al
	pop ax
	mov dl, [ebr_drive_number]
	mov bx, buffer
	call disk_read

	; search for kernel.bin (wanted dead or alive!)
	xor bx, bx
	mov di, buffer

.search_kernel:

	mov si, file_kernel_bin
	mov cx, 11
	push di
	repe cmpsb
	pop di
	je .found_kernel

	add di, 32
	inc bx
	cmp bx, [bdb_dir_entries_count]
	jl .search_kernel

	jmp kernel_not_found_error

.found_kernel:
	; di should have the address to the  entry
	mov ax, [di + 26]
	mov [kernel_cluster], ax

	; load FAT from disk into memory
	mov ax, [bdb_reserved_sectors]
	mov bx, buffer
	mov cl, [bdb_sectors_per_fat]
	mov dl, [ebr_drive_number]
	call disk_read

	; read kernel and process FAT chain
	mov bx, KERNEL_LOAD_SEGMENT
	mov es, bx
	mov bx, KERNEL_LOAD_OFFSET

.load_kernel_loop:
	; read next cluster
	mov ax, [kernel_cluster]

	; not nice :( hardcoded value
	add ax, 31


	mov cl, 1
	mov dl, [ebr_drive_number]
	call disk_read

	add bx, [bdb_bytes_per_sector]

	;compute location of next sector
	mov ax, [kernel_cluster]
	mov cx, 3
	mul cx
	mov cx, 2
	div cx

	mov si, buffer
	add si, ax
	mov ax, [ds:si]

	or dx, dx
	jz .even

.odd:
	shr ax, 4
	jmp .next_cluster_after

.even:
	and ax, 0x0FFF

.next_cluster_after:
	cmp ax, 0x0FF8
	jae .read_finish

	mov [kernel_cluster], ax
	jmp .load_kernel_loop

.read_finish:
	; jump to our kernel
	mov dl, [ebr_drive_number]	; boot device in dl
	; set segment registers
	mov ax, KERNEL_LOAD_SEGMENT
	mov ds, ax
	mov es, ax

	jmp KERNEL_LOAD_SEGMENT:KERNEL_LOAD_OFFSET

	jmp wait_key_and_reboot ; should never happen

	cli
	hlt

;
; Error Handler
;
floppy_error:
	mov si, msg_read_failed
	call puts
	jmp wait_key_and_reboot

kernel_not_found_error:
	mov si, msg_kernel_not_found
	call puts
	jmp wait_key_and_reboot

wait_key_and_reboot:
	mov ah, 0
	int 16h
	jmp 0FFFFh:0

.halt:
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

;
; DIsk Routines
;

;
; Converts an LBA address to a CHS adress
; Parameters:
;  - ax: LBA address
;  Returns:
;  - cx [bits 0-5]: sector number
;  - cx [bits 6=15]: cylinder
;  - dh: head
;

lba_to_chs:

	push ax
	push dx

	xor dx, dx ; dx = 0
	div word [bdb_sectors_per_track] ; ax = LBA / SectorsPerTrack
					; dx = LBA % SectorsPerTrack

	inc dx	; dx = LBA % SectorPerTrack + 1
	mov cx, dx	; cx = Sector

	xor dx, dx	; dx = 0
	div word [bdb_heads] ; ax = (LBA / SectorsPerTrack) / Heads = cylinder

	mov dh, dl	; dh =  = head
	mov ch, al	; ch = cylinder (lower 8 bits)
	shl ah, 6
	or cl, ah		; Put upper 2 bits of cylinder in CL

	pop ax
	mov dl, al
	pop ax
	ret

;
; Reads Disk
;
disk_read:

	push ax
	push bx
	push cx
	push dx
	push di

	push cx		; temporarily save CL (numbers of sectors in to read
	call lba_to_chs	; compute CHS
	pop ax		; AL = number of sectors to read
	mov ah, 02h
	mov di , 3	; retry count

.retry:
	pusha		; save all registers, we dont know what bios modifies
	stc		; set carry flag
	int 13h		; carry flag cleared = success
	jnc .done

	popa
	call disk_reset

	dec di
	test di, di
	jnz .retry

.fail:
	; after all attemps failed
	jmp floppy_error

.done:
	popa

	pop di
        pop dx
        pop cx
        pop bx
        pop ax
	ret

;
; Disk Reset
;
disk_reset:
	pusha
	mov ah, 0
	stc
	int 13h
	jc floppy_error
	popa
	ret

msg_loading: db 'Loading...', ENDL, 0
msg_read_failed: db 'Read From Disk Failed', ENDL, 0
msg_kernel_not_found: db 'STAGE2.BIN file not found!', ENDL, 0
file_kernel_bin: db 'STAGE2  BIN'
kernel_cluster: dw 0

KERNEL_LOAD_SEGMENT equ 0x2000
KERNEL_LOAD_OFFSET equ 0

times 510-($-$$) db 0
dw 0AA55h

buffer:

