#include "fat32.h"
#include "vga.h"
#include "disk.h"

#define SECTOR_SIZE 512
#define ATA_DATA    0x1F0
#define ATA_SECTOR  0x1F3
#define ATA_LCYL    0x1F4
#define ATA_HCYL    0x1F5
#define ATA_HEAD    0x1F6
#define ATA_CMD     0x1F7
#define ATA_STATUS  0x1F7

static fat32_bpb_t bpb;
static uint32_t fat_start;
static uint32_t data_start;
static uint32_t root_cluster;

static void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void inw_buffer(uint16_t port, uint16_t* buf, uint32_t count) {
    __asm__ volatile ("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}

static void ata_read_sector(uint32_t lba, uint8_t* buf) {
    // wait for drive ready
    while (inb(ATA_STATUS) & 0x80);

    outb(ATA_HEAD,   0xE0 | ((lba >> 24) & 0x0F) | 0x10); // drive 1
    outb(0x1F2, 1);
    outb(ATA_SECTOR, lba & 0xFF);
    outb(ATA_LCYL,   (lba >> 8) & 0xFF);
    outb(ATA_HCYL,   (lba >> 16) & 0xFF);
    outb(ATA_CMD,    0x20); // read command

    while (!(inb(ATA_STATUS) & 0x08)); // wait for DRQ

    inw_buffer(ATA_DATA, (uint16_t*)buf, SECTOR_SIZE / 2);
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint32_t fat_next_cluster(uint32_t cluster) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start + (fat_offset / SECTOR_SIZE);
    uint32_t offset = fat_offset % SECTOR_SIZE;

    disk_read_sector(fat_sector, buf);
    return *(uint32_t*)(buf + offset) & 0x0FFFFFFF;
}

void fat32_init() {
    uint8_t buf[SECTOR_SIZE];
    disk_read_sector(0, buf);

    // copy BPB
    for (int i = 0; i < sizeof(fat32_bpb_t); i++)
        ((uint8_t*)&bpb)[i] = buf[i];

    fat_start  = bpb.reserved_sectors;
    data_start = fat_start + (bpb.fat_count * bpb.sectors_per_fat_32);
    root_cluster = bpb.root_cluster;
}

void fat32_list_dir() {
    uint8_t buf[SECTOR_SIZE];
    uint32_t cluster = root_cluster;

    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            disk_read_sector(lba + s, buf);
            fat32_entry_t* entry = (fat32_entry_t*)buf;
            for (int i = 0; i < SECTOR_SIZE / sizeof(fat32_entry_t); i++) {
                if (entry[i].name[0] == 0) return;
                if (entry[i].name[0] == 0xE5) continue;
                if (entry[i].attributes & 0x0F) continue; // skip LFN
                vga_putchar('\n');
                for (int j = 0; j < 8; j++)
                    if (entry[i].name[j] != ' ')
                        vga_putchar(entry[i].name[j]);
                if (entry[i].name[8] != ' ') {
                    vga_putchar('.');
                    for (int j = 8; j < 11; j++)
                        if (entry[i].name[j] != ' ')
                            vga_putchar(entry[i].name[j]);
                }
            }
        }
        cluster = fat_next_cluster(cluster);
    }
}

int fat32_read_file(const char* name, uint8_t* buffer, uint32_t max_size) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t cluster = root_cluster;

    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            disk_read_sector(lba + s, buf);
            fat32_entry_t* entry = (fat32_entry_t*)buf;
            for (int i = 0; i < SECTOR_SIZE / sizeof(fat32_entry_t); i++) {
                if (entry[i].name[0] == 0) return -1;
                if (entry[i].name[0] == 0xE5) continue;
                if (entry[i].attributes & 0x0F) continue;

                // match name
                char fname[12] = "           ";
                int ni = 0, fi = 0;
                while (name[ni] && name[ni] != '.') fname[fi++] = name[ni++];
                if (name[ni] == '.') { fi = 8; ni++; while (name[ni]) fname[fi++] = name[ni++]; }

                int match = 1;
                for (int j = 0; j < 11; j++)
                    if (entry[i].name[j] != fname[j]) { match = 0; break; }

                if (match) {
                    uint32_t fc = ((uint32_t)entry[i].cluster_high << 16) | entry[i].cluster_low;
                    uint32_t bytes_read = 0;
                    while (fc < 0x0FFFFFF8 && bytes_read < max_size) {
                        uint32_t flba = cluster_to_lba(fc);
                        for (uint32_t fs = 0; fs < bpb.sectors_per_cluster && bytes_read < max_size; fs++) {
                            disk_read_sector(flba + fs, buf);
                            uint32_t to_copy = SECTOR_SIZE;
                            if (bytes_read + to_copy > max_size) to_copy = max_size - bytes_read;
                            for (uint32_t b = 0; b < to_copy; b++)
                                buffer[bytes_read++] = buf[b];
                        }
                        fc = fat_next_cluster(fc);
                    }
                    return bytes_read;
                }
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return -1;
}
