#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include "types.h"
#include "metafs.h"
#include "paging.h"
#include "process.h"

/* ============================================================
 * ELF validation
 * ============================================================ */
int executable_is_elf(const void* data, size_t size);
int executable_validate(metafs_context_t* ctx, object_id_t id);
uint32_t executable_get_entry_point(const void* data, size_t size);

/* ============================================================
 * ELF loading
 * ============================================================ */
int executable_load_elf(
    const void* elf_data,
    size_t size,
    page_directory_t* page_dir
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
