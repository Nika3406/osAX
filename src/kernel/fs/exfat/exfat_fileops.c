// src/kernel/exfat_fileops.c - File operations for exFAT
#include "exfat.h"
#include "serial.h"
#include "kstring.h"
#include "heap.h"

// External disk I/O functions (from exfat.c)
extern int disk_read_sector(uint32_t sector, void* buffer);
extern int disk_write_sector(uint32_t sector, const void* buffer);

// Helper: Calculate 16-bit checksum for directory entry set
static uint16_t exfat_calc_checksum(const uint8_t* entries, uint32_t count) {
    uint16_t checksum = 0;
    uint32_t total_bytes = count * 32;  // Each entry is 32 bytes

    for (uint32_t i = 0; i < total_bytes; i++) {
        // Skip checksum field itself (bytes 2-3 of first entry)
        if (i == 2 || i == 3) {
            continue;
        }

        checksum = ((checksum << 15) | (checksum >> 1)) + entries[i];
    }

    return checksum;
}

// Helper: Calculate filename hash
static uint16_t exfat_calc_name_hash(const uint16_t* name, uint8_t len) {
    uint16_t hash = 0;

    for (uint8_t i = 0; i < len; i++) {
        uint16_t ch = name[i];
        // Simple upcase for ASCII (a-z -> A-Z)
        if (ch >= 'a' && ch <= 'z') {
            ch -= 32;
        }
        hash = ((hash << 15) | (hash >> 1)) + ch;
    }

    return hash;
}

// Helper: Convert ASCII string to Unicode
static void ascii_to_unicode(const char* ascii, uint16_t* unicode, uint8_t max_len) {
    uint8_t i;
    for (i = 0; i < max_len && ascii[i]; i++) {
        unicode[i] = (uint16_t)ascii[i];
    }

    // Pad with zeros
    for (; i < max_len; i++) {
        unicode[i] = 0;
    }
}

// Helper: Find free cluster in bitmap
static uint32_t exfat_alloc_cluster(exfat_volume_t* volume) {
    // For simplicity, linear search through FAT
    // In production, use bitmap for faster allocation

    uint32_t fat_entries = volume->boot_sector.cluster_count + 2;
    uint8_t* fat_sector = (uint8_t*)kmalloc(volume->bytes_per_sector);

    for (uint32_t cluster = 2; cluster < fat_entries; cluster++) {
        // Read FAT sector containing this cluster
        uint32_t fat_offset = cluster * 4;
        uint32_t sector = volume->fat_start_sector + (fat_offset / volume->bytes_per_sector);
        uint32_t offset_in_sector = fat_offset % volume->bytes_per_sector;

        if (disk_read_sector(sector, fat_sector) < 0) {
            kfree(fat_sector);
            return 0;
        }

        uint32_t entry = *(uint32_t*)(fat_sector + offset_in_sector);

        // If free (0x00000000), allocate it
        if (entry == 0x00000000) {
            // Mark as end of chain
            *(uint32_t*)(fat_sector + offset_in_sector) = 0xFFFFFFFF;
            disk_write_sector(sector, fat_sector);
            kfree(fat_sector);
            return cluster;
        }
    }

    kfree(fat_sector);
    kprintf("EXFAT: No free clusters available!\n");
    return 0;
}

// Helper: Write FAT entry
static int exfat_write_fat_entry(exfat_volume_t* volume, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t sector = volume->fat_start_sector + (fat_offset / volume->bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % volume->bytes_per_sector;

    uint8_t* fat_sector = (uint8_t*)kmalloc(volume->bytes_per_sector);
    if (disk_read_sector(sector, fat_sector) < 0) {
        kfree(fat_sector);
        return -1;
    }

    *(uint32_t*)(fat_sector + offset_in_sector) = value;

    int result = disk_write_sector(sector, fat_sector);
    kfree(fat_sector);
    return result;
}

// Helper: Find free directory entry slot in a cluster
static int exfat_find_free_entry(exfat_volume_t* volume, uint32_t dir_cluster, uint32_t entries_needed, uint32_t* entry_index) {
    uint8_t* cluster_data = (uint8_t*)kmalloc(volume->bytes_per_cluster);
    if (exfat_read_cluster(volume, dir_cluster, cluster_data) < 0) {
        kfree(cluster_data);
        return -1;
    }

    exfat_dir_entry_t* entries = (exfat_dir_entry_t*)cluster_data;
    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
    uint32_t consecutive_free = 0;
    uint32_t start_index = 0;

    for (uint32_t i = 0; i < entries_per_cluster; i++) {
        if (entries[i].entry_type == EXFAT_TYPE_EOD ||
            entries[i].entry_type == 0x00) {
            if (consecutive_free == 0) {
                start_index = i;
            }
            consecutive_free++;

        if (consecutive_free >= entries_needed) {
            *entry_index = start_index;
            kfree(cluster_data);
            return 0;
        }
            } else {
                consecutive_free = 0;
            }
    }

    kfree(cluster_data);
    return -1;  // Not enough space
}

// Create a directory
int exfat_mkdir(exfat_volume_t* volume, const char* path) {
    kprintf("EXFAT: Creating directory '%s'...\n", path);

    // Parse filename
    const char* dirname = path;
    if (dirname[0] == '/') {
        dirname++;
    }

    size_t name_len_sz = strlen(dirname);
    if (name_len_sz > 255) {
        kprintf("EXFAT: Directory name too long!\n");
        return -1;
    }
    uint8_t name_len = (uint8_t)name_len_sz;

    // Calculate entries needed
    uint32_t name_entries = (name_len + 14) / 15;
    uint32_t total_entries = 1 + 1 + name_entries;

    // Find free slot
    uint32_t entry_index;
    if (exfat_find_free_entry(volume, volume->root_dir_cluster,
        total_entries, &entry_index) < 0) {
        kprintf("EXFAT: No space in root directory!\n");
    return -1;
        }

        // Allocate cluster for directory
        uint32_t dir_cluster = exfat_alloc_cluster(volume);
        if (dir_cluster == 0) {
            return -1;
        }

        // Read directory cluster
        uint8_t* cluster_data = (uint8_t*)kmalloc(volume->bytes_per_cluster);
        if (exfat_read_cluster(volume, volume->root_dir_cluster, cluster_data) < 0) {
            kfree(cluster_data);
            return -1;
        }

        // Create file entry (with directory flag)
        exfat_file_entry_t* file_entry = (exfat_file_entry_t*)(cluster_data + (entry_index * 32));
        memset(file_entry, 0, sizeof(exfat_file_entry_t));
        file_entry->entry_type = EXFAT_TYPE_FILE;
        file_entry->secondary_count = 1 + name_entries;
        file_entry->file_attributes = EXFAT_ATTR_DIRECTORY;  // Mark as directory!

        // Create stream extension
        exfat_stream_entry_t* stream_entry = (exfat_stream_entry_t*)(cluster_data + ((entry_index + 1) * 32));
        memset(stream_entry, 0, sizeof(exfat_stream_entry_t));
        stream_entry->entry_type = EXFAT_TYPE_STREAM;
        stream_entry->flags = 0x01;
        stream_entry->name_length = name_len;
        stream_entry->first_cluster = dir_cluster;
        stream_entry->valid_data_length = 0;
        stream_entry->data_length = 0;

        // Convert to Unicode and hash
        uint16_t unicode_name[256];
        ascii_to_unicode(dirname, unicode_name, name_len);
        stream_entry->name_hash = exfat_calc_name_hash(unicode_name, name_len);

        // Create name entries
        uint32_t chars_written = 0;
        for (uint32_t i = 0; i < name_entries; i++) {
            exfat_name_entry_t* name_entry = (exfat_name_entry_t*)(cluster_data + ((entry_index + 2 + i) * 32));
            memset(name_entry, 0, sizeof(exfat_name_entry_t));
            name_entry->entry_type = EXFAT_TYPE_FILE_NAME;
            name_entry->flags = 0;

            for (uint32_t j = 0; j < 15 && chars_written < name_len; j++, chars_written++) {
                name_entry->file_name[j] = unicode_name[chars_written];
            }
        }

        // Calculate checksum
        file_entry->set_checksum = exfat_calc_checksum(cluster_data + (entry_index * 32), total_entries);

        // Write back
        if (exfat_write_cluster(volume, volume->root_dir_cluster, cluster_data) < 0) {
            kfree(cluster_data);
            return -1;
        }

        // Initialize the new directory (empty)
        uint8_t* empty_dir = (uint8_t*)kmalloc(volume->bytes_per_cluster);
        memset(empty_dir, 0, volume->bytes_per_cluster);
        exfat_write_cluster(volume, dir_cluster, empty_dir);
        kfree(empty_dir);

        kfree(cluster_data);
        kprintf("EXFAT: Directory '%s' created successfully!\n", dirname);
        return 0;
}

// Create a new file
int exfat_create(exfat_volume_t* volume, const char* path) {
    kprintf("EXFAT: Creating file '%s'...\n", path);

    // Parse filename (assume root directory for now)
    const char* filename = path;
    if (filename[0] == '/') {
        filename++;
    }

    size_t name_len_sz = strlen(filename);
    if (name_len_sz > 255) {
        kprintf("EXFAT: Filename too long!\n");
        return -1;
    }
    uint8_t name_len = (uint8_t)name_len_sz;

    // Calculate number of name entries needed (15 chars per entry)
    uint32_t name_entries = (name_len + 14) / 15;
    uint32_t total_entries = 1 + 1 + name_entries;  // File + Stream + Name entries

    // Find free slot in root directory
    uint32_t entry_index;
    if (exfat_find_free_entry(volume, volume->root_dir_cluster,
        total_entries, &entry_index) < 0) {
        kprintf("EXFAT: No space in root directory!\n");
    return -1;
        }

        kprintf("EXFAT: Found free entry at index %d\n", entry_index);

        // Allocate cluster for file data
        uint32_t file_cluster = exfat_alloc_cluster(volume);
        if (file_cluster == 0) {
            return -1;
        }

        kprintf("EXFAT: Allocated cluster %d for file\n", file_cluster);

        // Read directory cluster
        uint8_t* cluster_data = (uint8_t*)kmalloc(volume->bytes_per_cluster);
        if (exfat_read_cluster(volume, volume->root_dir_cluster, cluster_data) < 0) {
            kfree(cluster_data);
            return -1;
        }

        // Create file entry
        exfat_file_entry_t* file_entry = (exfat_file_entry_t*)(cluster_data + (entry_index * 32));
        memset(file_entry, 0, sizeof(exfat_file_entry_t));
        file_entry->entry_type = EXFAT_TYPE_FILE;
        file_entry->secondary_count = 1 + name_entries;
        file_entry->file_attributes = EXFAT_ATTR_ARCHIVE;
        // Timestamps would go here (set to 0 for now)

        // Create stream extension entry
        exfat_stream_entry_t* stream_entry = (exfat_stream_entry_t*)(cluster_data + ((entry_index + 1) * 32));
        memset(stream_entry, 0, sizeof(exfat_stream_entry_t));
        stream_entry->entry_type = EXFAT_TYPE_STREAM;
        stream_entry->flags = 0x01;  // Allocation possible
        stream_entry->name_length = name_len;
        stream_entry->first_cluster = file_cluster;
        stream_entry->valid_data_length = 0;
        stream_entry->data_length = 0;

        // Convert filename to Unicode
        uint16_t unicode_name[256];
        ascii_to_unicode(filename, unicode_name, name_len);

        // Calculate name hash
        stream_entry->name_hash = exfat_calc_name_hash(unicode_name, name_len);

        // Create name entries
        uint32_t chars_written = 0;
        for (uint32_t i = 0; i < name_entries; i++) {
            exfat_name_entry_t* name_entry = (exfat_name_entry_t*)(cluster_data + ((entry_index + 2 + i) * 32));
            memset(name_entry, 0, sizeof(exfat_name_entry_t));
            name_entry->entry_type = EXFAT_TYPE_FILE_NAME;
            name_entry->flags = 0;

            // Copy up to 15 characters
            for (uint32_t j = 0; j < 15 && chars_written < name_len; j++, chars_written++) {
                name_entry->file_name[j] = unicode_name[chars_written];
            }
        }

        // Calculate and set checksum
        file_entry->set_checksum = exfat_calc_checksum(cluster_data + (entry_index * 32),
                                                    total_entries);

        // Write directory cluster back
        if (exfat_write_cluster(volume, volume->root_dir_cluster, cluster_data) < 0) {
            kfree(cluster_data);
            return -1;
        }

        kfree(cluster_data);

        kprintf("EXFAT: File '%s' created successfully!\n", filename);
        return 0;
}

// Open an existing file
int exfat_open(exfat_volume_t* volume, const char* path, exfat_file_t* file) {
    kprintf("EXFAT: Opening file '%s'...\n", path);

    // Parse filename
    const char* filename = path;
    if (filename[0] == '/') {
        filename++;
    }

    // Read root directory
    uint8_t* cluster_data = (uint8_t*)kmalloc(volume->bytes_per_cluster);
    if (exfat_read_cluster(volume, volume->root_dir_cluster, cluster_data) < 0) {
        kfree(cluster_data);
        return -1;
    }

    exfat_dir_entry_t* entries = (exfat_dir_entry_t*)cluster_data;
    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;

    // Search for file
    for (uint32_t i = 0; i < entries_per_cluster; i++) {
        if (entries[i].entry_type == EXFAT_TYPE_FILE) {
            exfat_file_entry_t* file_entry = (exfat_file_entry_t*)&entries[i];
            exfat_stream_entry_t* stream_entry = (exfat_stream_entry_t*)&entries[i + 1];

            // Reconstruct filename
            char found_name[256] = {0};
            uint32_t name_entries = file_entry->secondary_count - 1;
            uint32_t chars_read = 0;

            for (uint32_t j = 0; j < name_entries && chars_read < stream_entry->name_length; j++) {
                exfat_name_entry_t* name_entry = (exfat_name_entry_t*)&entries[i + 2 + j];

                for (uint32_t k = 0; k < 15 && chars_read < stream_entry->name_length; k++, chars_read++) {
                    found_name[chars_read] = (char)name_entry->file_name[k];
                }
            }

            // Compare filenames (simple ASCII comparison)
            int match = 1;
            for (uint32_t j = 0; j < stream_entry->name_length; j++) {
                if (found_name[j] != filename[j]) {
                    match = 0;
                    break;
                }
            }

            if (match && filename[stream_entry->name_length] == '\0') {
                // Found the file!
                file->first_cluster = stream_entry->first_cluster;
                file->file_size = stream_entry->data_length;
                file->position = 0;
                file->attributes = file_entry->file_attributes;
                file->is_open = 1;
                file->is_directory = (file_entry->file_attributes & EXFAT_ATTR_DIRECTORY) ? 1 : 0;

                // Copy name
                for (uint32_t j = 0; j < stream_entry->name_length && j < 255; j++) {
                    file->name[j] = found_name[j];
                }
                file->name[stream_entry->name_length] = '\0';

                kfree(cluster_data);
                kprintf("EXFAT: File opened: '%s', size=%d bytes, cluster=%d\n",
                        file->name, (uint32_t)file->file_size, file->first_cluster);
                return 0;
            }
        }
    }

    kfree(cluster_data);
    kprintf("EXFAT: File not found!\n");
    return -1;
}

// Read from file
int exfat_read(exfat_volume_t* volume, exfat_file_t* file, void* buffer, uint32_t size) {
    if (!file->is_open) {
        return -1;
    }

    if (file->position >= file->file_size) {
        return 0;  // EOF
    }

    // Adjust size if reading past EOF
    if (file->position + size > file->file_size) {
        size = (uint32_t)(file->file_size - file->position);
    }

    kprintf("EXFAT: Reading %d bytes from position %d...\n", size, (uint32_t)file->position);

    uint32_t bytes_read = 0;
    uint32_t current_cluster = file->first_cluster;

    // Use 32-bit arithmetic only - cast position to uint32_t
    uint32_t offset_in_file = (uint32_t)file->position;

    // Skip to correct cluster based on file position
    uint32_t clusters_to_skip = offset_in_file / volume->bytes_per_cluster;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        current_cluster = exfat_get_next_cluster(volume, current_cluster);
        if (current_cluster >= 0xFFFFFFF8) {
            return bytes_read;  // Reached end of chain
        }
    }

    uint32_t offset_in_cluster = offset_in_file % volume->bytes_per_cluster;
    uint8_t* cluster_buffer = (uint8_t*)kmalloc(volume->bytes_per_cluster);

    while (bytes_read < size) {
        // Read cluster
        if (exfat_read_cluster(volume, current_cluster, cluster_buffer) < 0) {
            break;
        }

        // Copy data from cluster
        uint32_t bytes_to_copy = volume->bytes_per_cluster - offset_in_cluster;
        if (bytes_to_copy > size - bytes_read) {
            bytes_to_copy = size - bytes_read;
        }

        memcpy((uint8_t*)buffer + bytes_read, cluster_buffer + offset_in_cluster, bytes_to_copy);
        bytes_read += bytes_to_copy;
        file->position += bytes_to_copy;
        offset_in_cluster = 0;  // Reset for next cluster

        // Move to next cluster if needed
        if (bytes_read < size) {
            current_cluster = exfat_get_next_cluster(volume, current_cluster);
            if (current_cluster >= 0xFFFFFFF8) {
                break;  // End of chain
            }
        }
    }

    kfree(cluster_buffer);
    kprintf("EXFAT: Read %d bytes\n", bytes_read);
    return bytes_read;
}

// Helper: Update file size in directory entry
static int exfat_update_file_size(exfat_volume_t* volume, const char* filename, uint64_t new_size) {
    // Read root directory
    uint8_t* cluster_data = (uint8_t*)kmalloc(volume->bytes_per_cluster);
    if (exfat_read_cluster(volume, volume->root_dir_cluster, cluster_data) < 0) {
        kfree(cluster_data);
        return -1;
    }

    exfat_dir_entry_t* entries = (exfat_dir_entry_t*)cluster_data;
    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;

    // Search for file
    for (uint32_t i = 0; i < entries_per_cluster; i++) {
        if (entries[i].entry_type == EXFAT_TYPE_FILE) {
            exfat_file_entry_t* file_entry = (exfat_file_entry_t*)&entries[i];
            exfat_stream_entry_t* stream_entry = (exfat_stream_entry_t*)&entries[i + 1];

            // Reconstruct filename to check if this is our file
            char found_name[256] = {0};
            uint32_t name_entries = file_entry->secondary_count - 1;
            uint32_t chars_read = 0;

            for (uint32_t j = 0; j < name_entries && chars_read < stream_entry->name_length; j++) {
                exfat_name_entry_t* name_entry = (exfat_name_entry_t*)&entries[i + 2 + j];
                for (uint32_t k = 0; k < 15 && chars_read < stream_entry->name_length; k++, chars_read++) {
                    found_name[chars_read] = (char)name_entry->file_name[k];
                }
            }

            // Check if filenames match
            int match = 1;
            for (uint32_t j = 0; j < stream_entry->name_length; j++) {
                if (found_name[j] != filename[j]) {
                    match = 0;
                    break;
                }
            }

            if (match && filename[stream_entry->name_length] == '\0') {
                // Found it! Update the size
                stream_entry->data_length = new_size;
                stream_entry->valid_data_length = new_size;

                // Write directory back
                if (exfat_write_cluster(volume, volume->root_dir_cluster, cluster_data) < 0) {
                    kfree(cluster_data);
                    return -1;
                }

                kfree(cluster_data);
                return 0;
            }
        }
    }

    kfree(cluster_data);
    return -1;  // File not found
}

// Write to file
int exfat_write(exfat_volume_t* volume, exfat_file_t* file, const void* buffer, uint32_t size) {
    if (!file->is_open) {
        return -1;
    }

    kprintf("EXFAT: Writing %d bytes at position %d...\n", size, (uint32_t)file->position);

    uint32_t bytes_written = 0;
    uint32_t current_cluster = file->first_cluster;

    // Use 32-bit arithmetic only
    uint32_t offset_in_file = (uint32_t)file->position;

    // Navigate to correct cluster
    uint32_t clusters_to_skip = offset_in_file / volume->bytes_per_cluster;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        uint32_t next = exfat_get_next_cluster(volume, current_cluster);
        if (next >= 0xFFFFFFF8) {
            // Need to allocate new cluster
            uint32_t new_cluster = exfat_alloc_cluster(volume);
            if (new_cluster == 0) return bytes_written;
            exfat_write_fat_entry(volume, current_cluster, new_cluster);
            current_cluster = new_cluster;
        } else {
            current_cluster = next;
        }
    }

    uint32_t offset_in_cluster = offset_in_file % volume->bytes_per_cluster;
    uint8_t* cluster_buffer = (uint8_t*)kmalloc(volume->bytes_per_cluster);

    while (bytes_written < size) {
        // Read existing cluster data (for partial writes)
        if (exfat_read_cluster(volume, current_cluster, cluster_buffer) < 0) {
            // If read fails, zero the buffer
            memset(cluster_buffer, 0, volume->bytes_per_cluster);
        }

        // Copy new data into cluster
        uint32_t bytes_to_copy = volume->bytes_per_cluster - offset_in_cluster;
        if (bytes_to_copy > size - bytes_written) {
            bytes_to_copy = size - bytes_written;
        }

        memcpy(cluster_buffer + offset_in_cluster, (const uint8_t*)buffer + bytes_written, bytes_to_copy);

        // Write cluster back
        if (exfat_write_cluster(volume, current_cluster, cluster_buffer) < 0) {
            break;
        }

        bytes_written += bytes_to_copy;
        file->position += bytes_to_copy;
        offset_in_cluster = 0;

        // Allocate next cluster if needed
        if (bytes_written < size) {
            uint32_t next = exfat_get_next_cluster(volume, current_cluster);
            if (next >= 0xFFFFFFF8) {
                uint32_t new_cluster = exfat_alloc_cluster(volume);
                if (new_cluster == 0) break;
                exfat_write_fat_entry(volume, current_cluster, new_cluster);
                current_cluster = new_cluster;
            } else {
                current_cluster = next;
            }
        }
    }

    kfree(cluster_buffer);

    // Update file size if we extended it
    if (file->position > file->file_size) {
        uint64_t old_size = file->file_size;
        file->file_size = file->position;

        // Update the directory entry with new size
        if (exfat_update_file_size(volume, file->name, file->file_size) == 0) {
            kprintf("EXFAT: Updated file size in directory: %d -> %d bytes\n",
                    (uint32_t)old_size, (uint32_t)file->file_size);
        } else {
            kprintf("EXFAT: Warning - could not update file size in directory\n");
        }
    }

    kprintf("EXFAT: Wrote %d bytes\n", bytes_written);
    return bytes_written;
}

// Close file
int exfat_close(exfat_file_t* file) {
    if (!file->is_open) {
        return -1;
    }

    file->is_open = 0;
    kprintf("EXFAT: File '%s' closed\n", file->name);
    return 0;
}

// Seek to position in file
int exfat_seek(exfat_file_t* file, uint64_t offset) {
    if (!file->is_open) {
        return -1;
    }

    file->position = offset;
    return 0;
}
