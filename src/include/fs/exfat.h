// src/include/exfat.h - exFAT filesystem support
#ifndef EXFAT_H
#define EXFAT_H

#include "../core/types.h"

// exFAT Boot Sector (Main Boot Region)
typedef struct __attribute__((packed)) {
    uint8_t  jump_boot[3];           // 0x00: Jump instruction
    uint8_t  fs_name[8];             // 0x03: "EXFAT   "
    uint8_t  must_be_zero[53];       // 0x0B: Must be zero
    uint64_t partition_offset;       // 0x40: Start sector of partition
    uint64_t volume_length;          // 0x48: Size in sectors
    uint32_t fat_offset;             // 0x50: Sectors before FAT
    uint32_t fat_length;             // 0x54: FAT size in sectors
    uint32_t cluster_heap_offset;    // 0x58: First cluster sector
    uint32_t cluster_count;          // 0x5C: Number of clusters
    uint32_t root_dir_cluster;       // 0x60: First cluster of root
    uint32_t volume_serial;          // 0x64: Volume serial number
    uint16_t fs_revision;            // 0x68: Filesystem version (0x0100)
    uint16_t volume_flags;           // 0x6A: Volume flags
    uint8_t  bytes_per_sector_shift; // 0x6C: Log2(bytes per sector)
    uint8_t  sectors_per_cluster_shift; // 0x6D: Log2(sectors per cluster)
    uint8_t  number_of_fats;         // 0x6E: Number of FATs (1 or 2)
    uint8_t  drive_select;           // 0x6F: 0x80 for hard disk
    uint8_t  percent_in_use;         // 0x70: Percentage of heap in use
    uint8_t  reserved[7];            // 0x71: Reserved
    uint8_t  boot_code[390];         // 0x78: Boot code
    uint16_t boot_signature;         // 0x1FE: 0xAA55
} exfat_boot_sector_t;

// Directory Entry Types
#define EXFAT_TYPE_EOD           0x00  // End of Directory
#define EXFAT_TYPE_ALLOCATION    0x81  // Allocation Bitmap
#define EXFAT_TYPE_UPCASE        0x82  // Upcase Table
#define EXFAT_TYPE_VOLUME_LABEL  0x83  // Volume Label
#define EXFAT_TYPE_FILE          0x85  // File Entry
#define EXFAT_TYPE_VOLUME_GUID   0xA0  // Volume GUID
#define EXFAT_TYPE_STREAM        0xC0  // Stream Extension
#define EXFAT_TYPE_FILE_NAME     0xC1  // File Name Extension

// File Attributes
#define EXFAT_ATTR_READ_ONLY     0x0001
#define EXFAT_ATTR_HIDDEN        0x0002
#define EXFAT_ATTR_SYSTEM        0x0004
#define EXFAT_ATTR_DIRECTORY     0x0010
#define EXFAT_ATTR_ARCHIVE       0x0020

// Generic Directory Entry (all are 32 bytes)
typedef struct __attribute__((packed)) {
    uint8_t entry_type;
    uint8_t data[31];
} exfat_dir_entry_t;

// File Directory Entry
typedef struct __attribute__((packed)) {
    uint8_t  entry_type;             // 0x85
    uint8_t  secondary_count;        // Number of secondary entries
    uint16_t set_checksum;           // Checksum of entry set
    uint16_t file_attributes;        // File attributes
    uint16_t reserved1;
    uint32_t create_timestamp;       // Creation time
    uint32_t modify_timestamp;       // Modification time
    uint32_t access_timestamp;       // Access time
    uint8_t  create_10ms;            // 10ms increment
    uint8_t  modify_10ms;
    uint8_t  create_tz_offset;       // Timezone offset
    uint8_t  modify_tz_offset;
    uint8_t  access_tz_offset;
    uint8_t  reserved2[7];
} exfat_file_entry_t;

// Stream Extension Entry
typedef struct __attribute__((packed)) {
    uint8_t  entry_type;             // 0xC0
    uint8_t  flags;                  // General flags
    uint8_t  reserved1;
    uint8_t  name_length;            // Length of filename
    uint16_t name_hash;              // Hash of filename
    uint16_t reserved2;
    uint64_t valid_data_length;      // Valid data size
    uint32_t reserved3;
    uint32_t first_cluster;          // First cluster of data
    uint64_t data_length;            // File size in bytes
} exfat_stream_entry_t;

// File Name Extension Entry
typedef struct __attribute__((packed)) {
    uint8_t  entry_type;             // 0xC1
    uint8_t  flags;                  // General flags
    uint16_t file_name[15];          // Unicode filename (30 bytes)
} exfat_name_entry_t;

// Volume Label Entry
typedef struct __attribute__((packed)) {
    uint8_t  entry_type;             // 0x83
    uint8_t  char_count;             // Number of characters
    uint16_t volume_label[11];       // Unicode label (22 bytes)
    uint8_t  reserved[8];
} exfat_volume_label_entry_t;

// Allocation Bitmap Entry
typedef struct __attribute__((packed)) {
    uint8_t  entry_type;             // 0x81
    uint8_t  flags;
    uint8_t  reserved[18];
    uint32_t first_cluster;          // First cluster of bitmap
    uint64_t data_length;            // Bitmap size in bytes
} exfat_bitmap_entry_t;

// Upcase Table Entry
typedef struct __attribute__((packed)) {
    uint8_t  entry_type;             // 0x82
    uint8_t  reserved1[3];
    uint32_t table_checksum;
    uint8_t  reserved2[12];
    uint32_t first_cluster;          // First cluster of table
    uint64_t data_length;            // Table size in bytes
} exfat_upcase_entry_t;

// Mounted exFAT Volume Structure
typedef struct {
    exfat_boot_sector_t boot_sector;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t fat_start_sector;
    uint32_t cluster_heap_start_sector;
    uint32_t root_dir_cluster;
    uint8_t* fat_cache;              // Cached FAT table
    uint8_t* bitmap_cache;           // Cached allocation bitmap
    uint32_t bitmap_cluster;
    uint64_t bitmap_length;
} exfat_volume_t;

// File Handle Structure
typedef struct {
    uint32_t first_cluster;
    uint64_t file_size;
    uint64_t position;
    uint16_t attributes;
    uint8_t  is_open;
    uint8_t  is_directory;
    char     name[256];
} exfat_file_t;

// Function Prototypes

// Disk Initialization (for testing with memory disk)
void exfat_init_disk(uint32_t size_mb);
void exfat_set_paging_mode(void);

// Volume Operations
int exfat_mount(exfat_volume_t* volume);
void exfat_unmount(exfat_volume_t* volume);
int exfat_format(uint32_t total_sectors);
void exfat_list_root(exfat_volume_t* volume);

// File Operations
int exfat_open(exfat_volume_t* volume, const char* path, exfat_file_t* file);
int exfat_read(exfat_volume_t* volume, exfat_file_t* file, void* buffer, uint32_t size);
int exfat_write(exfat_volume_t* volume, exfat_file_t* file, const void* buffer, uint32_t size);
int exfat_close(exfat_file_t* file);
int exfat_seek(exfat_file_t* file, uint64_t offset);

// Directory Operations
int exfat_opendir(exfat_volume_t* volume, const char* path, exfat_file_t* dir);
int exfat_readdir(exfat_volume_t* volume, exfat_file_t* dir, char* name, uint32_t* size, uint16_t* attrs);
int exfat_mkdir(exfat_volume_t* volume, const char* path);
int exfat_rmdir(exfat_volume_t* volume, const char* path);

// File Management
int exfat_create(exfat_volume_t* volume, const char* path);
int exfat_delete(exfat_volume_t* volume, const char* path);
int exfat_rename(exfat_volume_t* volume, const char* old_path, const char* new_path);

// Utility Functions
void exfat_debug_boot_sector(exfat_boot_sector_t* boot);
uint32_t exfat_get_next_cluster(exfat_volume_t* volume, uint32_t cluster);
int exfat_read_cluster(exfat_volume_t* volume, uint32_t cluster, void* buffer);
int exfat_write_cluster(exfat_volume_t* volume, uint32_t cluster, const void* buffer);

// Disk I/O (exported for file operations)
int disk_read_sector(uint32_t sector, void* buffer);
int disk_write_sector(uint32_t sector, const void* buffer);

// Memory comparison helper
int memcmp(const void* s1, const void* s2, size_t n);

#endif // EXFAT_H
