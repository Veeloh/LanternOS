BUILD_DIR?=build/
ASM?=nasm
CC?=gcc
LD?=ld

.PHONY: all kernel clean

all: kernel

kernel: $(BUILD_DIR)/kernel.bin

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

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel_entry.o $(BUILD_DIR)/kernel_main.o $(BUILD_DIR)/kernel_vga.o $(BUILD_DIR)/kernel_idt.o $(BUILD_DIR)/kernel_pic.o $(BUILD_DIR)/kernel_idt_asm.o
	$(LD) -m elf_i386 -Ttext 0x10000 --oformat binary -e _start \
		$(BUILD_DIR)/kernel_entry.o $(BUILD_DIR)/kernel_main.o $(BUILD_DIR)/kernel_vga.o  \
		$(BUILD_DIR)/kernel_idt.o $(BUILD_DIR)/kernel_pic.o $(BUILD_DIR)/kernel_idt_asm.o \
		-o $(BUILD_DIR)/kernel.bin

clean:
	rm -f $(BUILD_DIR)/kernel*.o $(BUILD_DIR)/kernel.bin
