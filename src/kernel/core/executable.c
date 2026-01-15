#include "executable.h" 
#include "process.h"
#include "terminal.h"
#include "kstring.h" 
#include "serial.h" 
#include "heap.h" 
#include "physical_mm.h" 
#include "metafs.h"

/* ============================================================
 * ELF constants
 * ============================================================ */
#define ELF_MAGIC 0x464C457F  /* 0x7F 'E' 'L' 'F' */

typedef struct {
    uint32_t magic;
    uint8_t  class_;
    uint8_t  data;
    uint8_t  version;
    uint8_t  osabi;
    uint8_t  abiversion;
    uint8_t  pad[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf32_header_t;

/* ============================================================
 * ELF helpers
 * ============================================================ */
int executable_is_elf(const void* data, size_t size) {
    if (!data || size < sizeof(elf32_header_t))
        return 0;

    const elf32_header_t* hdr = (const elf32_header_t*)data;
    return hdr->magic == ELF_MAGIC;
}

uint32_t executable_get_entry_point(const void* data, size_t size) {
    if (!executable_is_elf(data, size))
        return 0;

    const elf32_header_t* hdr = (const elf32_header_t*)data;
    return hdr->entry;
}

/* ============================================================
 * Validation
 * ============================================================ */
int executable_validate(metafs_context_t* ctx, object_id_t id) {
    uint8_t header[64];
    int bytes = metafs_read(ctx, id, header, sizeof(header));
    
    if (bytes < 0)
        return -1;

    if (!executable_is_elf(header, (size_t)bytes)) {
        terminal_writeln("exec: invalid ELF header");
        return -1;
    }

    return 0;
}

/* ============================================================
 * ELF loading
 * ============================================================ */
int executable_load_elf(
    const void* elf_data,
    size_t size,
    page_directory_t* page_dir
) {
    (void)elf_data;
    (void)size;
    (void)page_dir;

    /*
     * Stub for now.
     * You will:
     *  - iterate program headers
     *  - map pages
     *  - copy segments
     */
    return 0;
}

/* ============================================================
 * EXECUTION ENTRY POINT (OBJECT-BASED)
 * ============================================================ */
int executable_run_object(
    metafs_context_t* ctx,
    object_id_t id,
    int argc,
    char** argv
) {
    (void)argc;  // Unused for now
    
    if (executable_validate(ctx, id) != 0)
        return -1;

    metafs_core_meta_t meta;
    if (metafs_get_core_meta(ctx, id, &meta) != 0)
        return -1;

    uint32_t file_size = (uint32_t)meta.size;
    void* file_data = kmalloc(file_size);
    if (!file_data)
        return -1;

    ssize_t bytes_read = metafs_read(ctx, id, file_data, file_size);
    if (bytes_read < 0 || (size_t)bytes_read != file_size) {
        kfree(file_data);
        return -1;
    }

    uint32_t entry = executable_get_entry_point(file_data, file_size);
    if (!entry) {
        terminal_writeln("exec: no entry point");
        kfree(file_data);
        return -1;
    }

    // Cast entry point address to function pointer
    void (*entry_func)(void) = (void (*)(void))entry;

    process_t* proc = process_create(
        argv[0],          /* process name */
        entry_func,       /* entry point as function pointer */
        0                 /* user process */
    );

    if (!proc) {
        kfree(file_data);
        return -1;
    }

    if (executable_load_elf(file_data, file_size, proc->page_dir) != 0) {
        process_destroy(proc);
        kfree(file_data);
        return -1;
    }

    // Note: process_start() needs to be implemented
    // For now, just log that we would start it
    terminal_writeln("exec: process created (start not yet implemented)");

    kfree(file_data);
    return 0;
}