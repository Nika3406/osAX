// src/kernel/system/shell.c - Metadata-First Shell (No Directory Paths)
#include "shell.h"
#include "terminal.h"
#include "kstring.h"
#include "heap.h"
#include "logger.h"
#include "metafs.h"
#include "system.h"
#include "executable.h"

static metafs_context_t* shell_metafs;
static char current_view[64] = "";  // Empty = show all objects

// Environment variables
#define MAX_ENV_VARS 50
typedef struct {
    char name[64];
    char value[256];
} env_var_t;

static env_var_t env_vars[MAX_ENV_VARS];
static int env_count = 0;

// Command history
#define MAX_HISTORY 50
static char command_history[MAX_HISTORY][256];
static int history_count = 0;

// ===== FORWARD DECLARATIONS =====
static void cmd_help(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_ls(int argc, char** argv);
static void cmd_cat(int argc, char** argv);
static void cmd_info(int argc, char** argv);
static void cmd_mem(int argc, char** argv);
static void cmd_view(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_export(int argc, char** argv);
static void cmd_env(int argc, char** argv);
static void cmd_history(int argc, char** argv);
static void cmd_mkview(int argc, char** argv);
static void cmd_create(int argc, char** argv);
static void cmd_rm(int argc, char** argv);
static void cmd_tag(int argc, char** argv);
static void cmd_exec(int argc, char** argv);
static void cmd_mark(int argc, char** argv);
static void cmd_file(int argc, char** argv);
static void cmd_sysinfo(int argc, char** argv);
static void cmd_views(int argc, char** argv);
static void cmd_font(int argc, char** argv);
static void cmd_gfx(int argc, char** argv);


// Command structure
typedef struct {
    const char* name;
    const char* description;
    void (*handler)(int argc, char** argv);
} command_t;

static command_t commands[] = {
    {"help", "Display this help message", cmd_help},
    {"clear", "Clear the screen", cmd_clear},
    {"ls", "List objects (optionally filtered by view)", cmd_ls},
    {"cat", "Display object contents by name or ID", cmd_cat},
    {"info", "Show object metadata", cmd_info},
    {"mem", "Show memory statistics", cmd_mem},
    {"view", "Switch current view filter", cmd_view},
    {"echo", "Display text or variables", cmd_echo},
    {"export", "Set environment variable", cmd_export},
    {"env", "Show environment variables", cmd_env},
    {"history", "Show command history", cmd_history},
    {"mkview", "Create a new view", cmd_mkview},
    {"create", "Create a new object", cmd_create},
    {"rm", "Remove object by name or ID", cmd_rm},
    {"tag", "Add tag to object", cmd_tag},
    {"exec", "Execute a program", cmd_exec},
    {"mark", "Mark object type", cmd_mark},
    {"file", "Detect file type", cmd_file},
    {"sysinfo", "Display system information", cmd_sysinfo},
    {"views", "List all views with object counts", cmd_views},
    {"font", "Set framebuffer font scale (1-4)", cmd_font},
    {"gfx", "Show graphics info", cmd_gfx},
    {NULL, NULL, NULL}
};

// ===== Helper functions =====
void shell_set_var(const char* name, const char* value) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            strncpy(env_vars[i].value, value, 255);
            env_vars[i].value[255] = '\0';
            return;
        }
    }
    
    if (env_count < MAX_ENV_VARS) {
        strncpy(env_vars[env_count].name, name, 63);
        env_vars[env_count].name[63] = '\0';
        strncpy(env_vars[env_count].value, value, 255);
        env_vars[env_count].value[255] = '\0';
        env_count++;
    }
}

const char* shell_get_var(const char* name) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            return env_vars[i].value;
        }
    }
    return NULL;
}

void expand_variables(char* input, char* output, int max_len) {
    int in_pos = 0, out_pos = 0;
    
    while (input[in_pos] && out_pos < max_len - 1) {
        if (input[in_pos] == '$') {
            in_pos++;
            char var_name[64];
            int var_pos = 0;
            
            while (input[in_pos] && 
                   (input[in_pos] == '_' || 
                    (input[in_pos] >= 'A' && input[in_pos] <= 'Z') ||
                    (input[in_pos] >= 'a' && input[in_pos] <= 'z') ||
                    (input[in_pos] >= '0' && input[in_pos] <= '9')) &&
                   var_pos < 63) {
                var_name[var_pos++] = input[in_pos++];
            }
            var_name[var_pos] = '\0';
            
            const char* value = shell_get_var(var_name);
            if (value) {
                while (*value && out_pos < max_len - 1) {
                    output[out_pos++] = *value++;
                }
            }
        } else {
            output[out_pos++] = input[in_pos++];
        }
    }
    output[out_pos] = '\0';
}

static int parse_command(char* line, char** argv, int max_args) {
    int argc = 0;
    char* p = line;
    
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        
        argv[argc++] = p;
        
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    
    return argc;
}

// ===== Shell Init =====
void shell_init(metafs_context_t* ctx) {
    shell_metafs = ctx;
    // Command registration happens via the commands array above
}

void shell_prompt(void) {
    const char* user = shell_get_var("USER");
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    if (user) {
        terminal_write(user);
        terminal_write("@osax");
    } else {
        terminal_write("osax");
    }
    
    terminal_setcolor(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    terminal_write(":");
    if (current_view[0] != '\0') {
        terminal_write(current_view);
    } else {
        terminal_write("all");
    }
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("$ ");
}

void shell_execute(char* line) {
    if (!line || line[0] == '\0') return;
    
    // Add to history
    if (history_count < MAX_HISTORY) {
        strncpy(command_history[history_count], line, 255);
        command_history[history_count][255] = '\0';
        history_count++;
    }
    
    char expanded[256];
    expand_variables(line, expanded, 256);
    
    char* argv[16];
    int argc = parse_command(expanded, argv, 16);
    
    if (argc == 0) return;
    
    // Check for shutdown/halt
    if (strcmp(argv[0], "shutdown") == 0 || strcmp(argv[0], "halt") == 0) {
        system_shutdown();
        return;
    }
    
    // Normal command lookup
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].handler(argc, argv);
            return;
        }
    }
    
    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_write("bash: ");
    terminal_write(argv[0]);
    terminal_writeln(": command not found");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

// ===== COMMAND IMPLEMENTATIONS =====

static void cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("OSAX Metadata-First Shell - Available Commands:");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    for (int i = 0; commands[i].name != NULL; i++) {
        terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        terminal_printf("  %-12s", commands[i].name);
        
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        terminal_write(" - ");
        terminal_writeln(commands[i].description);
    }
    
    terminal_writeln("");
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeln("Note: Views are metadata filters, not directories.");
    terminal_writeln("Use 'view <name>' to filter objects by view.");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    terminal_clear();
}

static void cmd_ls(int argc, char** argv) {
    const char* filter_view = NULL;
    
    if (argc > 1) {
        filter_view = argv[1];
    } else if (current_view[0] != '\0') {
        filter_view = current_view;
    }
    
    // Print header
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeln("OBJECTID             NAME            TYPE        VIEW        SIZE    DATE");
    terminal_writeln("-------------------------------------------------------------------------");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    int found_any = 0;
    for (uint32_t i = 0; i < shell_metafs->num_objects; i++) {
        object_metadata_t meta;
        if (metafs_metadata_get(shell_metafs, shell_metafs->index[i].id, &meta) == 0) {
            // Get view for this object
            const char* obj_view = metafs_object_get_view(shell_metafs, shell_metafs->index[i].id);
            
            // Apply filter
            if (filter_view != NULL && obj_view != NULL && strcmp(obj_view, filter_view) != 0) {
                continue;
            }
            
            // Get name
            const char* obj_name = metafs_object_get_name_simple(shell_metafs, shell_metafs->index[i].id);
            if (!obj_name) obj_name = "(unnamed)";
            
            // Get extension (TYPE)
            const char* obj_ext = metafs_object_get_extension(shell_metafs, shell_metafs->index[i].id);
            if (!obj_ext) obj_ext = "none";
            
            // Format ObjectID
            terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
            terminal_printf("%08x%08x", 
                (uint32_t)shell_metafs->index[i].id.high,
                (uint32_t)shell_metafs->index[i].id.low);
            
            // Name
            terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            terminal_printf(" %-15s", obj_name);
            
            // Type (extension)
            terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            terminal_printf(" %-11s", obj_ext);
            
            // View
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            terminal_printf(" %-11s", obj_view ? obj_view : "none");
            
            // Size and date
            terminal_printf(" %7d %d\n", 
                (uint32_t)meta.core.size,
                meta.core.created);
            
            found_any = 1;
        }
    }
    
    if (!found_any) {
        terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        terminal_writeln("(no objects)");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

static void cmd_view(int argc, char** argv) {
    if (argc < 2) {
        // Show current view
        terminal_write("Current view: ");
        terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        terminal_writeln(current_view[0] != '\0' ? current_view : "all");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        // List available views
        terminal_writeln("\nAvailable views:");
        for (uint32_t i = 0; i < shell_metafs->num_views; i++) {
            terminal_printf("  - %s\n", shell_metafs->views[i].name);
        }
        return;
    }
    
    const char* view_name = argv[1];
    
    // Check if "all" or empty
    if (strcmp(view_name, "all") == 0 || view_name[0] == '\0') {
        current_view[0] = '\0';
        shell_set_var("VIEW", "all");
        terminal_writeln("View filter cleared - showing all objects");
        return;
    }
    
    // Check if view exists
    int found = 0;
    for (uint32_t i = 0; i < shell_metafs->num_views; i++) {
        if (strcmp(shell_metafs->views[i].name, view_name) == 0) {
            found = 1;
            break;
        }
    }
    
    if (!found) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_printf("view: '%s' does not exist\n", view_name);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    
    // Set current view
    strncpy(current_view, view_name, 63);
    current_view[63] = '\0';
    shell_set_var("VIEW", current_view);
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_printf("View filter set to: %s\n", current_view);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_writeln("Usage: cat <name|objectid>");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    
    // Try to resolve by name first, then ObjectID
    object_id_t id = metafs_resolve_by_name(shell_metafs, argv[1]);
    
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_printf("cat: '%s': not found\n", argv[1]);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    
    char* buffer = (char*)kmalloc(4096);
    int bytes = metafs_object_read_data(shell_metafs, id, buffer, 4095);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        terminal_write(buffer);
        terminal_write("\n");
    } else {
        terminal_writeln("(empty)");
    }
    
    kfree(buffer);
}

static void cmd_info(int argc, char** argv) {
    if (argc < 2) {
        terminal_writeln("Usage: info <name|objectid>");
        return;
    }
    
    object_id_t id = metafs_resolve_by_name(shell_metafs, argv[1]);
    
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        terminal_printf("info: '%s': not found\n", argv[1]);
        return;
    }
    
    object_metadata_t meta;
    if (metafs_metadata_get(shell_metafs, id, &meta) == 0) {
        terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        terminal_writeln("Object Metadata:");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        terminal_printf("  ObjectID: %08x%08x\n", 
            (uint32_t)id.high, (uint32_t)id.low);
        terminal_printf("  Type: %s\n", metafs_type_to_string(meta.core.type));
        terminal_printf("  Size: %d bytes\n", (uint32_t)meta.core.size);
        terminal_printf("  Created: %d\n", meta.core.created);
        terminal_printf("  Modified: %d\n", meta.core.modified);
        
        const char* name = metafs_object_get_name_simple(shell_metafs, id);
        if (name) {
            terminal_printf("  Name: %s\n", name);
        }
        
        const char* view = metafs_object_get_view(shell_metafs, id);
        if (view) {
            terminal_printf("  View: %s\n", view);
        }
    }
}

static void cmd_mem(int argc, char** argv) {
    (void)argc; (void)argv;
    
    extern void paging_get_stats(uint64_t*, uint64_t*, uint64_t*, uint64_t*);
    extern void heap_get_stats(heap_stats_t*);
    
    uint64_t total_virt, used_virt, total_phys, used_phys; 
    paging_get_stats(&total_virt, &used_virt, &total_phys, &used_phys);
    
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("Memory Statistics:");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    terminal_printf("  Physical: %d MB total, %d MB used, %d MB free\n",
                    (uint32_t)(total_phys / 1024 / 1024),
                    (uint32_t)(used_phys / 1024 / 1024),
                    (uint32_t)((total_phys - used_phys) / 1024 / 1024));
    
    terminal_printf("  Virtual:  %d MB range, %d KB used\n",
                    (uint32_t)(total_virt / 1024 / 1024),
                    (uint32_t)(used_virt / 1024));
    
    heap_stats_t stats;
    heap_get_stats(&stats);
    terminal_printf("  Heap:     %d MB total, %d KB used, %d MB free\n",
                    stats.total_size / 1024 / 1024,
                    stats.used_size / 1024,
                    stats.free_size / 1024 / 1024);
}

static void cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        terminal_write(argv[i]);
        if (i < argc - 1) terminal_write(" ");
    }
    terminal_write("\n");
}

static void cmd_export(int argc, char** argv) {
    if (argc < 2) {
        terminal_writeln("Usage: export VAR=value");
        return;
    }
    
    char* arg = argv[1];
    char* equals = strchr(arg, '=');
    
    if (!equals) {
        terminal_writeln("export: invalid syntax (use VAR=value)");
        return;
    }
    
    *equals = '\0';
    shell_set_var(arg, equals + 1);
    terminal_printf("Exported: %s=%s\n", arg, equals + 1);
}

static void cmd_env(int argc, char** argv) {
    (void)argc; (void)argv;
    
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("Environment Variables:");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    for (int i = 0; i < env_count; i++) {
        terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        terminal_write(env_vars[i].name);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        terminal_write("=");
        terminal_write(env_vars[i].value);
        terminal_write("\n");
    }
}

static void cmd_history(int argc, char** argv) {
    (void)argc; (void)argv;
    
    for (int i = 0; i < history_count; i++) {
        terminal_printf("  %d  %s\n", i + 1, command_history[i]);
    }
}

static void cmd_mkview(int argc, char** argv) {
    if (argc < 2) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_writeln("Usage: mkview <name> [filter_type]");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    
    object_type_t filter = OBJ_TYPE_UNKNOWN;
    if (argc >= 3) {
        if (strcmp(argv[2], "executable") == 0) filter = OBJ_TYPE_EXECUTABLE;
        else if (strcmp(argv[2], "document") == 0) filter = OBJ_TYPE_DOCUMENT;
        else if (strcmp(argv[2], "image") == 0) filter = OBJ_TYPE_IMAGE;
    }
    
    if (metafs_view_create(shell_metafs, argv[1], filter) == 0) {
        terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        terminal_printf("View created: %s\n", argv[1]);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    } else {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_printf("Failed to create view: %s\n", argv[1]);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

// Helper function to split filename into name and type (extension)
static void split_filename(const char* filename, char* name, char* type, size_t max_len) {
    const char* dot = strrchr(filename, '.');
    
    if (!dot || dot == filename) {
        // No extension - store full name, empty type
        strncpy(name, filename, max_len - 1);
        name[max_len - 1] = '\0';
        type[0] = '\0';
        return;
    }
    
    // Copy name part (before dot)
    size_t name_len = dot - filename;
    if (name_len >= max_len) name_len = max_len - 1;
    memcpy(name, filename, name_len);
    name[name_len] = '\0';
    
    // Copy type part (after dot)
    strncpy(type, dot + 1, max_len - 1);
    type[max_len - 1] = '\0';
}

static void cmd_create(int argc, char** argv) {
    if (argc < 2) {
        terminal_writeln("Usage: create <name.ext> [view]");
        return;
    }
    
    char name[256];
    char type[256];
    
    // Split filename into name and extension
    split_filename(argv[1], name, type, sizeof(name));
    
    const char* view = current_view[0] != '\0' ? current_view : NULL;
    if (argc >= 3) view = argv[2];
    
    // Create with inferred object type (for internal classification)
    object_type_t obj_type = OBJ_TYPE_DOCUMENT;  // Default
    
    object_id_t id = metafs_object_create(shell_metafs, obj_type);
    if (!metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        // Store name WITHOUT extension
        metafs_object_set_name(shell_metafs, id, name);
        
        // Store extension as metadata (you'll need to add this field)
        metafs_object_set_extension(shell_metafs, id, type);
        
        if (view) metafs_object_set_view(shell_metafs, id, view);
        
        metafs_object_write_data(shell_metafs, id, "", 0);
        
        terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        terminal_printf("Created: %s (type: %s)\n", name, type[0] ? type : "none");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

static void cmd_rm(int argc, char** argv) {
    if (argc < 2) {
        terminal_writeln("Usage: rm <name|objectid>");
        return;
    }
    
    object_id_t id = metafs_resolve_by_name(shell_metafs, argv[1]);
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        terminal_printf("rm: '%s': not found\n", argv[1]);
        return;
    }
    
    // CHECK: Is this a system-critical object?
    const char* view = metafs_object_get_view(shell_metafs, id);
    if (view && (strcmp(view, "kernel") == 0 || strcmp(view, "boot") == 0)) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_writeln("WARNING: This object is system-critical!");
        terminal_writeln("Deleting it will make the OS unbootable.");
        terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        terminal_printf("Object: %s (view: %s)\n", argv[1], view);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        terminal_write("Type 'yes' to confirm deletion: ");
        
        // TODO: Read confirmation from keyboard
        // For now, just abort
        terminal_writeln("\nDeletion aborted (confirmation not implemented)");
        return;
    }
    
    // Safe to delete
    metafs_object_delete(shell_metafs, id);
    terminal_printf("Removed: %s\n", argv[1]);
}

static void cmd_tag(int argc, char** argv) {
    if (argc < 3) {
        terminal_writeln("Usage: tag <name|objectid> <tag>");
        return;
    }
    
    object_id_t id = metafs_resolve_by_name(shell_metafs, argv[1]);
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        terminal_printf("tag: '%s': not found\n", argv[1]);
        return;
    }
    
    metafs_metadata_add_tag(shell_metafs, id, argv[2]);
    terminal_printf("Tagged '%s' with: %s\n", argv[1], argv[2]);
}

static void cmd_exec(int argc, char** argv) {
    if (argc < 2) {
        terminal_writeln("Usage: exec <name>");
        return;
    }

    object_id_t id = metafs_resolve_by_name(shell_metafs, argv[1]);
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        terminal_printf("exec: '%s': not found\n", argv[1]);
        return;
    }

    /*
     * THIS IS THE ONLY PLACE executable_run_object IS CALLED
     */
    executable_run_object(
        shell_metafs,
        id,
        argc - 1,
        &argv[1]
    );
}

static void cmd_mark(int argc, char** argv) {
    if (argc < 3) {
        terminal_writeln("Usage: mark <name|objectid> <type>");
        return;
    }
    
    object_id_t id = metafs_resolve_by_name(shell_metafs, argv[1]);
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        terminal_printf("mark: '%s': not found\n", argv[1]);
        return;
    }
    
    object_type_t type = OBJ_TYPE_UNKNOWN;
    if (strcmp(argv[2], "executable") == 0) type = OBJ_TYPE_EXECUTABLE;
    else if (strcmp(argv[2], "document") == 0) type = OBJ_TYPE_DOCUMENT;
    else if (strcmp(argv[2], "data") == 0) type = OBJ_TYPE_DATA;
    else if (strcmp(argv[2], "image") == 0) type = OBJ_TYPE_IMAGE;
    
    metafs_object_set_type(shell_metafs, id, type);
    terminal_printf("Marked '%s' as %s\n", argv[1], metafs_type_to_string(type));
}

static void cmd_file(int argc, char** argv) {
    if (argc < 2) {
        terminal_writeln("Usage: file <name|objectid>");
        return;
    }
    
    object_id_t id = metafs_resolve_by_name(shell_metafs, argv[1]);
    if (metafs_object_id_equal(id, OBJECT_ID_NULL)) {
        terminal_printf("file: '%s': not found\n", argv[1]);
        return;
    }
    
    uint8_t buffer[512];
    int bytes = metafs_object_read_data(shell_metafs, id, buffer, sizeof(buffer));
    
    if (bytes <= 0) {
        terminal_writeln("(empty)");
        return;
    }
    
    object_type_t inferred = metafs_infer_type(buffer, bytes);
    terminal_printf("%s: ", argv[1]);
    
    if (inferred == OBJ_TYPE_EXECUTABLE && executable_is_elf(buffer, bytes)) {
        terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        terminal_write("ELF 32-bit LSB executable");
    } else {
        terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        terminal_write(metafs_type_to_string(inferred));
    }
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("\n");
}

// Add this implementation with your other command implementations
static void cmd_views(int argc, char** argv) {
    const char* filter_view = NULL;
    int show_objects = 0;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--objects") == 0) {
            show_objects = 1;
        } else {
            filter_view = argv[i];
        }
    }
    
    // If no filter specified, use current view
    if (!filter_view && current_view[0] != '\0') {
        filter_view = current_view;
    }
    
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("=== Available Views ===");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    if (filter_view) {
        terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        terminal_printf("(Showing objects in view: %s)\n\n", filter_view);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    
    // Count objects per view
    int total_objects = 0;
    
    for (uint32_t v = 0; v < shell_metafs->num_views; v++) {
        const char* view_name = shell_metafs->views[v].name;
        int obj_count = 0;
        
        // Count objects in this view
        for (uint32_t i = 0; i < shell_metafs->num_objects; i++) {
            const char* obj_view = metafs_object_get_view(
                shell_metafs, 
                shell_metafs->index[i].id
            );
            
            if (obj_view && strcmp(obj_view, view_name) == 0) {
                obj_count++;
            }
        }
        
        // Determine if this is a system view
        int is_system = (strcmp(view_name, "kernel") == 0 || 
                        strcmp(view_name, "boot") == 0);
        
        // Show current view indicator
        int is_current = (current_view[0] != '\0' && 
                         strcmp(current_view, view_name) == 0);
        
        // Set color based on view type
        if (is_system) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        } else {
            terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        }
        
        // Print view name with indicator
        terminal_printf("  %s%-12s", is_current ? "* " : "  ", view_name);
        
        terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        terminal_printf(" [%d objects]", obj_count);
        
        if (is_system) {
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            terminal_write(" (SYSTEM)");
        }
        
        terminal_write("\n");
        
        // Show objects if requested and this view matches filter
        if (show_objects && (!filter_view || strcmp(view_name, filter_view) == 0)) {
            terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
            
            for (uint32_t i = 0; i < shell_metafs->num_objects; i++) {
                const char* obj_view = metafs_object_get_view(
                    shell_metafs,
                    shell_metafs->index[i].id
                );
                
                if (obj_view && strcmp(obj_view, view_name) == 0) {
                    const char* obj_name = metafs_object_get_name_simple(
                        shell_metafs,
                        shell_metafs->index[i].id
                    );
                    
                    const char* obj_ext = metafs_object_get_extension(
                        shell_metafs,
                        shell_metafs->index[i].id
                    );
                    
                    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
                    terminal_write("      ↳ ");
                    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    terminal_printf("%s", obj_name ? obj_name : "(unnamed)");
                    
                    if (obj_ext && obj_ext[0] != '\0') {
                        terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                        terminal_printf(".%s", obj_ext);
                    }
                    
                    terminal_write("\n");
                }
            }
            
            if (obj_count > 0) {
                terminal_write("\n");
            }
        }
        
        total_objects += obj_count;
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    
    // Show "all" view
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    int is_all_current = (current_view[0] == '\0');
    terminal_printf("  %s%-12s", is_all_current ? "* " : "  ", "all");
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_printf(" [%d objects total]\n", total_objects);
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("\n");
    
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeln("Usage:");
    terminal_writeln("  views              - List all views with counts");
    terminal_writeln("  views -o           - List all views and their objects");
    terminal_writeln("  views <view> -o    - List objects in specific view");
    terminal_writeln("  * indicates current active view filter");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static int to_int(const char* s) {
    if (!s || !*s) return 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v * sign;
}

static void cmd_font(int argc, char** argv) {
    if (!terminal_is_graphics()) {
        terminal_writeln("font: not in framebuffer graphics mode (still VGA text mode)");
        return;
    }

    if (argc < 2) {
        terminal_printf("font: current scale = %d\n", terminal_get_font_scale());
        terminal_writeln("usage: font <1-4>");
        return;
    }

    int scale = to_int(argv[1]);
    if (scale < 1 || scale > 4) {
        terminal_writeln("font: scale must be 1..4");
        return;
    }

    if (terminal_set_font_scale(scale) == 0) {
        terminal_printf("font: scale set to %d\n", scale);
    } else {
        terminal_writeln("font: failed (need framebuffer mode + valid fb)");
    }
}

static void cmd_gfx(int argc, char** argv) {
    (void)argc; (void)argv;

    int w=0,h=0,pitch=0,bpp=0,cols=0,rows=0;
    terminal_get_gfx_info(&w,&h,&pitch,&bpp,&cols,&rows);

    terminal_printf("gfx: mode=%s\n", terminal_is_graphics() ? "framebuffer" : "vga-text");
    if (terminal_is_graphics()) {
        terminal_printf("gfx: %dx%d pitch=%d bpp=%d scale=%d grid=%dx%d\n",
            w, h, pitch, bpp, terminal_get_font_scale(), cols, rows);
    }
}


static void cmd_sysinfo(int argc, char** argv) {
    (void)argc; (void)argv;
    
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("=== OSAX System Information ===");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    terminal_writeln("Architecture: x86_64");
    terminal_writeln("Storage Layer: exFAT (file storage)");
    terminal_writeln("Metadata Layer: MetaFS (object nervous system)");
    terminal_writeln("");
    
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("System Views (critical - delete = dead OS):");
    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_writeln("  kernel  - Core system objects (objects.db, etc)");
    terminal_writeln("  boot    - Boot-critical executables");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeln("");
    
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("User Views (safe to modify):");
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeln("  apps      - User applications");
    terminal_writeln("  documents - User documents");
    terminal_writeln("  media     - Images, videos, audio");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeln("");
    
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
