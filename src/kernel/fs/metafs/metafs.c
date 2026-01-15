// src/kernel/metafs.c - Metadata-First Filesystem Implementation (COMPLETE)
#include "metafs.h"
#include "exfat.h"
#include "serial.h"
#include "kstring.h"
#include "heap.h"

// ===== SYSTEM VIEWS (like Windows System32) =====
// These views are CRITICAL for OS function - delete them and the OS is dead

#define SYSTEM_VIEW_KERNEL   "kernel"    // Critical system objects (.db, .state, etc)
#define SYSTEM_VIEW_DATA     "data"      // Raw data storage
#define SYSTEM_VIEW_BOOT     "boot"      // Boot-critical objects
#define SYSTEM_VIEW_CONFIG   "config"    // Configuration objects

// When formatting, create these CRITICAL views
int metafs_format(metafs_context_t* ctx) {
    kprintf("METAFS: Formatting filesystem structure...\n");
    
    // CRITICAL SYSTEM VIEWS (like System32 - delete = dead OS)
    view_definition_t kernel_view;
    memcpy(kernel_view.name, SYSTEM_VIEW_KERNEL, 7);
    kernel_view.type = VIEW_STATIC_APPS;
    kernel_view.filter_type = OBJ_TYPE_DATA;
    ctx->views[ctx->num_views++] = kernel_view;
    
    view_definition_t data_view;
    memcpy(data_view.name, SYSTEM_VIEW_DATA, 5);
    data_view.type = VIEW_STATIC_DOCUMENTS;
    data_view.filter_type = OBJ_TYPE_DATA;
    ctx->views[ctx->num_views++] = data_view;
    
    view_definition_t boot_view;
    memcpy(boot_view.name, SYSTEM_VIEW_BOOT, 5);
    boot_view.type = VIEW_STATIC_APPS;
    boot_view.filter_type = OBJ_TYPE_EXECUTABLE;
    ctx->views[ctx->num_views++] = boot_view;
    
    view_definition_t config_view;
    memcpy(config_view.name, SYSTEM_VIEW_CONFIG, 7);
    config_view.type = VIEW_STATIC_DOCUMENTS;
    config_view.filter_type = OBJ_TYPE_DATA;
    ctx->views[ctx->num_views++] = config_view;
    
    // USER VIEWS (deletable without killing OS)
    view_definition_t apps_view;
    memcpy(apps_view.name, "apps", 5);
    apps_view.type = VIEW_STATIC_APPS;
    apps_view.filter_type = OBJ_TYPE_EXECUTABLE;
    ctx->views[ctx->num_views++] = apps_view;

    view_definition_t docs_view;
    memcpy(docs_view.name, "documents", 10);
    docs_view.type = VIEW_STATIC_DOCUMENTS;
    docs_view.filter_type = OBJ_TYPE_DOCUMENT;
    ctx->views[ctx->num_views++] = docs_view;

    view_definition_t media_view;
    memcpy(media_view.name, "media", 6);
    media_view.type = VIEW_STATIC_MEDIA;
    media_view.filter_type = OBJ_TYPE_IMAGE;
    ctx->views[ctx->num_views++] = media_view;

    kprintf("METAFS: Format complete! Created %d views (%d system, %d user)\n", 
            ctx->num_views, 4, 3);
    return 0;
}

// ===== EXPOSE SYSTEM FILES AS OBJECTS =====
// Convert exFAT files (like objects.db, system.state) into MetaFS objects

// Create a system object and link it to a view
static object_id_t create_system_object(metafs_context_t* ctx, 
                                        const char* name,
                                        const char* extension,
                                        const char* view,
                                        object_type_t type) {
    // Create the object
    object_id_t id = metafs_object_create(ctx, type);
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        return OBJECT_ID_NULL;
    }
    
    // Set metadata
    metafs_object_set_name(ctx, id, name);
    metafs_object_set_extension(ctx, id, extension);
    metafs_object_set_view(ctx, id, view);
    
    kprintf("METAFS: Created system object '%s.%s' in view '%s'\n", 
            name, extension, view);
    
    return id;
}

// Import existing exFAT files as objects (run on first boot)
int metafs_import_system_files(metafs_context_t* ctx) {
    kprintf("METAFS: Importing system files as objects...\n");
    
    // Import objects.db
    object_id_t objects_db = create_system_object(ctx, 
        "objects", "db", SYSTEM_VIEW_KERNEL, OBJ_TYPE_DATA);
    
    // Import system.state
    object_id_t system_state = create_system_object(ctx,
        "system", "state", SYSTEM_VIEW_KERNEL, OBJ_TYPE_DATA);
    
    // These objects now appear when you "ls kernel" or "view kernel; ls"
    
    kprintf("METAFS: Imported 2 system files as objects\n");
    return 0;
}

// ===== DYNAMIC FILE DISCOVERY =====
// Scan exFAT /data/ directory and import any orphaned files as objects

int metafs_scan_and_import_data(metafs_context_t* ctx) {
    kprintf("METAFS: Scanning /data/ for orphaned files...\n");
    
    // TODO: Implement exFAT directory scanning
    // For each file in /data/:
    //   1. Check if ObjectID already exists in index
    //   2. If not, create new object with that ID
    //   3. Auto-detect type from file content
    //   4. Add to "data" view
    
    // Example:
    // Found: /data/0000000000000005
    // → Create Object 0000000000000005, type=auto, view=data
    
    kprintf("METAFS: Scan complete (TODO: implement)\n");
    return 0;
}

// ===== CRC32 Implementation =====
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t metafs_crc32(const void* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;

    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return ~crc;
}

// ===== ObjectID Operations =====
int metafs_object_id_equal(object_id_t a, object_id_t b) {
    return (a.high == b.high) && (a.low == b.low);
}

object_id_t metafs_generate_object_id(metafs_context_t* ctx) {
    ctx->last_object_id++;

    object_id_t id;
    id.high = 0;
    id.low = ctx->last_object_id;
    return id;
}

// ===== Tier 0: Content Inference =====
object_type_t metafs_infer_type(const void* data, size_t size) {
    if (!data || size < 4) {
        return OBJ_TYPE_UNKNOWN;
    }

    const uint8_t* bytes = (const uint8_t*)data;

    // ELF executable
    if (size >= 4 && bytes[0] == 0x7F && bytes[1] == 'E' &&
        bytes[2] == 'L' && bytes[3] == 'F') {
        return OBJ_TYPE_EXECUTABLE;
    }

    // PNG image
    if (size >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' &&
        bytes[2] == 'N' && bytes[3] == 'G') {
        return OBJ_TYPE_IMAGE;
    }

    // JPEG image
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        return OBJ_TYPE_IMAGE;
    }

    // Check for text (UTF-8)
    uint8_t printable_count = 0;
    for (size_t i = 0; i < (size < 256 ? size : 256); i++) {
        if ((bytes[i] >= 32 && bytes[i] <= 126) || bytes[i] == '\n' ||
            bytes[i] == '\r' || bytes[i] == '\t') {
            printable_count++;
        }
    }

    if (printable_count > ((size < 256 ? size : 256) * 9 / 10)) {
        return OBJ_TYPE_DOCUMENT;
    }

    return OBJ_TYPE_DATA;
}

const char* metafs_type_to_string(object_type_t type) {
    switch (type) {
        case OBJ_TYPE_EXECUTABLE: return "executable";
        case OBJ_TYPE_DOCUMENT: return "document";
        case OBJ_TYPE_IMAGE: return "image";
        case OBJ_TYPE_VIDEO: return "video";
        case OBJ_TYPE_AUDIO: return "audio";
        // Remove: case OBJ_TYPE_SCRIPT: return "script";
        case OBJ_TYPE_ARCHIVE: return "archive";
        case OBJ_TYPE_DATA: return "data";
        default: return "unknown";
    }
}

// ===== Initialization =====
int metafs_init(metafs_context_t* ctx, exfat_volume_t* volume) {
    if (!ctx || !volume) return -1;

    kprintf("METAFS: Initializing metadata-first filesystem...\n");

    ctx->volume = volume;
    ctx->num_objects = 0;
    ctx->max_objects = 1024;
    ctx->num_views = 0;
    ctx->last_object_id = 0;

    // Allocate index
    ctx->index = (object_index_entry_t*)kmalloc(sizeof(object_index_entry_t) * ctx->max_objects);
    if (!ctx->index) {
        kprintf("METAFS: Failed to allocate index!\n");
        return -1;
    }

    // CRITICAL: Clear the index after allocation
    memset(ctx->index, 0, sizeof(object_index_entry_t) * ctx->max_objects);

    kprintf("METAFS: Initialized with capacity for %d objects\n", ctx->max_objects);
    return 0;
}

// ===== Persistence Functions =====

// Save index to disk
int metafs_save_index(metafs_context_t* ctx) {
    kprintf("METAFS: Saving index to disk...\n");

    // Create metadata file
    if (exfat_create(ctx->volume, "/.kernel/objects.db") < 0) {
        kprintf("METAFS: Failed to create objects.db\n");
        return -1;
    }

    exfat_file_t file;
    if (exfat_open(ctx->volume, "/.kernel/objects.db", &file) < 0) {
        kprintf("METAFS: Failed to open objects.db\n");
        return -1;
    }

    // Write header
    metadata_db_header_t header;
    header.magic = METADATA_DB_MAGIC;
    header.version = 1;
    header.num_objects = ctx->num_objects;
    header.num_views = ctx->num_views;
    header.last_sync = 0;

    exfat_write(ctx->volume, &file, &header, sizeof(header));

    // Write index entries
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        exfat_write(ctx->volume, &file, &ctx->index[i], sizeof(object_index_entry_t));
    }

    exfat_close(&file);
    kprintf("METAFS: Index saved (%d objects)\n", ctx->num_objects);
    return 0;
}

// Load index from disk
int metafs_load_index(metafs_context_t* ctx) {
    kprintf("METAFS: Loading index from disk...\n");

    exfat_file_t file;
    if (exfat_open(ctx->volume, "/.kernel/objects.db", &file) < 0) {
        kprintf("METAFS: No existing index found\n");
        return -1;
    }

    // Read header
    metadata_db_header_t header;
    if (exfat_read(ctx->volume, &file, &header, sizeof(header)) != sizeof(header)) {
        exfat_close(&file);
        return -1;
    }

    // Validate
    if (header.magic != METADATA_DB_MAGIC) {
        kprintf("METAFS: Invalid index magic!\n");
        exfat_close(&file);
        return -1;
    }

    ctx->num_objects = header.num_objects;
    ctx->num_views = header.num_views;

    // Update last_object_id to avoid collisions
    ctx->last_object_id = 0;

    // Read entries and find highest ObjectID
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        exfat_read(ctx->volume, &file, &ctx->index[i], sizeof(object_index_entry_t));

        if (ctx->index[i].id.low > ctx->last_object_id) {
            ctx->last_object_id = ctx->index[i].id.low;
        }
    }

    exfat_close(&file);
    kprintf("METAFS: Loaded %d objects from index\n", ctx->num_objects);
    return 0;
}

// Complete set of fixed MetaFS functions - Replace in metafs.c

// Helper: Convert ObjectID to filename string manually
static void object_id_to_filename(object_id_t id, char* filename, const char* prefix) {
    char* p = filename;
    const char* s = prefix;

    // Copy prefix
    while (*s) {
        *p++ = *s++;
    }

    // Convert ObjectID to 16 hex digits
    char hex[] = "0123456789abcdef";
    uint32_t high = (uint32_t)id.high;
    uint32_t low = (uint32_t)id.low;

    // High 8 digits
    for (int i = 28; i >= 0; i -= 4) {
        *p++ = hex[(high >> i) & 0xF];
    }

    // Low 8 digits
    for (int i = 28; i >= 0; i -= 4) {
        *p++ = hex[(low >> i) & 0xF];
    }

    *p = '\0';
}

// Store object data - FIXED VERSION
int metafs_object_write_data(metafs_context_t* ctx, object_id_t id,
                              const void* data, size_t size) {
    char filename[64];
    object_id_to_filename(id, filename, "/data/");

    kprintf("METAFS: Writing object data to ");
    kprintf(filename);
    kprintf(" (");
    extern void serial_put_dec(uint32_t);
    serial_put_dec(size);
    kprintf(" bytes)\n");

    if (exfat_create(ctx->volume, filename) < 0) {
        return -1;
    }

    exfat_file_t file;
    if (exfat_open(ctx->volume, filename, &file) < 0) {
        return -1;
    }

    int written = exfat_write(ctx->volume, &file, data, size);
    exfat_close(&file);

    kprintf("METAFS: Wrote ");
    serial_put_dec(written);
    kprintf(" bytes to ");
    kprintf(filename);
    kprintf("\n");

    return written;
}

// Read object data - FIXED VERSION
int metafs_object_read_data(metafs_context_t* ctx, object_id_t id,
                             void* buffer, size_t size) {
    if (!ctx || !buffer) {
        kprintf("METAFS: Invalid parameters to read_data\n");
        return -1;
    }

    char filename[64];
    object_id_to_filename(id, filename, "/data/");

    kprintf("METAFS: Reading object data from ");
    kprintf(filename);
    kprintf("\n");

    exfat_file_t file;
    if (exfat_open(ctx->volume, filename, &file) < 0) {
        kprintf("METAFS: Failed to open ");
        kprintf(filename);
        kprintf("\n");
        return -1;
    }

    int bytes = exfat_read(ctx->volume, &file, buffer, size);
    exfat_close(&file);

    kprintf("METAFS: Read ");
    extern void serial_put_dec(uint32_t);
    serial_put_dec(bytes);
    kprintf(" bytes from ");
    kprintf(filename);
    kprintf("\n");

    return bytes;
}

// Create view link - FIXED VERSION
int metafs_view_link_persistent(metafs_context_t* ctx, const char* view_name,
                                 const char* name, object_id_t id) {
    // Build path manually
    char path[256];
    char* p = path;

    // "/views/"
    *p++ = '/';
    *p++ = 'v'; *p++ = 'i'; *p++ = 'e'; *p++ = 'w'; *p++ = 's';
    *p++ = '/';

    // view_name
    const char* s = view_name;
    while (*s) *p++ = *s++;
    *p++ = '/';

    // name
    s = name;
    while (*s) *p++ = *s++;
    *p = '\0';

    kprintf("METAFS: Creating view link ");
    kprintf(path);
    kprintf("\n");

    // Store ObjectID as hex string (16 characters)
    char id_str[17];
    char hex[] = "0123456789abcdef";
    uint32_t high = (uint32_t)id.high;
    uint32_t low = (uint32_t)id.low;
    int pos = 0;

    // High 8 digits
    for (int i = 28; i >= 0; i -= 4) {
        id_str[pos++] = hex[(high >> i) & 0xF];
    }

    // Low 8 digits
    for (int i = 28; i >= 0; i -= 4) {
        id_str[pos++] = hex[(low >> i) & 0xF];
    }
    id_str[16] = '\0';

    if (exfat_create(ctx->volume, path) < 0) {
        kprintf("METAFS: Failed to create link file\n");
        return -1;
    }

    exfat_file_t file;
    if (exfat_open(ctx->volume, path, &file) < 0) {
        kprintf("METAFS: Failed to open link file\n");
        return -1;
    }

    exfat_write(ctx->volume, &file, id_str, 16);  // Write exactly 16 bytes
    exfat_close(&file);

    kprintf("METAFS: Created view link ");
    kprintf(path);
    kprintf(" -> ObjectID ");
    kprintf(id_str);
    kprintf("\n");

    return 0;
}

// Resolve path to ObjectID - FIXED VERSION
object_id_t metafs_path_resolve(metafs_context_t* ctx, const char* path) {
    if (!ctx || !path) return OBJECT_ID_NULL;

    kprintf("METAFS: Resolving path '");
    kprintf(path);
    kprintf("'...\n");

    // Skip leading /
    if (path[0] == '/') path++;

    // Find first /
    const char* slash = strchr(path, '/');
    if (!slash) {
        kprintf("METAFS: Invalid path format\n");
        return OBJECT_ID_NULL;
    }

    // Extract view name
    char view_name[64];
    size_t view_len = slash - path;
    if (view_len >= 64) {
        kprintf("METAFS: View name too long\n");
        return OBJECT_ID_NULL;
    }

    memcpy(view_name, path, view_len);
    view_name[view_len] = '\0';

    // Extract object name
    const char* object_name = slash + 1;

    // Build link path manually
    char link_path[256];
    char* p = link_path;

    // "/views/"
    *p++ = '/';
    *p++ = 'v'; *p++ = 'i'; *p++ = 'e'; *p++ = 'w'; *p++ = 's';
    *p++ = '/';

    // view_name
    const char* s = view_name;
    while (*s) *p++ = *s++;
    *p++ = '/';

    // object_name
    s = object_name;
    while (*s) *p++ = *s++;
    *p = '\0';

    kprintf("METAFS: Opening link file ");
    kprintf(link_path);
    kprintf("\n");

    exfat_file_t file;
    if (exfat_open(ctx->volume, link_path, &file) < 0) {
        kprintf("METAFS: Link file not found\n");
        return OBJECT_ID_NULL;
    }

    // Read ObjectID string (exactly 16 bytes)
    char id_str[17];
    memset(id_str, 0, sizeof(id_str));

    int bytes = exfat_read(ctx->volume, &file, id_str, 16);
    exfat_close(&file);

    if (bytes != 16) {
        kprintf("METAFS: Failed to read link file (got ");
        extern void serial_put_dec(uint32_t);
        serial_put_dec(bytes);
        kprintf(" bytes)\n");
        return OBJECT_ID_NULL;
    }

    id_str[16] = '\0';  // Ensure null termination

    kprintf("METAFS: Read ObjectID string: '");
    kprintf(id_str);
    kprintf("' (length=");
    serial_put_dec(strlen(id_str));
    kprintf(")\n");

    // Parse ObjectID
    object_id_t id;
    uint32_t high_temp = 0;
    uint32_t low_temp = 0;

    if (ksscanf_hex(id_str, &high_temp, &low_temp) < 0) {
        kprintf("METAFS: Failed to parse ObjectID\n");
        return OBJECT_ID_NULL;
    }

    id.high = high_temp;
    id.low = low_temp;

    // Safe hex printing
    kprintf("METAFS: Parsed ObjectID: high=");
    extern void serial_put_hex(uint32_t);
    serial_put_hex(high_temp);
    kprintf(" low=");
    serial_put_hex(low_temp);
    kprintf("\n");

    kprintf("METAFS: Resolved ");
    kprintf(link_path);
    kprintf(" -> ObjectID\n");

    return id;
}


// Update metafs_mount to load index
int metafs_mount(metafs_context_t* ctx) {
    kprintf("METAFS: Mounting...\n");

    if (metafs_load_index(ctx) == 0) {
        kprintf("METAFS: Loaded existing filesystem\n");
    } else {
        kprintf("METAFS: No existing filesystem, starting fresh\n");
    }

    return 0;
}

// Update metafs_sync to save index
void metafs_sync(metafs_context_t* ctx) {
    kprintf("METAFS: Syncing to disk...\n");
    metafs_save_index(ctx);
    kprintf("METAFS: Sync complete\n");
}

// Fixed metafs_object_create() - Replace in metafs.c
object_id_t metafs_object_create(metafs_context_t* ctx, object_type_t type) {
    if (!ctx) return OBJECT_ID_NULL;

    // Generate unique ObjectID
    object_id_t id = metafs_generate_object_id(ctx);

    kprintf("METAFS: Creating object (type=");
    kprintf(metafs_type_to_string(type));
    kprintf(")...\n");

    // Create core metadata
    core_metadata_t core;
    core.magic = META_MAGIC;
    core.version = 1;
    core.id = id;
    core.type = type;
    core.created = 0;
    core.modified = 0;
    core.size = 0;
    core.flags = 0;

    if (type == OBJ_TYPE_EXECUTABLE) {
        core.flags |= META_FLAG_EXECUTABLE;
    }

    // Calculate checksum (excluding checksum field)
    core.checksum = 0;
    core.checksum = metafs_crc32(&core, offsetof(core_metadata_t, checksum));

    // Add to index
    if (ctx->num_objects >= ctx->max_objects) {
        kprintf("METAFS: Object limit reached!\n");
        return OBJECT_ID_NULL;
    }

    object_index_entry_t* entry = &ctx->index[ctx->num_objects];
    entry->id = id;
    entry->type = type;  // CRITICAL: Store type in index!
    entry->data_offset = 0;
    entry->meta_offset = 0;
    entry->checksum = core.checksum;
    
    ctx->num_objects++;

    kprintf("METAFS: Object created successfully (index entry ");
    extern void serial_put_dec(uint32_t);
    serial_put_dec(ctx->num_objects - 1);
    kprintf(")\n");

    return id;
}


int metafs_object_open(metafs_context_t* ctx, object_id_t id, object_handle_t* handle) {
    if (!ctx || !handle) return -1;

    // Find object in index
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            handle->id = id;
            handle->is_open = 1;

            // Load metadata
            handle->metadata.core.id = id;
            handle->metadata.has_extended = 0;

            kprintf("METAFS: Opened object %08x%08x\n",
                    (uint32_t)id.high, (uint32_t)id.low);
            return 0;
        }
    }

    kprintf("METAFS: Object %08x%08x not found!\n",
            (uint32_t)id.high, (uint32_t)id.low);
    return -1;
}

int metafs_object_close(object_handle_t* handle) {
    if (!handle || !handle->is_open) return -1;

    handle->is_open = 0;
    kprintf("METAFS: Closed object %08x%08x\n",
            (uint32_t)handle->id.high, (uint32_t)handle->id.low);
    return 0;
}

// ===== Metadata Operations =====
int metafs_metadata_get(metafs_context_t* ctx, object_id_t id, object_metadata_t* metadata) {
    if (!ctx || !metadata) return -1;

    // Find in index
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            metadata->core.id = id;
            metadata->core.type = ctx->index[i].type;  // Get type from index!
            metadata->core.size = 0;  // TODO: read from disk
            metadata->core.created = 0;
            metadata->core.modified = 0;
            metadata->has_extended = 0;
            return 0;
        }
    }

    return -1;
}

int metafs_metadata_add_tag(metafs_context_t* ctx, object_id_t id, const char* tag) {
    if (!ctx || !tag) return -1;

    kprintf("METAFS: Adding tag '%s' to object %08x%08x\n",
            tag, (uint32_t)id.high, (uint32_t)id.low);

    // TODO: Implement tag storage
    return 0;
}

// ===== View Operations =====
int metafs_view_link(metafs_context_t* ctx, const char* view_name, const char* name, object_id_t id) {
    // Use persistent version
    return metafs_view_link_persistent(ctx, view_name, name, id);
}

int metafs_view_unlink(metafs_context_t* ctx, const char* view_name, const char* name) {
    if (!ctx || !view_name || !name) return -1;

    char path[256];
    ksprintf(path, "/views/%s/%s", view_name, name);

    kprintf("METAFS: Unlinking %s\n", path);

    // TODO: Implement exfat_delete
    // For now, just log it
    kprintf("METAFS: Unlink not yet implemented\n");

    return 0;
}

// ===== Validation and Recovery =====
int metafs_validate_metadata(const core_metadata_t* meta) {
    if (!meta) return 0;

    if (meta->magic != META_MAGIC) {
        kprintf("METAFS: Invalid metadata magic: %x\n", meta->magic);
        return 0;
    }

    // Verify checksum
    core_metadata_t temp = *meta;
    uint32_t stored_checksum = temp.checksum;
    temp.checksum = 0;
    uint32_t calculated = metafs_crc32(&temp, offsetof(core_metadata_t, checksum));

    if (stored_checksum != calculated) {
        kprintf("METAFS: Checksum mismatch! Stored=%x, Calculated=%x\n",
                stored_checksum, calculated);
        return 0;
    }

    return 1;
}

// Simple name storage (stored in index for now)
int metafs_object_set_name(metafs_context_t* ctx, object_id_t id, const char* name) {
    if (!ctx || !name) return -1;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            strncpy(ctx->index[i].name, name, 63);
            ctx->index[i].name[63] = '\0';
            return 0;
        }
    }
    
    return -1;
}

const char* metafs_object_get_name_simple(metafs_context_t* ctx, object_id_t id) {
    if (!ctx) return NULL;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            if (ctx->index[i].name[0] != '\0') {
                return ctx->index[i].name;
            }
            return NULL;
        }
    }
    
    return NULL;
}

int metafs_object_set_view(metafs_context_t* ctx, object_id_t id, const char* view) {
    if (!ctx || !view) return -1;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            strncpy(ctx->index[i].view, view, 63);
            ctx->index[i].view[63] = '\0';
            return 0;
        }
    }
    
    return -1;
}

const char* metafs_object_get_view(metafs_context_t* ctx, object_id_t id) {
    if (!ctx) return NULL;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            if (ctx->index[i].view[0] != '\0') {
                return ctx->index[i].view;
            }
            return NULL;
        }
    }
    
    return NULL;
}

int metafs_object_set_type(metafs_context_t* ctx, object_id_t id, object_type_t type) {
    if (!ctx) return -1;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            ctx->index[i].type = type;
            return 0;
        }
    }
    
    return -1;
}

// Resolve object by name (searches all objects)
object_id_t metafs_resolve_by_name(metafs_context_t* ctx, const char* name) {
    if (!ctx || !name) return OBJECT_ID_NULL;
    
    // First try to parse as ObjectID (16 hex digits)
    if (strlen(name) == 16) {
        uint32_t high, low;
        if (ksscanf_hex(name, &high, &low) == 0) {
            object_id_t id;
            id.high = high;
            id.low = low;
            return id;
        }
    }
    
    // Search by name
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (ctx->index[i].name[0] != '\0' && 
            strcmp(ctx->index[i].name, name) == 0) {
            return ctx->index[i].id;
        }
    }
    
    return OBJECT_ID_NULL;
}

int metafs_object_delete(metafs_context_t* ctx, object_id_t id) {
    if (!ctx) return -1;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            // Shift remaining objects down
            for (uint32_t j = i; j < ctx->num_objects - 1; j++) {
                ctx->index[j] = ctx->index[j + 1];
            }
            ctx->num_objects--;
            
            // TODO: Delete data file
            
            return 0;
        }
    }
    
    return -1;
}

// ===== Data I/O Functions =====

ssize_t metafs_read(metafs_context_t* fs, object_id_t id, void* buffer, size_t len) {
    if (!fs || !buffer) {
        return -1;
    }
    
    return metafs_object_read_data(fs, id, buffer, len);
}

ssize_t metafs_write(metafs_context_t* fs, object_id_t id, const void* buffer, size_t len) {
    if (!fs || !buffer) {
        return -1;
    }
    
    return metafs_object_write_data(fs, id, buffer, len);
}

int metafs_get_core_meta(metafs_context_t* fs, object_id_t id, metafs_core_meta_t* out) {
    if (!fs || !out) {
        return -1;
    }
    
    // Find in index
    for (uint32_t i = 0; i < fs->num_objects; i++) {
        if (metafs_object_id_equal(fs->index[i].id, id)) {
            out->magic = META_MAGIC;
            out->version = 1;
            out->id = id;
            out->type = fs->index[i].type;
            out->size = 0;  // TODO: read actual size from disk
            out->created = 0;
            out->modified = 0;
            out->flags = (fs->index[i].type == OBJ_TYPE_EXECUTABLE) ? META_FLAG_EXECUTABLE : 0;
            out->checksum = fs->index[i].checksum;
            return 0;
        }
    }
    
    return -1;
}

int metafs_get_ext_meta(metafs_context_t* fs, object_id_t id, metafs_ext_meta_t* out) {
    if (!fs || !out) {
        return -1;
    }
    
    // Find in index
    for (uint32_t i = 0; i < fs->num_objects; i++) {
        if (metafs_object_id_equal(fs->index[i].id, id)) {
            // Copy name and view from index
            strncpy(out->name, fs->index[i].name, 63);
            out->name[63] = '\0';
            
            strncpy(out->view, fs->index[i].view, 63);
            out->view[63] = '\0';
            
            // Tags are not yet implemented
            out->tags[0] = '\0';
            
            return 0;
        }
    }
    
    return -1;
}

int metafs_set_ext_meta(metafs_context_t* fs, object_id_t id, const metafs_ext_meta_t* in) {
    if (!fs || !in) {
        return -1;
    }
    
    // Find in index
    for (uint32_t i = 0; i < fs->num_objects; i++) {
        if (metafs_object_id_equal(fs->index[i].id, id)) {
            // Update name and view in index
            strncpy(fs->index[i].name, in->name, 63);
            fs->index[i].name[63] = '\0';
            
            strncpy(fs->index[i].view, in->view, 63);
            fs->index[i].view[63] = '\0';
            
            // Tags are not yet implemented
            
            return 0;
        }
    }
    
    return -1;
}

int metafs_query_by_name(metafs_context_t* fs, const char* name, object_id_t* out, size_t max_results) {
    if (!fs || !name || !out) {
        return 0;
    }
    
    int count = 0;
    for (uint32_t i = 0; i < fs->num_objects && count < (int)max_results; i++) {
        if (fs->index[i].name[0] != '\0' && strcmp(fs->index[i].name, name) == 0) {
            out[count++] = fs->index[i].id;
        }
    }
    
    return count;
}

int metafs_object_set_extension(metafs_context_t* ctx, object_id_t id, const char* ext) {
    if (!ctx || !ext) return -1;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            strncpy(ctx->index[i].extension, ext, 15);
            ctx->index[i].extension[15] = '\0';
            return 0;
        }
    }
    
    return -1;
}

const char* metafs_object_get_extension(metafs_context_t* ctx, object_id_t id) {
    if (!ctx) return NULL;
    
    for (uint32_t i = 0; i < ctx->num_objects; i++) {
        if (metafs_object_id_equal(ctx->index[i].id, id)) {
            if (ctx->index[i].extension[0] != '\0') {
                return ctx->index[i].extension;
            }
            return NULL;
        }
    }
    
    return NULL;
}