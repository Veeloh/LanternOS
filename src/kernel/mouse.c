#include "mouse.h"
#include "pci.h"
#include "idt.h"
#include "vga.h"

// --- ACPI Table Definitions (Directly matches your acpi.c structures) ---
typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
} __attribute__((packed)) mouse_acpi_rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) mouse_acpi_sdt_t;

typedef struct {
    mouse_acpi_sdt_t header;
    uint32_t         other_sdt[];
} __attribute__((packed)) mouse_acpi_rsdt_t;

typedef struct {
    mouse_acpi_sdt_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
} __attribute__((packed)) mouse_acpi_fadt_t;

// --- Legacy Port I/O Registers ---
#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

// --- Synaptics / DesignWare I2C MMIO Register Maps ---
#define DW_IC_CON            0x00  // Control Register
#define DW_IC_TAR            0x04  // Target Address Register
#define DW_IC_DATA_CMD     0x10  // Data Buffer and Command
#define DW_IC_STATUS       0x70  // Controller Status Register

#define IC_STATUS_TFNF     (1u << 1) // Transmit FIFO Not Full
#define IC_STATUS_RFNE     (1u << 3) // Receive FIFO Not Empty
#define DW_IC_DATA_CMD_READ   (1u << 8) // Command bit to trigger a read cycle

// --- Driver Core States ---
static int mouse_x = 0;
static int mouse_y = 0;
static int screen_w = 320;
static int screen_h = 200;

static int left_btn = 0;
static int right_btn = 0;
static int middle_btn = 0;

static uint8_t packet[3]; // FIXED: Added missing array size
static int packet_index = 0;

// Trackpad Hardware Properties
static int is_modern_trackpad = 0;
static uint32_t i2c_mmio_base = 0;      
static uint16_t trackpad_slave_addr = 0x2C; 

// --- Core Helper Subroutines ---
static uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void ps2_wait_write() {
    int timeout = 100000;
    while (timeout-- && (inb(PS2_STATUS) & 0x02));
}

static void ps2_wait_read() {
    int timeout = 100000;
    while (timeout-- && !(inb(PS2_STATUS) & 0x01));
}

// --- MMIO I2C Low-Level Hardware Drivers ---
static inline uint32_t mmio_read(uint32_t reg) {
    return *(volatile uint32_t*)(i2c_mmio_base + reg);
}

static inline void mmio_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(i2c_mmio_base + reg) = val;
}

void i2c_write_reg16(uint16_t slave_addr, uint16_t reg, uint16_t value) {
    if (!i2c_mmio_base) return;
    
    mmio_write(DW_IC_TAR, slave_addr);

    while (!(mmio_read(DW_IC_STATUS) & IC_STATUS_TFNF));
    mmio_write(DW_IC_DATA_CMD, (reg >> 8) & 0xFF);         
    
    while (!(mmio_read(DW_IC_STATUS) & IC_STATUS_TFNF));
    mmio_write(DW_IC_DATA_CMD, reg & 0xFF);                
    
    while (!(mmio_read(DW_IC_STATUS) & IC_STATUS_TFNF));
    mmio_write(DW_IC_DATA_CMD, value & 0xFF);              
}

void i2c_read_bytes(uint16_t slave_addr, uint16_t reg, uint8_t* buffer, uint32_t length) {
    if (!i2c_mmio_base) return;

    mmio_write(DW_IC_TAR, slave_addr);

    while (!(mmio_read(DW_IC_STATUS) & IC_STATUS_TFNF));
    mmio_write(DW_IC_DATA_CMD, (reg >> 8) & 0xFF);
    while (!(mmio_read(DW_IC_STATUS) & IC_STATUS_TFNF));
    mmio_write(DW_IC_DATA_CMD, reg & 0xFF);

    for (uint32_t i = 0; i < length; i++) {
        while (!(mmio_read(DW_IC_STATUS) & IC_STATUS_TFNF));
        mmio_write(DW_IC_DATA_CMD, DW_IC_DATA_CMD_READ); // FIXED: Macro name corrected

        while (!(mmio_read(DW_IC_STATUS) & IC_STATUS_RFNE)); 
        buffer[i] = mmio_read(DW_IC_DATA_CMD) & 0xFF; // FIXED: Should read DATA_CMD register for data
    }
}

// --- Modern Trackpad Absolute Interrupt Handler ---
void trackpad_gpio_interrupt_handler() {
    uint8_t input_report[32] = {0}; // FIXED: Added explicit size allocation
    i2c_read_bytes(trackpad_slave_addr, 0x0000, input_report, sizeof(input_report));

    uint16_t packet_len = input_report[0] | (input_report[1] << 8);
    if (packet_len == 0 || packet_len > sizeof(input_report)) return;

    uint8_t status_byte = input_report[2];
    left_btn = status_byte & 0x01; 

    uint32_t raw_x = input_report[4] | (input_report[5] << 8);
    uint32_t raw_y = input_report[6] | (input_report[7] << 8);

    if (raw_x > 0 && raw_y > 0) {
        mouse_x = (int)((raw_x * screen_w) / 4096);
        mouse_y = (int)((raw_y * screen_h) / 4096);

        if (mouse_x >= screen_w) mouse_x = screen_w - 1;
        if (mouse_y >= screen_h) mouse_y = screen_h - 1;
    }
}

// --- Legacy PS/2 Mouse Driver ISR ---
void mouse_handler() {
    uint8_t status = inb(PS2_STATUS);
    if (!(status & 0x01) || !(status & 0x20)) {
        outb(0xA0, 0x20); outb(0x20, 0x20);
        return;
    }

    uint8_t data = inb(PS2_DATA);
    if (packet_index == 0 && !(data & 0x08)) {
        outb(0xA0, 0x20); outb(0x20, 0x20);
        return;
    }

    packet[packet_index++] = data;

    if (packet_index == 3) {
        packet_index = 0;
        left_btn   = packet[0] & 0x01;
        right_btn  = packet[0] & 0x02;
        middle_btn = packet[0] & 0x04;

        if (!(packet[0] & 0xC0)) {
            int dx = packet[1];
            int dy = packet[2];

            if (packet[0] & 0x10) dx -= 256;
            if (packet[0] & 0x20) dy -= 256;

            mouse_x += dx;
            mouse_y -= dy;

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= screen_w) mouse_x = screen_w - 1;
            if (mouse_y >= screen_h) mouse_y = screen_h - 1;
        }
    }
    outb(0xA0, 0x20); outb(0x20, 0x20);
}

// --- ACPI Scanner Implementation ---
static int check_acpi_trackpad() {
    uint16_t ebda_seg = *(volatile uint16_t*)0x40E;
    uint32_t ebda_addr = (uint32_t)ebda_seg << 4;
    uint32_t ranges[2] = { ebda_addr, 0xE0000 };
    uint32_t ends[2]   = { ebda_addr + 1024, 0x100000 };

    mouse_acpi_rsdp_t* rsdp = 0;
    for (int r = 0; r < 2; r++) {
        if (ranges[r] == 0) continue;
        for (uint32_t addr = ranges[r]; addr < ends[r]; addr += 16) {
            uint8_t* sig = (uint8_t*)addr;
            if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' && sig[3] == ' ' && 
                sig[4] == ' ' && sig[5] == 'P' && sig[6] == 'T' && sig[7] == 'R') {
                rsdp = (mouse_acpi_rsdp_t*)addr;
                break;
            }
        }
        if (rsdp) break;
    }
    if (!rsdp) return 0;

    mouse_acpi_rsdt_t* rsdt = (mouse_acpi_rsdt_t*)rsdp->rsdt_address;
    mouse_acpi_fadt_t* fadt = 0;
    int count = (rsdt->header.length - sizeof(mouse_acpi_sdt_t)) / 4;
    for (int i = 0; i < count; i++) {
        mouse_acpi_sdt_t* hdr = (mouse_acpi_sdt_t*)rsdt->other_sdt[i];
        if (hdr->signature[0] == 'F' && hdr->signature[1] == 'A' && 
            hdr->signature[2] == 'C' && hdr->signature[3] == 'P') {
            fadt = (mouse_acpi_fadt_t*)hdr;
            break;
        }
    }
    if (!fadt) return 0;

    mouse_acpi_sdt_t* dsdt = (mouse_acpi_sdt_t*)fadt->dsdt;
    uint8_t* p = (uint8_t*)dsdt + sizeof(mouse_acpi_sdt_t);
    uint8_t* end = (uint8_t*)dsdt + dsdt->length - 8;

    for (; p < end; p++) {
        if (p[0] == 'P' && p[1] == 'N' && p[2] == 'P' && p[3] == '0' && 
            p[4] == 'C' && p[5] == '5' && p[6] == '0') return 1;
        if (p[0] == 'S' && p[1] == 'Y' && p[2] == 'N' && p[3] == 'A') return 1;
    }
    return 0;
}

extern void mouse_isr();

void mouse_init() {
    screen_w = (int)vga_get_fb_width();
    screen_h = (int)vga_get_fb_height();
    if (screen_w <= 0) screen_w = 320;
    if (screen_h <= 0) screen_h = 200;
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;

    if (check_acpi_trackpad()) {
        pci_device_t pci_list[32]; // FIXED: Added array size allocation
        int dev_count = pci_scan(pci_list, 32);
        
        pci_device_t* i2c_dev = pci_find(pci_list, dev_count, 0x11, 0x00); 
        if (!i2c_dev) {
            i2c_dev = pci_find(pci_list, dev_count, 0x0C, 0x80);        
        }

        if (i2c_dev) {
            i2c_mmio_base = i2c_dev->bar[0] & 0xFFFFFFF0; // FIXED: Read bar[0] index explicitly
            vga_print("\nmouse: linked to I2C host controller");

            i2c_write_reg16(trackpad_slave_addr, 0x0024, 0x0000); 
            i2c_write_reg16(trackpad_slave_addr, 0x0026, 0x0001); 

            is_modern_trackpad = 1;
            return; 
        }
    }

    // --- LEGACY DESKTOP FALLBACK ---
    while (inb(PS2_STATUS) & 0x01) inb(PS2_DATA);

    ps2_wait_write();
    outb(PS2_CMD, 0xA8);

    ps2_wait_write();
    outb(PS2_CMD, 0x20);
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA);

    config |= 0x02;
    config &= ~0x20;

    ps2_wait_write();
    outb(PS2_CMD, 0x60);
    ps2_wait_write();
    outb(PS2_DATA, config);

    ps2_wait_write(); outb(PS2_CMD, 0xD4);
    ps2_wait_write(); outb(PS2_DATA, 0xF6);
    ps2_wait_read(); inb(PS2_DATA);

    ps2_wait_write(); outb(PS2_CMD, 0xD4);
    ps2_wait_write(); outb(PS2_DATA, 0xF4);
    ps2_wait_read(); inb(PS2_DATA);

    idt_set_handler(44, (uint32_t)mouse_isr);

    uint8_t master_mask = inb(0x21);
    master_mask &= ~0x04; 
    outb(0x21, master_mask);

    uint8_t slave_mask = inb(0xA1);
    slave_mask &= ~0x10; 
    outb(0xA1, slave_mask);
}

int mouse_get_x() { return mouse_x; }
int mouse_get_y() { return mouse_y; }
int mouse_left_pressed()   { return left_btn != 0; }
int mouse_right_pressed()  { return right_btn != 0; }
int mouse_middle_pressed() { return middle_btn != 0; }
