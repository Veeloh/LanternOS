ASM=nasm
CC=gcc
LD=ld

SRC_DIR=src
BUILD_DIR=build

.PHONY: all iso kernel clean always

#
#iso image (so exciting)
#

iso: $(BUILD_DIR)/lanternos.iso

$(BUILD_DIR)/lanternos.iso: kernel
	cp $(BUILD_DIR)/kernel.elf isodir/boot/kernel.elf
	grub-mkrescue -o $(BUILD_DIR)/lanternos.iso isodir

#
#kernel directory
#
kernel: always
	$(MAKE) -C $(SRC_DIR)/kernel BUILD_DIR=$(abspath $(BUILD_DIR))

#
#always
#
always:
	mkdir -p $(BUILD_DIR)

#
#clean
#
clean:
	rm -rf $(BUILD_DIR)/*
