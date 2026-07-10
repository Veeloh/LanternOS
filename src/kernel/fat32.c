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

static void fat_write_sector(uint32_t lba, uint8_t* buf) {
	disk_write_sector(partition_base + lba, buf);
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

// Writes a single FAT entry, to every FAT copy on disk (bpb.fat_count is
// normally 2 - mismatched copies are exactly how "chkdsk fixed errors"
// horror stories start, so keep them in lockstep). The top 4 bits of
// each 32-bit entry are reserved by the spec and must be preserved.
static void fat_set_cluster(uint32_t cluster, uint32_t value) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_in_fat = fat_offset / SECTOR_SIZE;
    uint32_t offset = fat_offset % SECTOR_SIZE;

    for (uint32_t copy = 0; copy < bpb.fat_count; copy++) {
        uint32_t fat_sector = fat_start + copy * bpb.sectors_per_fat_32 + sector_in_fat;
        fat_read_sector(fat_sector, buf);
        uint32_t* entry = (uint32_t*)(buf + offset);
        *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);
        fat_write_sector(fat_sector, buf);
    }
}

// Linear scan of the FAT for the first entry reading as 0 (free).
// Fine for a hobby OS; a real one would cache a "search hint" (FSInfo's
// next-free field) instead of re-scanning from cluster 2 every time.
static uint32_t find_free_cluster(void) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t total_clusters = (bpb.total_sectors_32 - data_start) / bpb.sectors_per_cluster;

    for (uint32_t sector = 0; sector < bpb.sectors_per_fat_32; sector++) {
        fat_read_sector(fat_start + sector, buf);
        uint32_t* entries = (uint32_t*)buf;
        for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
            uint32_t cluster = sector * (SECTOR_SIZE / 4) + i;
            if (cluster < 2) continue;             // 0 and 1 are reserved, not real clusters
            if (cluster >= total_clusters + 2) return 0; // ran off the end of the volume
            if ((entries[i] & 0x0FFFFFFF) == 0) return cluster;
        }
    }
    return 0; // disk full
}

// Claims a free cluster (marks it end-of-chain so nothing else can grab
// it) and zeroes its data on disk, so a freshly-allocated directory
// cluster doesn't start out full of garbage that looks like entries.
static uint32_t allocate_cluster(void) {
    uint32_t cluster = find_free_cluster();
    if (!cluster) return 0;

    fat_set_cluster(cluster, 0x0FFFFFFF);

    uint8_t zero[SECTOR_SIZE] = {0};
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++)
        fat_write_sector(lba + s, zero);

    return cluster;
}

// Frees every cluster in a chain starting at `cluster` (used when
// truncating a file, or deleting one). Does NOT touch whatever pointed
// to `cluster` in the first place - the caller is responsible for
// terminating/updating that pointer separately.
static void free_chain_from(uint32_t cluster) {
    uint32_t hops = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (++hops > MAX_CLUSTER_CHAIN) return;
        uint32_t next = fat_next_cluster(cluster);
        fat_set_cluster(cluster, 0);
        cluster = next;
    }
}

// Allocates a fresh cluster and links it onto the end of an existing
// chain (used both for growing a file and for growing a directory that's
// run out of entry slots). Returns 0 on disk-full, same as allocate_cluster.
static uint32_t extend_chain(uint32_t last_cluster) {
    uint32_t new_cluster = allocate_cluster();
    if (!new_cluster) return 0;
    fat_set_cluster(last_cluster, new_cluster);
    return new_cluster;
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

// Same scan as find_dir_entry, but returns *where* the entry lives (its
// sector's LBA and index within that sector) instead of decoding it, so
// a caller can read that sector, edit the entry in place, and write the
// sector straight back.
static int find_dir_entry_loc(uint32_t dir_cluster, const char* fname, uint32_t* out_lba, uint32_t* out_index) {
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
                if (entry[i].attributes & 0x0F) continue;

                int match = 1;
                for (int j = 0; j < 11; j++)
                    if (entry[i].name[j] != fname[j]) { match = 0; break; }

                if (match) {
                    *out_lba = lba + s;
                    *out_index = i;
                    return 1;
                }
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return 0;
}

// Finds a directory-entry slot free for the taking - either an unused
// tail slot (name[0] == 0) or a deleted one (name[0] == 0xE5). If the
// directory's whole cluster chain is packed full, extends it with one
// more (already-zeroed) cluster and hands back the first slot in that.
static int find_free_dir_slot(uint32_t dir_cluster, uint32_t* out_lba, uint32_t* out_index) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t cluster = dir_cluster;
    uint32_t hops = 0;
    uint32_t last_cluster = dir_cluster;

    while (cluster < 0x0FFFFFF8) {
        if (++hops > MAX_CLUSTER_CHAIN) return 0;
        last_cluster = cluster;
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            fat_read_sector(lba + s, buf);
            fat32_entry_t* entry = (fat32_entry_t*)buf;
            for (int i = 0; i < SECTOR_SIZE / sizeof(fat32_entry_t); i++) {
                if (entry[i].name[0] == 0 || entry[i].name[0] == 0xE5) {
                    *out_lba = lba + s;
                    *out_index = i;
                    return 1;
                }
            }
        }
        uint32_t next = fat_next_cluster(cluster);
        if (next >= 0x0FFFFFF8) {
            uint32_t new_cluster = extend_chain(last_cluster);
            if (!new_cluster) return 0; // disk full
            *out_lba = cluster_to_lba(new_cluster);
            *out_index = 0;
            return 1;
        }
        cluster = next;
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

// Writes `size` bytes of `data` to a file named `name` in the current
// directory - creating it if it doesn't exist, overwriting it in place
// (and freeing any now-unused trailing clusters) if it does.
//
// Returns bytes actually written on success, -1 if `name` exists but is
// a directory, -2 if no directory slot was available and the directory
// couldn't be extended (disk full), or a value less than `size` if the
// disk ran out of space partway through (a short write, not an error -
// check the return value against `size` if that distinction matters).
int fat32_write_file(const char* name, const uint8_t* data, uint32_t size) {
    char fname[11];
    build_fat_name(name, fname);

    uint32_t entry_lba, entry_index;
    uint8_t entry_buf[SECTOR_SIZE];

    int existed = find_dir_entry_loc(current_dir_cluster, fname, &entry_lba, &entry_index);

    if (existed) {
        fat_read_sector(entry_lba, entry_buf);
        fat32_entry_t* e = (fat32_entry_t*)entry_buf;
        if (e[entry_index].attributes & FAT_ATTR_DIRECTORY) return -1;
    } else {
        if (!find_free_dir_slot(current_dir_cluster, &entry_lba, &entry_index)) return -2;

        fat_read_sector(entry_lba, entry_buf);
        fat32_entry_t* e = (fat32_entry_t*)entry_buf;
        for (int i = 0; i < 11; i++) e[entry_index].name[i] = fname[i];
        e[entry_index].attributes    = 0x20; // ARCHIVE
        e[entry_index].reserved      = 0;
        e[entry_index].created_tenths = 0;
        e[entry_index].created_time  = 0;
        e[entry_index].created_date  = 0;
        e[entry_index].accessed_date = 0;
        e[entry_index].modified_time = 0;
        e[entry_index].modified_date = 0;
        e[entry_index].cluster_high  = 0;
        e[entry_index].cluster_low   = 0;
        e[entry_index].size          = 0;
        fat_write_sector(entry_lba, entry_buf);
    }

    fat_read_sector(entry_lba, entry_buf);
    fat32_entry_t* e = (fat32_entry_t*)entry_buf;
    uint32_t first_cluster = ((uint32_t)e[entry_index].cluster_high << 16) | e[entry_index].cluster_low;

    uint32_t bytes_per_cluster = bpb.sectors_per_cluster * SECTOR_SIZE;
    uint32_t clusters_needed = (size == 0) ? 0 : (size + bytes_per_cluster - 1) / bytes_per_cluster;

    uint32_t cluster = first_cluster;
    uint32_t prev_cluster = 0;
    uint32_t written = 0;
    uint32_t clusters_used = 0;

    if (clusters_needed == 0) {
        if (first_cluster >= 2) free_chain_from(first_cluster);
        first_cluster = 0;
    } else {
        while (clusters_used < clusters_needed) {
            if (cluster < 2 || cluster >= 0x0FFFFFF8) {
                uint32_t new_cluster = allocate_cluster();
                if (!new_cluster) break; // disk full mid-write - short write
                if (prev_cluster) fat_set_cluster(prev_cluster, new_cluster);
                else first_cluster = new_cluster;
                cluster = new_cluster;
            }

            uint8_t buf[SECTOR_SIZE];
            uint32_t lba = cluster_to_lba(cluster);
            for (uint32_t s = 0; s < bpb.sectors_per_cluster && written < size; s++) {
                uint32_t to_copy = SECTOR_SIZE;
                if (written + to_copy > size) to_copy = size - written;
                for (uint32_t b = 0; b < to_copy; b++) buf[b] = data[written + b];
                for (uint32_t b = to_copy; b < SECTOR_SIZE; b++) buf[b] = 0; // pad final partial sector
                fat_write_sector(lba + s, buf);
                written += to_copy;
            }

            prev_cluster = cluster;
            clusters_used++;
            cluster = fat_next_cluster(cluster);
        }

        // if the file used to be longer, whatever's left of its old chain
        // past the point we actually needed gets freed
        if (prev_cluster) fat_set_cluster(prev_cluster, 0x0FFFFFFF);
        if (cluster >= 2 && cluster < 0x0FFFFFF8) free_chain_from(cluster);
    }

    fat_read_sector(entry_lba, entry_buf);
    e = (fat32_entry_t*)entry_buf;
    e[entry_index].cluster_high = (first_cluster >> 16) & 0xFFFF;
    e[entry_index].cluster_low  = first_cluster & 0xFFFF;
    e[entry_index].size = written;
    fat_write_sector(entry_lba, entry_buf);

    return (int)written;
}

// True if dir_cluster's chain contains nothing but "." and ".." (and/or
// already-deleted slots). Used by fat32_rmdir to refuse to remove a
// directory that still has something in it.
static int dir_is_empty(uint32_t dir_cluster) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t cluster = dir_cluster;
    uint32_t hops = 0;

    while (cluster < 0x0FFFFFF8) {
        // if we can't finish verifying it's empty, don't delete it
        if (++hops > MAX_CLUSTER_CHAIN) return 0;
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            fat_read_sector(lba + s, buf);
            fat32_entry_t* entry = (fat32_entry_t*)buf;
            for (int i = 0; i < SECTOR_SIZE / sizeof(fat32_entry_t); i++) {
                if (entry[i].name[0] == 0) return 1; // end of entries - nothing real found
                if (entry[i].name[0] == 0xE5) continue; // deleted slot
                if (entry[i].attributes & 0x0F) continue; // LFN

                int is_dot    = (entry[i].name[0] == '.' && entry[i].name[1] == ' ');
                int is_dotdot = (entry[i].name[0] == '.' && entry[i].name[1] == '.' && entry[i].name[2] == ' ');
                if (is_dot || is_dotdot) continue;

                return 0; // found a real entry - not empty
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return 1;
}

// Creates a subdirectory of the current directory: allocates it a
// cluster, seeds that cluster with "." and ".." entries, and adds a
// directory-type entry for it in the parent.
//
// Returns 0 on success, -1 if that name already exists, -2 if the disk
// or the parent directory is full.
int fat32_mkdir(const char* name) {
    char fname[11];
    build_fat_name(name, fname);

    uint32_t existing_cluster;
    uint8_t existing_attr;
    if (find_dir_entry(current_dir_cluster, fname, &existing_cluster, &existing_attr))
        return -1; // name taken (by a file or a directory, doesn't matter)

    uint32_t new_cluster = allocate_cluster();
    if (!new_cluster) return -2;

    // seed the new directory's own cluster with "." and ".."
    uint8_t buf[SECTOR_SIZE];
    fat_read_sector(cluster_to_lba(new_cluster), buf);
    fat32_entry_t* entries = (fat32_entry_t*)buf;

    for (int i = 0; i < 11; i++) entries[0].name[i] = ' ';
    entries[0].name[0] = '.';
    entries[0].attributes = FAT_ATTR_DIRECTORY;
    entries[0].cluster_high = (new_cluster >> 16) & 0xFFFF;
    entries[0].cluster_low  = new_cluster & 0xFFFF;
    entries[0].size = 0;

    for (int i = 0; i < 11; i++) entries[1].name[i] = ' ';
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attributes = FAT_ATTR_DIRECTORY;
    // FAT32 convention: ".." pointing at the root is stored as cluster 0,
    // not root_cluster's real value (see fat32_change_dir's remap).
    uint32_t parent_field = (current_dir_cluster == root_cluster) ? 0 : current_dir_cluster;
    entries[1].cluster_high = (parent_field >> 16) & 0xFFFF;
    entries[1].cluster_low  = parent_field & 0xFFFF;
    entries[1].size = 0;

    fat_write_sector(cluster_to_lba(new_cluster), buf);

    // add the directory's own entry in the parent
    uint32_t entry_lba, entry_index;
    if (!find_free_dir_slot(current_dir_cluster, &entry_lba, &entry_index)) {
        free_chain_from(new_cluster); // don't leak the cluster we just claimed
        return -2;
    }

    uint8_t entry_buf[SECTOR_SIZE];
    fat_read_sector(entry_lba, entry_buf);
    fat32_entry_t* e = (fat32_entry_t*)entry_buf;
    for (int i = 0; i < 11; i++) e[entry_index].name[i] = fname[i];
    e[entry_index].attributes    = FAT_ATTR_DIRECTORY;
    e[entry_index].reserved      = 0;
    e[entry_index].created_tenths = 0;
    e[entry_index].created_time  = 0;
    e[entry_index].created_date  = 0;
    e[entry_index].accessed_date = 0;
    e[entry_index].modified_time = 0;
    e[entry_index].modified_date = 0;
    e[entry_index].cluster_high  = (new_cluster >> 16) & 0xFFFF;
    e[entry_index].cluster_low   = new_cluster & 0xFFFF;
    e[entry_index].size          = 0;
    fat_write_sector(entry_lba, entry_buf);

    return 0;
}

// Removes an empty subdirectory of the current directory.
//
// Returns 0 on success, -1 if the name doesn't exist, -2 if it exists
// but isn't a directory (use fat32_remove_file for that), -3 if it's
// not empty, -4 if it's the directory you're currently sitting in.
int fat32_rmdir(const char* name) {
    char fname[11];
    build_fat_name(name, fname);

    uint32_t entry_lba, entry_index;
    if (!find_dir_entry_loc(current_dir_cluster, fname, &entry_lba, &entry_index))
        return -1;

    uint8_t entry_buf[SECTOR_SIZE];
    fat_read_sector(entry_lba, entry_buf);
    fat32_entry_t* e = (fat32_entry_t*)entry_buf;

    if (!(e[entry_index].attributes & FAT_ATTR_DIRECTORY)) return -2;

    uint32_t target_cluster = ((uint32_t)e[entry_index].cluster_high << 16) | e[entry_index].cluster_low;
    if (target_cluster == 0) target_cluster = root_cluster;

    if (target_cluster == current_dir_cluster) return -4;
    if (!dir_is_empty(target_cluster)) return -3;

    free_chain_from(target_cluster);

    // re-read - free_chain_from only touched the FAT, not this sector
    fat_read_sector(entry_lba, entry_buf);
    e = (fat32_entry_t*)entry_buf;
    e[entry_index].name[0] = 0xE5; // mark deleted
    fat_write_sector(entry_lba, entry_buf);

    return 0;
}

// Deletes a file (not a directory - use fat32_rmdir for those) in the
// current directory. Returns 0 on success, -1 if not found, -2 if the
// name refers to a directory.
int fat32_remove_file(const char* name) {
    char fname[11];
    build_fat_name(name, fname);

    uint32_t entry_lba, entry_index;
    if (!find_dir_entry_loc(current_dir_cluster, fname, &entry_lba, &entry_index))
        return -1;

    uint8_t entry_buf[SECTOR_SIZE];
    fat_read_sector(entry_lba, entry_buf);
    fat32_entry_t* e = (fat32_entry_t*)entry_buf;

    if (e[entry_index].attributes & FAT_ATTR_DIRECTORY) return -2;

    uint32_t cluster = ((uint32_t)e[entry_index].cluster_high << 16) | e[entry_index].cluster_low;
    if (cluster >= 2) free_chain_from(cluster);

    fat_read_sector(entry_lba, entry_buf);
    e = (fat32_entry_t*)entry_buf;
    e[entry_index].name[0] = 0xE5;
    fat_write_sector(entry_lba, entry_buf);

    return 0;
}
