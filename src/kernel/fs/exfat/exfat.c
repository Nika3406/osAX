// src/kernel/exfat.c - exFAT with unified memory support
#include "exfat.h"
#include "serial.h"
#include "paging.h"
#include "kstring.h"
#include "heap.h"
#include "dma.h"
// Memory-based disk for testing
static uint8_t* disk_buffer = NULL;
static uint32_t disk_size_sectors = 0;
static int paging_is_enabled = 0;
// Use DMA-allocated buffer instead of static array
static uint8_t* sector_buffer = NULL;

// Initialize DMA buffer (must be called after DMA subsystem init)
void exfat_init_dma(void) {
    if (!sector_buffer) {
        sector_buffer = (uint8_t*)dma_alloc(4096);
    if (!sector_buffer) {
        kprintf("exFAT: Failed to allocate DMA sector buffer!\n");
        return;
    }
    kprintf("exFAT: Using DMA buffer at 0x%08x\n", (uint32_t)sector_buffer);
    }
}

// Modified initialization function
void exfat_init_disk(uint32_t size_mb) {
    disk_size_sectors = (size_mb * 1024 * 1024) / 512;
    uint32_t total_bytes = disk_size_sectors * 512;

    kprintf("EXFAT: Allocating %d MB disk buffer...\n", size_mb);

    // Use appropriate allocation method
    if (paging_is_enabled) {
        // After paging: use virtual heap (can handle large allocations)
        disk_buffer = (uint8_t*)kmalloc_virtual(total_bytes);
    } else {
        // Before paging: use regular heap (limited by identity mapping)
        disk_buffer = (uint8_t*)kmalloc(total_bytes);
    }

    if (!disk_buffer) {
        kprintf("EXFAT: Failed to allocate disk buffer!\n");
        return;
    }

    kprintf("EXFAT: Buffer allocated at 0x%08x\n", (uint32_t)disk_buffer);

    // Initialize DMA buffer for sector I/O
    exfat_init_dma();
}

void exfat_set_paging_mode(void) {
    paging_is_enabled = 1;
}

// Read sector from disk
int disk_read_sector(uint32_t sector, void* buffer) {
    if (!disk_buffer) {
        kprintf("Disk read error: disk_buffer is NULL\n");
        return -1;
    }
    
    if (sector >= disk_size_sectors) {
        kprintf("Disk read error: sector %d >= %d\n", sector, disk_size_sectors);
        return -1;
    }
    
    if (!buffer) {
        kprintf("Disk read error: buffer is NULL\n");
        return -1;
    }

    // Copy sector data (may contain garbage if never written, but that's OK)
    uint8_t* src = disk_buffer + (sector * 512);
    uint8_t* dst = (uint8_t*)buffer;
    
    // Debug: print source address for first read
    static int first_read = 1;
    if (first_read) {
        kprintf("EXFAT: First read - sector=%d, src=0x%08x, dst=0x%08x\n", 
                sector, (uint32_t)src, (uint32_t)dst);
        first_read = 0;
    }
    
    for (int i = 0; i < 512; i++) {
        dst[i] = src[i];
    }
    return 0;
}

// Write sector to disk
int disk_write_sector(uint32_t sector, const void* buffer) {
    if (!disk_buffer || sector >= disk_size_sectors) {
        return -1;
    }

    // Copy data to disk buffer
    const uint8_t* src = (const uint8_t*)buffer;
    uint8_t* dst = disk_buffer + (sector * 512);
    for (int i = 0; i < 512; i++) {
        dst[i] = src[i];
    }
    return 0;
}

// Calculate checksum for boot sector
static uint32_t exfat_boot_checksum(const uint8_t* sector, uint32_t bytes) {
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < bytes; i++) {
        if (i == 106 || i == 107 || i == 112) {
            continue;
        }
        checksum = ((checksum << 31) | (checksum >> 1)) + sector[i];
    }

    return checksum;
}

// Format a volume as exFAT
int exfat_format(uint32_t total_sectors) {
    kprintf("EXFAT: Formatting volume (%d sectors = %d MB)...\n",
            total_sectors, (total_sectors * 512) / 1024 / 1024);

    // Allocate boot sector
    exfat_boot_sector_t* boot = (exfat_boot_sector_t*)kmalloc(512);
    memset(boot, 0, 512);

    // Fill in boot sector
    boot->jump_boot[0] = 0xEB;
    boot->jump_boot[1] = 0x76;
    boot->jump_boot[2] = 0x90;

    memcpy(boot->fs_name, "EXFAT   ", 8);

    boot->partition_offset = 0;
    boot->volume_length = total_sectors;

    // Calculate FAT and cluster sizes
    boot->bytes_per_sector_shift = 9;  // 512 bytes = 2^9
    boot->sectors_per_cluster_shift = 3;  // 8 sectors per cluster = 4KB

    uint32_t bytes_per_sector = 1 << boot->bytes_per_sector_shift;
    uint32_t sectors_per_cluster = 1 << boot->sectors_per_cluster_shift;

    // FAT starts after boot region (24 sectors: 12 boot + 12 backup)
    boot->fat_offset = 24;

    // Calculate number of clusters
    uint32_t usable_sectors = total_sectors - boot->fat_offset;
    uint32_t max_clusters = usable_sectors / sectors_per_cluster;

    // FAT needs 4 bytes per cluster
    boot->fat_length = (max_clusters * 4 + bytes_per_sector - 1) / bytes_per_sector;

    // Cluster heap starts after FAT(s)
    boot->number_of_fats = 1;
    boot->cluster_heap_offset = boot->fat_offset + (boot->fat_length * boot->number_of_fats);

    // Recalculate actual cluster count
    uint32_t heap_sectors = total_sectors - boot->cluster_heap_offset;
    boot->cluster_count = heap_sectors / sectors_per_cluster;

    // Root directory at cluster 2 (first valid cluster)
    boot->root_dir_cluster = 2;

    // Other fields
    boot->volume_serial = 0x12345678;
    boot->fs_revision = 0x0100;  // Version 1.0
    boot->volume_flags = 0x0000;
    boot->drive_select = 0x80;
    boot->percent_in_use = 0;

    boot->boot_signature = 0xAA55;

    // Write boot sector
    disk_write_sector(0, boot);

    kprintf("EXFAT: Boot sector written\n");
    kprintf("  Bytes per sector: %d\n", bytes_per_sector);
    kprintf("  Sectors per cluster: %d\n", sectors_per_cluster);
    kprintf("  FAT offset: %d sectors\n", boot->fat_offset);
    kprintf("  FAT length: %d sectors\n", boot->fat_length);
    kprintf("  Cluster heap offset: %d sectors\n", boot->cluster_heap_offset);
    kprintf("  Total clusters: %d\n", boot->cluster_count);
    kprintf("  Root directory: cluster %d\n", boot->root_dir_cluster);

    // Initialize FAT (mark first clusters as used)
    uint8_t* fat_buffer = (uint8_t*)kmalloc(bytes_per_sector);
    memset(fat_buffer, 0, bytes_per_sector);

    uint32_t* fat = (uint32_t*)fat_buffer;
    fat[0] = 0xFFFFFFF8;  // Media descriptor
    fat[1] = 0xFFFFFFFF;  // End of chain marker
    fat[2] = 0xFFFFFFFF;  // Root directory (end of chain)

    disk_write_sector(boot->fat_offset, fat_buffer);

    // Create root directory entries
    exfat_dir_entry_t* entries = (exfat_dir_entry_t*)kmalloc(bytes_per_sector);
    memset(entries, 0, bytes_per_sector);

    // Entry 0: Volume label
    exfat_volume_label_entry_t* label = (exfat_volume_label_entry_t*)&entries[0];
    label->entry_type = EXFAT_TYPE_VOLUME_LABEL;
    label->char_count = 6;
    label->volume_label[0] = 'E';
    label->volume_label[1] = 'X';
    label->volume_label[2] = 'F';
    label->volume_label[3] = 'A';
    label->volume_label[4] = 'T';
    label->volume_label[5] = ' ';

    // Entry 1: Allocation bitmap
    exfat_bitmap_entry_t* bitmap = (exfat_bitmap_entry_t*)&entries[1];
    bitmap->entry_type = EXFAT_TYPE_ALLOCATION;
    bitmap->flags = 0;
    bitmap->first_cluster = 3;
    bitmap->data_length = (boot->cluster_count + 7) / 8;

    // Write root directory
    uint32_t root_sector = boot->cluster_heap_offset +
    ((boot->root_dir_cluster - 2) * sectors_per_cluster);
    disk_write_sector(root_sector, entries);

    kprintf("EXFAT: Root directory created\n");
    kprintf("EXFAT: Format complete!\n\n");

    kfree(fat_buffer);
    kfree(entries);
    kfree(boot);

    return 0;
}

// Mount an exFAT volume
int exfat_mount(exfat_volume_t* volume) {
    kprintf("EXFAT: Mounting volume...\n");

    // Read boot sector
    uint8_t* sector = (uint8_t*)kmalloc(512);
    if (disk_read_sector(0, sector) < 0) {
        kprintf("EXFAT: Failed to read boot sector\n");
        kfree(sector);
        return -1;
    }

    // Copy to volume structure
    memcpy(&volume->boot_sector, sector, sizeof(exfat_boot_sector_t));
    kfree(sector);

    // Verify signature
    if (volume->boot_sector.boot_signature != 0xAA55) {
        kprintf("EXFAT: Invalid boot signature: %x\n",
                volume->boot_sector.boot_signature);
        return -1;
    }

    // Verify filesystem name
    if (memcmp(volume->boot_sector.fs_name, "EXFAT   ", 8) != 0) {
        kprintf("EXFAT: Not an exFAT filesystem\n");
        return -1;
    }

    // Calculate sizes
    volume->bytes_per_sector = 1 << volume->boot_sector.bytes_per_sector_shift;
    volume->sectors_per_cluster = 1 << volume->boot_sector.sectors_per_cluster_shift;
    volume->bytes_per_cluster = volume->bytes_per_sector * volume->sectors_per_cluster;

    volume->fat_start_sector = volume->boot_sector.fat_offset;
    volume->cluster_heap_start_sector = volume->boot_sector.cluster_heap_offset;
    volume->root_dir_cluster = volume->boot_sector.root_dir_cluster;

    kprintf("EXFAT: Volume mounted successfully\n");
    kprintf("  Bytes per sector: %d\n", volume->bytes_per_sector);
    kprintf("  Sectors per cluster: %d\n", volume->sectors_per_cluster);
    kprintf("  Bytes per cluster: %d\n", volume->bytes_per_cluster);
    kprintf("  Total clusters: %d\n", volume->boot_sector.cluster_count);
    kprintf("  Root directory: cluster %d\n\n", volume->root_dir_cluster);

    return 0;
}

// Debug print boot sector
void exfat_debug_boot_sector(exfat_boot_sector_t* boot) {
    kprintf("\n=== exFAT Boot Sector ===\n");
    kprintf("FS Name: ");
    for (int i = 0; i < 8; i++) {
        serial_putc(boot->fs_name[i]);
    }
    kprintf("\n");

    kprintf("Partition offset: %d\n", (uint32_t)boot->partition_offset);
    kprintf("Volume length: %d sectors\n", (uint32_t)boot->volume_length);
    kprintf("FAT offset: %d sectors\n", boot->fat_offset);
    kprintf("FAT length: %d sectors\n", boot->fat_length);
    kprintf("Cluster heap offset: %d sectors\n", boot->cluster_heap_offset);
    kprintf("Cluster count: %d\n", boot->cluster_count);
    kprintf("Root directory cluster: %d\n", boot->root_dir_cluster);
    kprintf("Volume serial: %x\n", boot->volume_serial);
    kprintf("FS revision: %x\n", boot->fs_revision);
    kprintf("Bytes per sector: %d (2^%d)\n",
            1 << boot->bytes_per_sector_shift, boot->bytes_per_sector_shift);
    kprintf("Sectors per cluster: %d (2^%d)\n",
            1 << boot->sectors_per_cluster_shift, boot->sectors_per_cluster_shift);
    kprintf("Number of FATs: %d\n", boot->number_of_fats);
    kprintf("Boot signature: %x\n", boot->boot_signature);
    kprintf("========================\n\n");
}

// Read a cluster from disk
int exfat_read_cluster(exfat_volume_t* volume, uint32_t cluster, void* buffer) {
    if (cluster < 2 || cluster >= volume->boot_sector.cluster_count + 2) {
        return -1;
    }

    uint32_t first_sector = volume->cluster_heap_start_sector +
    ((cluster - 2) * volume->sectors_per_cluster);

    for (uint32_t i = 0; i < volume->sectors_per_cluster; i++) {
        if (disk_read_sector(first_sector + i,
            (uint8_t*)buffer + (i * volume->bytes_per_sector)) < 0) {
            return -1;
            }
    }

    return 0;
}

// Write a cluster to disk
int exfat_write_cluster(exfat_volume_t* volume, uint32_t cluster, const void* buffer) {
    if (cluster < 2 || cluster >= volume->boot_sector.cluster_count + 2) {
        return -1;
    }

    uint32_t first_sector = volume->cluster_heap_start_sector +
    ((cluster - 2) * volume->sectors_per_cluster);

    for (uint32_t i = 0; i < volume->sectors_per_cluster; i++) {
        if (disk_write_sector(first_sector + i,
            (const uint8_t*)buffer + (i * volume->bytes_per_sector)) < 0) {
            return -1;
            }
    }

    return 0;
}

// Get next cluster from FAT
uint32_t exfat_get_next_cluster(exfat_volume_t* volume, uint32_t cluster) {
    if (cluster < 2 || cluster >= volume->boot_sector.cluster_count + 2) {
        return 0xFFFFFFFF;
    }

    // Calculate FAT entry position
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = volume->fat_start_sector + (fat_offset / volume->bytes_per_sector);
    uint32_t fat_entry_offset = fat_offset % volume->bytes_per_sector;

    // Read FAT sector
    uint8_t* sector = (uint8_t*)kmalloc(volume->bytes_per_sector);
    if (disk_read_sector(fat_sector, sector) < 0) {
        kfree(sector);
        return 0xFFFFFFFF;
    }

    uint32_t next_cluster = *(uint32_t*)(sector + fat_entry_offset);
    kfree(sector);

    return next_cluster;
}

// List root directory contents
void exfat_list_root(exfat_volume_t* volume) {
    kprintf("\n=== Root Directory Listing ===\n");

    uint8_t* cluster_buffer = (uint8_t*)kmalloc(volume->bytes_per_cluster);
    if (exfat_read_cluster(volume, volume->root_dir_cluster, cluster_buffer) < 0) {
        kprintf("Failed to read root directory\n");
        kfree(cluster_buffer);
        return;
    }

    exfat_dir_entry_t* entries = (exfat_dir_entry_t*)cluster_buffer;
    int entry_count = volume->bytes_per_cluster / sizeof(exfat_dir_entry_t);

    for (int i = 0; i < entry_count; i++) {
        uint8_t type = entries[i].entry_type;

        if (type == EXFAT_TYPE_EOD) {
            break;
        }

        kprintf("Entry %d: Type 0x%x ", i, type);

        switch (type) {
            case EXFAT_TYPE_VOLUME_LABEL:
                kprintf("(Volume Label)\n");
                break;
            case EXFAT_TYPE_ALLOCATION:
                kprintf("(Allocation Bitmap)\n");
                break;
            case EXFAT_TYPE_UPCASE:
                kprintf("(Upcase Table)\n");
                break;
            case EXFAT_TYPE_FILE:
                kprintf("(File)\n");
                break;
            case EXFAT_TYPE_STREAM:
                kprintf("(Stream Extension)\n");
                break;
            case EXFAT_TYPE_FILE_NAME:
                kprintf("(File Name)\n");
                break;
            default:
                kprintf("(Unknown)\n");
                break;
        }
    }

    kprintf("==============================\n\n");
    kfree(cluster_buffer);
}

// Compare memory
int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }

    return 0;
}
