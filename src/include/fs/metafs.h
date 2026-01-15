#ifndef METAFS_H
#define METAFS_H

#include "../core/types.h"
#include "exfat.h"

/* ============================================================
 * Object Identity
 * ============================================================ */

typedef struct {
    uint64_t high;
    uint64_t low;
} object_id_t;

#define OBJECT_ID_NULL ((object_id_t){0, 0})

static inline int object_id_is_null(object_id_t id) {
    return (id.high == 0 && id.low == 0);
}

/* ============================================================
 * Object Types
 * ============================================================ */

typedef enum {
    OBJ_TYPE_UNKNOWN = 0,
    OBJ_TYPE_EXECUTABLE,
    OBJ_TYPE_DOCUMENT,
    OBJ_TYPE_IMAGE,
    OBJ_TYPE_VIDEO,
    OBJ_TYPE_AUDIO,
    OBJ_TYPE_ARCHIVE,
    OBJ_TYPE_DATA
} object_type_t;

/* ============================================================
 * Core Metadata (on-disk, fixed)
 * ============================================================ */

#define META_MAGIC   0x4D455441  /* "META" */
#define META_VERSION 1
#define META_FLAG_EXECUTABLE 0x0001

typedef struct {
    uint32_t    magic;
    uint32_t    version;
    object_id_t id;
    object_type_t type;
    uint64_t    size;
    uint64_t    created;
    uint64_t    modified;
    uint32_t    flags;
    uint32_t    checksum;
} __attribute__((packed)) metafs_core_meta_t;

/* For backwards compatibility */
typedef metafs_core_meta_t core_metadata_t;

/* ============================================================
 * Extended Metadata (on-disk, mutable)
 * ============================================================ */

typedef struct {
    char name[64];    /* optional, non-unique */
    char view[64];    /* optional, non-exclusive */
    char tags[256];   /* space- or comma-separated */
} metafs_ext_meta_t;

/* ============================================================
 * Object Index Entry (in-memory)
 * ============================================================ */

typedef struct {
    object_id_t id;
    object_type_t type;
    uint64_t    data_offset;
    uint64_t    meta_offset;
    uint32_t    checksum;
    char        name[64];
    char        view[64];
    char        extension[16];  // stores "txt", "c", "png", etc
} object_index_entry_t;

/* For backwards compatibility */
typedef object_index_entry_t metafs_index_entry_t;

/* ============================================================
 * Views
 * ============================================================ */

typedef enum {
    VIEW_STATIC_APPS,
    VIEW_STATIC_DOCUMENTS,
    VIEW_STATIC_MEDIA,
    VIEW_DYNAMIC
} view_type_t;

typedef struct {
    char name[64];
    view_type_t type;
    object_type_t filter_type;
} view_definition_t;

/* ============================================================
 * Object Handle
 * ============================================================ */

typedef struct {
    object_id_t id;
    int is_open;
    struct {
        metafs_core_meta_t core;
        int has_extended;
        metafs_ext_meta_t extended;
    } metadata;
} object_handle_t;

/* ============================================================
 * Object Metadata (combined)
 * ============================================================ */

typedef struct {
    metafs_core_meta_t core;
    int has_extended;
    metafs_ext_meta_t extended;
} object_metadata_t;

/* ============================================================
 * Database Header
 * ============================================================ */

#define METADATA_DB_MAGIC 0x4D444220  /* "MDB " */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_objects;
    uint32_t num_views;
    uint64_t last_sync;
} __attribute__((packed)) metadata_db_header_t;

/* ============================================================
 * View Entry (for listing)
 * ============================================================ */

typedef struct {
    char name[256];
    object_id_t id;
    object_type_t type;
    uint64_t size;
    uint64_t created;
} metafs_view_entry_t;

/* ============================================================
 * MetaFS Context
 * ============================================================ */

typedef struct {
    exfat_volume_t*      volume;

    metafs_index_entry_t* index;
    uint32_t              num_objects;
    uint32_t              max_objects;
    
    view_definition_t     views[64];
    uint32_t              num_views;

    uint64_t              last_object_id;
    
    /* For compatibility */
    uint32_t              index_count;
    uint32_t              index_capacity;
    uint64_t              next_object_id;
} metafs_context_t;

/* ============================================================
 * MetaFS API
 * ============================================================ */

/* CRC32 */
uint32_t metafs_crc32(const void* data, size_t len);

/* ObjectID operations */
int metafs_object_id_equal(object_id_t a, object_id_t b);
object_id_t metafs_generate_object_id(metafs_context_t* ctx);

/* Type inference */
object_type_t metafs_infer_type(const void* data, size_t size);
const char* metafs_type_to_string(object_type_t type);

/* lifecycle */
int  metafs_init(metafs_context_t* fs, exfat_volume_t* vol);
int  metafs_format(metafs_context_t* fs);
int  metafs_mount(metafs_context_t* fs);
void metafs_sync(metafs_context_t* fs);

/* Persistence */
int metafs_save_index(metafs_context_t* ctx);
int metafs_load_index(metafs_context_t* ctx);

/* object lifecycle */
object_id_t metafs_object_create(metafs_context_t* fs, object_type_t type);
int         metafs_object_delete(metafs_context_t* fs, object_id_t id);
int         metafs_object_open(metafs_context_t* ctx, object_id_t id, object_handle_t* handle);
int         metafs_object_close(object_handle_t* handle);

/* metadata */
int metafs_get_core_meta(metafs_context_t* fs,
                         object_id_t id,
                         metafs_core_meta_t* out);

int metafs_get_ext_meta(metafs_context_t* fs,
                        object_id_t id,
                        metafs_ext_meta_t* out);

int metafs_set_ext_meta(metafs_context_t* fs,
                        object_id_t id,
                        const metafs_ext_meta_t* in);

int metafs_metadata_get(metafs_context_t* ctx, object_id_t id, object_metadata_t* metadata);
int metafs_metadata_add_tag(metafs_context_t* ctx, object_id_t id, const char* tag);
int metafs_validate_metadata(const core_metadata_t* meta);

/* Name and view operations */
int metafs_object_set_name(metafs_context_t* ctx, object_id_t id, const char* name);
const char* metafs_object_get_name_simple(metafs_context_t* ctx, object_id_t id);
int metafs_object_set_view(metafs_context_t* ctx, object_id_t id, const char* view);
const char* metafs_object_get_view(metafs_context_t* ctx, object_id_t id);
int metafs_object_set_type(metafs_context_t* ctx, object_id_t id, object_type_t type);

/* Extension operations */
int metafs_object_set_extension(metafs_context_t* ctx, object_id_t id, const char* ext);
const char* metafs_object_get_extension(metafs_context_t* ctx, object_id_t id);

/* data */
ssize_t metafs_read(metafs_context_t* fs,
                    object_id_t id,
                    void* buffer,
                    size_t len);

ssize_t metafs_write(metafs_context_t* fs,
                     object_id_t id,
                     const void* buffer,
                     size_t len);

int metafs_object_write_data(metafs_context_t* ctx, object_id_t id,
                              const void* data, size_t size);
int metafs_object_read_data(metafs_context_t* ctx, object_id_t id,
                             void* buffer, size_t size);

/* Views */
int metafs_view_link(metafs_context_t* ctx, const char* view_name, 
                     const char* name, object_id_t id);
int metafs_view_unlink(metafs_context_t* ctx, const char* view_name, 
                       const char* name);
int metafs_view_link_persistent(metafs_context_t* ctx, const char* view_name,
                                 const char* name, object_id_t id);
object_id_t metafs_path_resolve(metafs_context_t* ctx, const char* path);

/* Resolution */
object_id_t metafs_resolve_by_name(metafs_context_t* ctx, const char* name);

/* queries (non-unique by design) */
int metafs_query_by_name(metafs_context_t* fs,
                         const char* name,
                         object_id_t* out,
                         size_t max_results);

/* ============================================================
 * Wrapper Functions (for shell integration)
 * ============================================================ */

int metafs_view_exists(metafs_context_t* ctx, const char* path);
int metafs_view_list(metafs_context_t* ctx, const char* view_path, 
                     metafs_view_entry_t** entries_out);
const char* metafs_object_get_name(metafs_context_t* ctx, object_id_t id, 
                                   const char* view_name);
int metafs_view_create(metafs_context_t* ctx, const char* view_name, 
                       object_type_t filter_type);
int metafs_path_get_view(const char* path, char* view_name, size_t max_len);
int metafs_normalize_path(const char* current_dir, const char* path, 
                          char* normalized, size_t max_len);
object_id_t shell_resolve_path(metafs_context_t* ctx, const char* current_dir, 
                                const char* path);
int shell_path_is_valid(const char* current_dir, const char* path);
void metafs_view_list_free(metafs_view_entry_t* entries);

#endif /* METAFS_H */