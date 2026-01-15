// src/kernel/metafs_wrappers.c - Shell-facing MetaFS API
// This layer completely hides exFAT from the shell
#include "metafs.h"
#include "exfat.h"
#include "serial.h"
#include "kstring.h"
#include "heap.h"
// ===== Check if a MetaFS view exists =====
int metafs_view_exists(metafs_context_t* ctx, const char* path) {
    if (!ctx || !path) return 0;
    
    // Root always exists
    if (strcmp(path, "/") == 0) return 1;
    
    // Strip leading slash
    const char* view_path = path;
    if (view_path[0] == '/') view_path++;
    
    // Empty path after stripping = root
    if (view_path[0] == '\0') return 1;
    
    // Extract view name (everything before first slash)
    char view_name[64];
    const char* slash = strchr(view_path, '/');
    size_t view_len = slash ? (size_t)(slash - view_path) : strlen(view_path);
    
    if (view_len == 0 || view_len >= 64) return 0;
    
    memcpy(view_name, view_path, view_len);
    view_name[view_len] = '\0';
    
    // Check if this view exists in MetaFS
    for (uint32_t i = 0; i < ctx->num_views; i++) {
        if (strcmp(ctx->views[i].name, view_name) == 0) {
            return 1;
        }
    }
    
    return 0;
}

// ===== List objects in a MetaFS view =====
int metafs_view_list(metafs_context_t* ctx, const char* view_path, 
                     metafs_view_entry_t** entries_out) {
    if (!ctx || !view_path || !entries_out) return -1;
    
    kprintf("METAFS: Listing view '%s'\n", view_path);
    
    // Handle root directory - list all views
    const char* path = view_path;
    if (path[0] == '/') path++;
    
    if (path[0] == '\0') {
        // Root - return list of views as entries
        int count = ctx->num_views;
        metafs_view_entry_t* entries = (metafs_view_entry_t*)kmalloc(
            sizeof(metafs_view_entry_t) * count);
        
        for (int i = 0; i < count; i++) {
            // Copy view name
            size_t name_len = strlen(ctx->views[i].name);
            if (name_len >= 255) name_len = 255;
            memcpy(entries[i].name, ctx->views[i].name, name_len);
            entries[i].name[name_len] = '\0';
            
            // These are directories/views, not files
            entries[i].id = OBJECT_ID_NULL;
            entries[i].type = OBJ_TYPE_UNKNOWN;
            entries[i].size = 0;
            entries[i].created = 0;
        }
        
        *entries_out = entries;
        kprintf("METAFS: Listed %d views\n", count);
        return count;
    }
    
    // Extract view name
    char view_name[64];
    const char* slash = strchr(path, '/');
    size_t view_len = slash ? (size_t)(slash - path) : strlen(path);
    
    if (view_len >= 64) {
        kprintf("METAFS: View name too long\n");
        return -1;
    }
    
    memcpy(view_name, path, view_len);
    view_name[view_len] = '\0';
    
    kprintf("METAFS: View name: '%s'\n", view_name);
    
    // Find matching view definition
    view_definition_t* view = NULL;
    for (uint32_t i = 0; i < ctx->num_views; i++) {
        if (strcmp(ctx->views[i].name, view_name) == 0) {
            view = &ctx->views[i];
            break;
        }
    }
    
    if (!view) {
        kprintf("METAFS: View not found\n");
        return -1;
    }
    
    // Scan MetaFS index AND match with view links on disk
    // For each object in the index, check if it has a link in this view
    
    int max_entries = 128;
    metafs_view_entry_t* entries = (metafs_view_entry_t*)kmalloc(
        sizeof(metafs_view_entry_t) * max_entries);
    int count = 0;
    
    kprintf("METAFS: Scanning %d objects in index...\n", ctx->num_objects);
    
    for (uint32_t i = 0; i < ctx->num_objects && count < max_entries; i++) {
        object_id_t id = ctx->index[i].id;
        
        kprintf("METAFS: Checking object %08x%08x...\n", 
                (uint32_t)id.high, (uint32_t)id.low);
        
        // Get metadata
        object_metadata_t meta;
        if (metafs_metadata_get(ctx, id, &meta) != 0) {
            kprintf("METAFS: Failed to get metadata\n");
            continue;
        }
        
        // Check if object matches view filter
        int matches = (view->filter_type == OBJ_TYPE_UNKNOWN) ||
                     (meta.core.type == view->filter_type);
        
        kprintf("METAFS: Object type=%s, filter=%s, matches=%d\n",
                metafs_type_to_string(meta.core.type),
                metafs_type_to_string(view->filter_type),
                matches);
        
        if (matches) {
            // Try to find a link file for this object in the view
            // We need to scan the view directory for any file that points to this ObjectID
            // For now, use a default name based on ObjectID
            
            char link_name[256];
            ksprintf(link_name, "obj_%08x%08x", 
                    (uint32_t)id.high, (uint32_t)id.low);
            
            // Build full path to check
            char link_path[256];
            ksprintf(link_path, "/views/%s/", view_name);
            
            // Try common names: check for link files
            // This is a simplified approach - ideally we'd scan the directory
            
            // For now, just add the object to the list
            // Copy name
            size_t name_len = strlen(link_name);
            if (name_len >= 255) name_len = 255;
            memcpy(entries[count].name, link_name, name_len);
            entries[count].name[name_len] = '\0';
            
            entries[count].id = id;
            entries[count].type = meta.core.type;
            entries[count].size = meta.core.size;
            entries[count].created = meta.core.created;
            count++;
            
            kprintf("METAFS: Added object to list (count=%d)\n", count);
        }
    }
    
    *entries_out = entries;
    kprintf("METAFS: Found %d objects in view\n", count);
    return count;
}

// ===== Get a user-friendly name for an object in a view =====
const char* metafs_object_get_name(metafs_context_t* ctx, object_id_t id, 
                                   const char* view_name) {
    if (!ctx) return NULL;
    
    // For now, return ObjectID-based name
    static char default_name[32];
    ksprintf(default_name, "obj_%08x%08x", 
            (uint32_t)id.high, (uint32_t)id.low);
    
    (void)view_name;  // Unused for now
    return default_name;
}

// ===== Create a new view (directory) =====
int metafs_view_create(metafs_context_t* ctx, const char* view_name, 
                       object_type_t filter_type) {
    if (!ctx || !view_name) return -1;
    
    kprintf("METAFS: Creating view '%s'\n", view_name);
    
    // Check if view already exists
    for (uint32_t i = 0; i < ctx->num_views; i++) {
        if (strcmp(ctx->views[i].name, view_name) == 0) {
            kprintf("METAFS: View already exists\n");
            return -1;
        }
    }
    
    // Check capacity
    if (ctx->num_views >= 64) {
        kprintf("METAFS: Maximum views reached\n");
        return -1;
    }
    
    // Create exFAT directory for storage
    char exfat_path[256];
    ksprintf(exfat_path, "/views/%s", view_name);
    
    if (exfat_mkdir(ctx->volume, exfat_path) < 0) {
        kprintf("METAFS: Failed to create exFAT directory\n");
        return -1;
    }
    
    // Add to MetaFS views
    view_definition_t* view = &ctx->views[ctx->num_views];
    memset(view, 0, sizeof(view_definition_t));
    
    size_t name_len = strlen(view_name);
    if (name_len >= 64) name_len = 63;
    memcpy(view->name, view_name, name_len);
    view->name[name_len] = '\0';
    
    view->type = VIEW_STATIC_DOCUMENTS;  // Default
    view->filter_type = filter_type;
    
    ctx->num_views++;
    
    // Sync to disk
    metafs_sync(ctx);
    
    kprintf("METAFS: View created successfully\n");
    return 0;
}

// ===== Get view name from path =====
int metafs_path_get_view(const char* path, char* view_name, size_t max_len) {
    if (!path || !view_name || max_len == 0) return -1;
    
    const char* p = path;
    if (p[0] == '/') p++;
    
    if (p[0] == '\0') {
        view_name[0] = '\0';
        return 0;  // Root path
    }
    
    const char* slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    
    if (len >= max_len) return -1;
    
    memcpy(view_name, p, len);
    view_name[len] = '\0';
    
    return 0;
}

// Normalize a path, resolving . and .. components
// Returns 0 on success, -1 on error
int metafs_normalize_path(const char* current_dir, const char* path, 
                          char* normalized, size_t max_len) {
    if (!path || !normalized || max_len == 0) return -1;
    
    char temp[256];
    const char* input;
    
    // Determine starting point
    if (path[0] == '/') {
        // Absolute path - use as-is
        input = path;
    } else {
        // Relative path - prepend current directory
        if (strcmp(current_dir, "/") == 0) {
            // At root, relative path is just /<path>
            ksprintf(temp, "/%s", path);
        } else {
            // In a view, relative path is <current_dir>/<path>
            ksprintf(temp, "%s/%s", current_dir, path);
        }
        input = temp;
    }
    
    // Now process the path components
    char components[8][64];  // Max 8 path components
    int comp_count = 0;
    
    const char* p = input;
    if (*p == '/') p++;  // Skip leading slash
    
    while (*p && comp_count < 8) {
        // Skip multiple slashes
        while (*p == '/') p++;
        if (*p == '\0') break;
        
        // Extract component
        int i = 0;
        while (*p && *p != '/' && i < 63) {
            components[comp_count][i++] = *p++;
        }
        components[comp_count][i] = '\0';
        
        // Process special components
        if (strcmp(components[comp_count], ".") == 0) {
            // Current directory - skip
            continue;
        } else if (strcmp(components[comp_count], "..") == 0) {
            // Parent directory
            if (comp_count > 0) {
                // Go back one level (remove last component)
                comp_count--;
            }
            // If comp_count == 0, we're already at root, so .. does nothing
            continue;
        }
        
        comp_count++;
    }
    
    // Build normalized path
    if (comp_count == 0) {
        // Root directory
        normalized[0] = '/';
        normalized[1] = '\0';
        return 0;
    }
    
    // CRITICAL: MetaFS views are FLAT
    // Valid paths: "/" or "/<viewname>"
    // Anything deeper is invalid
    if (comp_count > 2) {
        // Path has nesting like /view/subdir - invalid in MetaFS!
        return -1;
    }
    
    // Build path: /<viewname>
    int pos = 0;
    normalized[pos++] = '/';

    for (int i = 0; i < comp_count && pos < (int)max_len - 1; i++) {
        const char* c = components[i];
        while (*c && pos < (int)max_len - 1) {
            normalized[pos++] = *c++;
        }
        if (i < comp_count - 1 && pos < (int)max_len - 1) {
            normalized[pos++] = '/';
        }
    }
    normalized[pos] = '\0';
    
    return 0;
}

// Wrapper for shell commands - resolves relative paths
object_id_t shell_resolve_path(metafs_context_t* ctx, const char* current_dir, 
                                const char* path) {
    if (!ctx || !path) return OBJECT_ID_NULL;
    
    char normalized[256];
    if (metafs_normalize_path(current_dir, path, normalized, sizeof(normalized)) < 0) {
        // Invalid path
        return OBJECT_ID_NULL;
    }
    
    return metafs_path_resolve(ctx, normalized);
}

// Check if a path is valid (handles ./ and ../)
int shell_path_is_valid(const char* current_dir, const char* path) {
    char normalized[256];
    return metafs_normalize_path(current_dir, path, normalized, sizeof(normalized)) == 0;
}

// ===== Free entries list =====
void metafs_view_list_free(metafs_view_entry_t* entries) {
    if (entries) {
        kfree(entries);
    }
}