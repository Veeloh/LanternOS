#pragma once
#include <stdint.h>

typedef struct {
    uint8_t  jump[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    // FAT32 extended
    uint32_t sectors_per_fat_32;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved2;
    uint8_t  signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    uint8_t  name[11];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  created_tenths;
    uint16_t created_time;
    uint16_t created_date;
    uint16_t accessed_date;
    uint16_t cluster_high;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t cluster_low;
    uint32_t size;
} __attribute__((packed)) fat32_entry_t;

// One entry from a structured (non-printing) directory listing - see
// fat32_list_dir_entries() below. name is 8.3-formatted and NUL-terminated.
typedef struct {
	char name[13];   // up to "12345678.123" + NUL
	uint32_t size;
	uint8_t is_dir;
} fat32_dirent_t;

void fat32_init();
int fat32_read_file(const char* name, uint8_t* buffer, uint32_t max_size);
void fat32_list_dir();

// Same directory walk as fat32_list_dir(), but fills `out` (up to
// max_entries) instead of vga_print-ing to the text console, so GUI code
// (the Files app) can actually use the result. Returns the number of
// entries written.
int fat32_list_dir_entries(fat32_dirent_t* out, int max_entries);
int fat32_change_dir(const char* name);
const char* fat32_get_cwd(void);
int fat32_write_file(const char* name, const uint8_t* data, uint32_t size);
int fat32_mkdir(const char* name);
int fat32_rmdir(const char* name);
int fat32_remove_file(const char* name);
