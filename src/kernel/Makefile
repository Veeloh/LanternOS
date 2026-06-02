BUILD_DIR?=build/
ASM?=nasm
CC?=gcc
LD?=ld

.PHONY: all kernel clean

all: kernel
kernel: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR)/kernel_entry.o: entry.asm
	$(ASM) entry.asm -f elf32 -o $(BUILD_DIR)/kernel_entry.o

$(BUILD_DIR)/kernel_vga.o: vga.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c vga.c -o $(BUILD_DIR)/kernel_vga.o

$(BUILD_DIR)/kernel_main.o: main.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0 -mno-red-zone -fno-stack-protector -c main.c -o $(BUILD_DIR)/kernel_main.o

$(BUILD_DIR)/kernel_idt.o: idt.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c idt.c -o $(BUILD_DIR)/kernel_idt.o

$(BUILD_DIR)/kernel_pic.o: pic.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c pic.c -o $(BUILD_DIR)/kernel_pic.o

$(BUILD_DIR)/kernel_idt_asm.o: idt_asm.asm
	$(ASM) idt_asm.asm -f elf32 -o $(BUILD_DIR)/kernel_idt_asm.o

$(BUILD_DIR)/kernel_keyboard.o: keyboard.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c keyboard.c -o $(BUILD_DIR)/kernel_keyboard.o
	
$(BUILD_DIR)/kernel_shell.o: shell.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c shell.c -o $(BUILD_DIR)/kernel_shell.o

	
$(BUILD_DIR)/kernel_timer.o: timer.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c timer.c -o $(BUILD_DIR)/kernel_timer.o

$(BUILD_DIR)/shell_clock.o: clock.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c clock.c -o $(BUILD_DIR)/shell_clock.o
	
$(BUILD_DIR)/kernel_gdt.o: gdt.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c gdt.c -o $(BUILD_DIR)/kernel_gdt.o

$(BUILD_DIR)/kernel_pmm.o: pmm.c
	$(CC) -m32 -ffreestanding -fno-pic -nostdlib -O0  -c pmm.c -o $(BUILD_DIR)/kernel_pmm.o

	
$(BUILD_DIR)/kernel_gdt_asm.o: gdt.asm
	$(ASM) gdt.asm -f elf32 -o $(BUILD_DIR)/kernel_gdt_asm.o

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel_entry.o $(BUILD_DIR)/kernel_main.o $(BUILD_DIR)/kernel_vga.o $(BUILD_DIR)/kernel_idt.o $(BUILD_DIR)/kernel_pic.o $(BUILD_DIR)/kernel_idt_asm.o $(BUILD_DIR)/kernel_keyboard.o $(BUILD_DIR)/kernel_shell.o $(BUILD_DIR)/kernel_timer.o $(BUILD_DIR)/shell_clock.o $(BUILD_DIR)/kernel_gdt_asm.o $(BUILD_DIR)/kernel_gdt.o $(BUILD_DIR)/kernel_pmm.o
	$(LD) -m elf_i386 -T linker.ld  \
		$(BUILD_DIR)/kernel_entry.o $(BUILD_DIR)/kernel_main.o $(BUILD_DIR)/kernel_vga.o  \
		$(BUILD_DIR)/kernel_idt.o $(BUILD_DIR)/kernel_pic.o $(BUILD_DIR)/kernel_idt_asm.o $(BUILD_DIR)/kernel_keyboard.o $(BUILD_DIR)/kernel_shell.o $(BUILD_DIR)/kernel_timer.o $(BUILD_DIR)/shell_clock.o $(BUILD_DIR)/kernel_gdt.o $(BUILD_DIR)/kernel_gdt_asm.o $(BUILD_DIR)/kernel_pmm.o \
		-o $(BUILD_DIR)/kernel.elf

clean:
	rm -f $(BUILD_DIR)/kernel*.o $(BUILD_DIR)/kernel.elf
