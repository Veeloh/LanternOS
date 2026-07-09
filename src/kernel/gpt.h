#pragma once
#include <stdint.h>

// Looks for a GPT partition table (LBA1, per spec - LBA0 is just the
// protective MBR) and searches its partition entries for one whose
// type GUID is "EFI System Partition". If found, *out_lba is set to
// that partition's starting LBA and 1 is returned. If there's no GPT
// header or no ESP entry, returns 0 and leaves *out_lba untouched -
// callers should fall back to treating the disk as an unpartitioned
// raw FAT32 volume (e.g. an SD card written directly with no MBR/GPT).
int gpt_find_esp(uint32_t* out_lba);
