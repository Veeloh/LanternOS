#include "fat32.h"
#include "vga.h"
#include "disk.h"
#include "gpt.h"

#define SECTOR_SIZE 512

// Absolute number of cluster-chain hops we'll ever follow. Real FAT32
// filesystems are nowhere close to this - it exists purely so a
// corrupt/misdetected FAT (e.g. wrong partition offset) can't spin the
// kernel forever instead of just failing.
#define MAX_CLUSTER_CHAIN 200000
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

// The directory `ls`/`cat`/`cd` operate relative to. Starts at the root
// and gets moved by fat32_change_dir(). cwd_path is the human-readable
// form of the same thing, kept in sync purely for the prompt/pwd - it
// has no bearing on how paths are actually resolved on disk.
static uint32_t current_dir_cluster;
static char cwd_path[128] = "/";

#define FAT_ATTR_DIRECTORY 0x10

// LBA of the start of the FAT32 volume itself (0 for an unpartitioned
// raw FAT32 disk, or the ESP's starting LBA on a GPT-partitioned one).
// Every read in this file goes through fat_read_sector() below so this
// offset only has to be applied in one place.
static uint32_t partition_base = 0;

static void fat_read_sector(uint32_t lba, uint8_t* buf) {
	disk_read_sector(partition_base + lba, buf);
}

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

    fat_read_sector(fat_sector, buf);
    return *(uint32_t*)(buf + offset) & 0x0FFFFFFF;
}

void fat32_init() {
    uint8_t buf[SECTOR_SIZE];

    // Don't assume the FAT32 volume starts at LBA0 - that's only true
    // for a raw/unpartitioned disk (e.g. an SD card written directly).
    // A GPT-partitioned disk (e.g. most eMMC installs) has LBA0/1 taken
    // up by the protective MBR and GPT header, with the actual FAT32
    // filesystem living inside the EFI System Partition further in.
    uint32_t esp_lba;
    if (gpt_find_esp(&esp_lba)) {
        vga_print("\nfat32: using GPT EFI System Partition as FAT32 volume");
        partition_base = esp_lba;
    } else {
        vga_print("\nfat32: no GPT/ESP found, assuming raw FAT32 at LBA0");
        partition_base = 0;
    }

    fat_read_sector(0, buf);

    // Sanity-check we actually landed on a boot sector before trusting
    // its fields - a wrong partition_base would otherwise turn into
    // silently-garbage fat_start/data_start/root_cluster values, which
    // is exactly what caused the ls hang.
    if (buf[510] != 0x55 || buf[511] != 0xAA) {
        vga_print("\nfat32: WARNING - boot sector signature (0x55AA) missing, filesystem may be misdetected");
    }

    // copy BPB
    for (int i = 0; i < sizeof(fat32_bpb_t); i++)
        ((uint8_t*)&bpb)[i] = buf[i];

    fat_start  = bpb.reserved_sectors;
    data_start = fat_start + (bpb.fat_count * bpb.sectors_per_fat_32);
    root_cluster = bpb.root_cluster;
    current_dir_cluster = root_cluster;
}

void fat32_list_dir() {
    uint8_t buf[SECTOR_SIZE];
    uint32_t cluster = current_dir_cluster;
    uint32_t hops = 0;

    while (cluster < 0x0FFFFFF8) {
        if (++hops > MAX_CLUSTER_CHAIN) {
            vga_print("\nfat32: cluster chain too long, aborting (corrupt FAT or wrong partition offset?)");
            return;
        }
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            fat_read_sector(lba + s, buf);
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
    uint32_t cluster = current_dir_cluster;
    uint32_t hops = 0;

    while (cluster < 0x0FFFFFF8) {
        if (++hops > MAX_CLUSTER_CHAIN) {
            vga_print("\nfat32: cluster chain too long, aborting (corrupt FAT or wrong partition offset?)");
            return -1;
        }
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            fat_read_sector(lba + s, buf);
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
                    uint32_t file_hops = 0;
                    while (fc < 0x0FFFFFF8 && bytes_read < max_size) {
                        if (++file_hops > MAX_CLUSTER_CHAIN) {
                            vga_print("\nfat32: file cluster chain too long, aborting");
                            return (int)bytes_read;
                        }
                        uint32_t flba = cluster_to_lba(fc);
                        for (uint32_t fs = 0; fs < bpb.sectors_per_cluster && bytes_read < max_size; fs++) {
                            fat_read_sector(flba + fs, buf);
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

// Converts a typed name like "foo" or "foo.txt" into the raw 11-byte
// space-padded 8.3 form FAT32 stores on disk. Same convention as the
// inline matching fat32_read_file() does above.
static void build_fat_name(const char* name, char* fname) {
    for (int i = 0; i < 11; i++) fname[i] = ' ';
    int ni = 0, fi = 0;
    while (name[ni] && name[ni] != '.' && fi < 8) fname[fi++] = name[ni++];
    if (name[ni] == '.') {
        ni++;
        fi = 8;
        while (name[ni] && fi < 11) fname[fi++] = name[ni++];
    }
}

// Scans dir_cluster for an entry matching the raw 8.3 fname. Returns 1
// and fills out_cluster/out_attr on a hit, 0 if nothing matched.
static int find_dir_entry(uint32_t dir_cluster, const char* fname, uint32_t* out_cluster, uint8_t* out_attr) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t cluster = dir_cluster;
    uint32_t hops = 0;

    while (cluster < 0x0FFFFFF8) {
        if (++hops > MAX_CLUSTER_CHAIN) return 0;
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            fat_read_sector(lba + s, buf);
            fat32_entry_t* entry = (fat32_entry_t*)buf;
            for (int i = 0; i < SECTOR_SIZE / sizeof(fat32_entry_t); i++) {
                if (entry[i].name[0] == 0) return 0;
                if (entry[i].name[0] == 0xE5) continue;
                if (entry[i].attributes & 0x0F) continue; // skip LFN

                int match = 1;
                for (int j = 0; j < 11; j++)
                    if (entry[i].name[j] != fname[j]) { match = 0; break; }

                if (match) {
                    *out_cluster = ((uint32_t)entry[i].cluster_high << 16) | entry[i].cluster_low;
                    *out_attr = entry[i].attributes;
                    return 1;
                }
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return 0;
}

// Moves current_dir_cluster to `name`, which must be a single path
// component ("foo", "..", "." or "/") - it does not parse "foo/bar" in
// one call. Returns 0 on success, -1 if the name doesn't exist in the
// current directory, -2 if it exists but isn't a directory.
int fat32_change_dir(const char* name) {
    if (!name || name[0] == 0) return -1;

    // "cd /" or "cd \" - straight back to root regardless of depth
    if ((name[0] == '/' || name[0] == '\\') && name[1] == 0) {
        current_dir_cluster = root_cluster;
        cwd_path[0] = '/';
        cwd_path[1] = 0;
        return 0;
    }

    if (name[0] == '.' && name[1] == 0) return 0; // "." - no-op

    int is_parent = (name[0] == '.' && name[1] == '.' && name[2] == 0);

    char fname[11];
    build_fat_name(is_parent ? ".." : name, fname);

    uint32_t target_cluster;
    uint8_t attr;
    if (!find_dir_entry(current_dir_cluster, fname, &target_cluster, &attr))
        return -1; // not found

    if (!(attr & FAT_ATTR_DIRECTORY))
        return -2; // exists, but it's a file

    // FAT32 stores a directory's own "." and ".." entries with cluster 0
    // as a sentinel for "the root directory" rather than writing
    // root_cluster's real value, so remap it here.
    if (target_cluster == 0) target_cluster = root_cluster;

    current_dir_cluster = target_cluster;

    // keep the human-readable path in sync for the prompt/pwd
    int len = 0;
    while (cwd_path[len]) len++;

    if (is_parent) {
        while (len > 1 && cwd_path[len - 1] != '/') len--;
        if (len > 1) len--; // drop the trailing slash too, unless at root
        cwd_path[len] = 0;
        if (cwd_path[0] == 0) { cwd_path[0] = '/'; cwd_path[1] = 0; }
    } else {
        if (len > 1) cwd_path[len++] = '/';
        int ni = 0;
        while (name[ni] && len < (int)sizeof(cwd_path) - 1) cwd_path[len++] = name[ni++];
        cwd_path[len] = 0;
    }

    return 0;
}

const char* fat32_get_cwd() {
    return cwd_path;
}
