// src/include/core/executable.h - x86_64 VERSION
#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include "types.h"
#include "../fs/metafs.h"
#include "paging.h"
#include "process.h"

/* ============================================================
 * ELF validation
 * ============================================================ */
int executable_is_elf(const void* data, size_t size);
int executable_validate(metafs_context_t* ctx, object_id_t id);

// Get entry point (now returns 64-bit address)
uint64_t executable_get_entry_point(const void* data, size_t size);

/* ============================================================
 * ELF loading
 * ============================================================ */
int executable_load_elf(
    const void* elf_data,
    size_t size,
    page_directory_t* page_dir  // PML4 in x86_64
);

/* ============================================================
 * Execution (OBJECT-BASED ONLY)
 * ============================================================ */
int executable_run_object(
    metafs_context_t* ctx,
    object_id_t id,
    int argc,
    char** argv
);

#endif /* EXECUTABLE_H */
