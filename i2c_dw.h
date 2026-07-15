#pragma once
#include <stdint.h>

// Minimal polling-mode driver for the Synopsys DesignWare I2C controller
// (this is what AMD exposes under ACPI HID "AMD0010"). Register layout is
// the standard DW_apb_i2c map, not device-specific.
//
// On this machine: \_SB.I2CC = AMD0010:02, MMIO 0xFEDC4000, len 0x1000, IRQ 6.
// We do NOT use the interrupt here - everything is polled, since wiring the
// real completion IRQ isn't required for simple blocking transactions.

typedef struct {
	volatile uint32_t* base; // mapped MMIO base, word-addressed access
} i2c_dw_t;

// mmio_base: physical address of the controller's register window
//            (e.g. 0xFEDC4000 for I2CC on this machine).
// NOTE: assumes identity-mapped / physical==virtual addressing, matching
// the rest of this kernel's driver style (ahci.c, nvme.c, etc.)
void i2c_dw_init(i2c_dw_t* bus, uint32_t mmio_base);

// Blocking write of `len` bytes to 7-bit slave address `addr7`.
// Returns 0 on success, -1 on timeout or NACK (tx abort).
int i2c_dw_write(i2c_dw_t* bus, uint8_t addr7, const uint8_t* data, int len);

// Blocking read of `len` bytes from 7-bit slave address `addr7`.
// Returns 0 on success, -1 on timeout or NACK.
int i2c_dw_read(i2c_dw_t* bus, uint8_t addr7, uint8_t* data, int len);

// Write `wlen` bytes (no stop), repeated-start, then read `rlen` bytes.
// This is the standard "write register/command, then read the reply"
// pattern the Elan protocol (and HID-over-I2C in general) uses.
int i2c_dw_write_read(i2c_dw_t* bus, uint8_t addr7,
                       const uint8_t* wbuf, int wlen,
                       uint8_t* rbuf, int rlen);
