// src/kernel/system/system.c - FIXED VERSION with proper initialization
#include "system.h"
#include "terminal.h"
#include "logger.h"
#include "exfat.h"
#include "metafs.h"
#include "heap.h"

#define SYSTEM_STATE_FILE ".kernel.system.state"
#define SYSTEM_MAGIC 0x4F534158  // "OSAX"

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t boot_count;
    uint32_t clean_shutdown;
    uint32_t last_boot_timestamp;
} system_state_t;

static system_state_t current_state;
static exfat_volume_t* sys_volume = NULL;
static metafs_context_t* sys_metafs = NULL;
static int logger_ready = 0;  // NEW: Track logger state

extern int metafs_import_system_files(metafs_context_t* ctx);

int system_check_filesystem(exfat_volume_t* volume) {
    exfat_file_t test_file;
    if (exfat_open(volume, SYSTEM_STATE_FILE, &test_file) == 0) {
        exfat_close(&test_file);
        return 1;
    }
    return 0;
}

int system_load_state(exfat_volume_t* volume) {
    exfat_file_t state_file;
    
    if (exfat_open(volume, SYSTEM_STATE_FILE, &state_file) < 0) {
        current_state.magic = SYSTEM_MAGIC;
        current_state.version = 1;
        current_state.boot_count = 0;
        current_state.clean_shutdown = 0;
        current_state.last_boot_timestamp = 0;
        return -1;
    }
    
    int bytes = exfat_read(volume, &state_file, &current_state, sizeof(system_state_t));
    exfat_close(&state_file);
    
    if (bytes != sizeof(system_state_t) || current_state.magic != SYSTEM_MAGIC) {
        terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        terminal_writeln("Warning: Corrupted system state, resetting");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    return 0;
}

int system_save_state(exfat_volume_t* volume) {
    exfat_file_t state_file;
    
    if (exfat_open(volume, SYSTEM_STATE_FILE, &state_file) < 0) {
        if (exfat_create(volume, SYSTEM_STATE_FILE) < 0) {
            return -1;
        }
        if (exfat_open(volume, SYSTEM_STATE_FILE, &state_file) < 0) {
            return -1;
        }
    }
    
    exfat_seek(&state_file, 0);
    exfat_write(volume, &state_file, &current_state, sizeof(system_state_t));
    exfat_close(&state_file);
    
    return 0;
}

void system_first_boot(exfat_volume_t* volume) {
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("=== First Boot Detected ===");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeln("Initializing filesystem...");
    
    // Format exFAT
    uint32_t sectors = (10 * 1024 * 1024) / 512;
    exfat_format(sectors);
    exfat_mount(volume);
    
    // Initialize MetaFS
    terminal_write("Initializing metadata system...");
    sys_metafs = (metafs_context_t*)kmalloc(sizeof(metafs_context_t));
    metafs_init(sys_metafs, volume);
    metafs_format(sys_metafs);
    terminal_writeln(" done");
    
    // Import system files
    terminal_write("Exposing system files as objects...");
    metafs_import_system_files(sys_metafs);
    terminal_writeln(" done");
    
    // Save MetaFS index
    metafs_sync(sys_metafs);
    
    // Initialize state
    current_state.magic = SYSTEM_MAGIC;
    current_state.version = 1;
    current_state.boot_count = 1;
    current_state.clean_shutdown = 0;
    current_state.last_boot_timestamp = 0;
    
    system_save_state(volume);
    
    // NOW it's safe to initialize logger
    terminal_write("Initializing logger...");
    if (logger_init(volume) == 0) {
        logger_ready = 1;
        log_write(LOG_INFO, "BOOT", "First boot initialization complete");
        terminal_writeln(" done");
    } else {
        terminal_writeln(" failed (non-critical)");
    }
    
    terminal_setcolor(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    terminal_writeln("First boot setup complete!");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeln("");
}

void system_normal_boot(exfat_volume_t* volume) {
    terminal_write("Loading filesystem...");
    
    if (exfat_mount(volume) < 0) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_writeln(" failed!");
        terminal_writeln("Filesystem corrupted. Run recovery.");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    terminal_writeln(" done");
    
    if (system_load_state(volume) < 0) {
        terminal_writeln("Warning: Could not load system state");
    }
    
    // ONLY show warning in terminal, don't log yet
    if (current_state.clean_shutdown == 0) {
        terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        terminal_writeln("Warning: System was not shut down properly");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    
    terminal_write("Loading metadata...");
    sys_metafs = (metafs_context_t*)kmalloc(sizeof(metafs_context_t));
    metafs_init(sys_metafs, volume);
    
    if (metafs_mount(sys_metafs) < 0) {
        terminal_writeln(" failed, using empty index");
        metafs_format(sys_metafs);
        
        terminal_write("Re-importing system files...");
        metafs_import_system_files(sys_metafs);
        terminal_writeln(" done");
    } else {
        terminal_writeln(" done");
    }
    
    // NOW initialize logger
    terminal_write("Initializing logger...");
    if (logger_init(volume) == 0) {
        logger_ready = 1;
        
        // NOW we can log the dirty shutdown warning
        if (current_state.clean_shutdown == 0) {
            log_write(LOG_WARN, "BOOT", "Dirty shutdown detected");
        }
        
        terminal_writeln(" done");
    } else {
        terminal_writeln(" failed (non-critical)");
    }
    
    current_state.boot_count++;
    current_state.clean_shutdown = 0;
    system_save_state(volume);
    
    if (logger_ready) {
        log_printf(LOG_INFO, "BOOT", "Boot #%d successful", current_state.boot_count);
    }
}

metafs_context_t* system_boot(exfat_volume_t* volume) {
    sys_volume = volume;
    logger_ready = 0;  // Start with logger disabled
    
    if (!system_check_filesystem(volume)) {
        system_first_boot(volume);
    } else {
        system_normal_boot(volume);
    }
    
    return sys_metafs;
}

void system_shutdown(void) {
    terminal_writeln("");
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("Shutting down...");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    terminal_write("Syncing filesystem...");
    if (sys_metafs) {
        metafs_sync(sys_metafs);
    }
    terminal_writeln(" done");
    
    if (logger_ready) {
        terminal_write("Flushing log...");
        logger_flush();
        terminal_writeln(" done");
    }
    
    current_state.clean_shutdown = 1;
    system_save_state(sys_volume);
    
    if (logger_ready) {
        log_write(LOG_INFO, "SHUTDOWN", "Clean shutdown complete");
        logger_close();
    }
    
    terminal_setcolor(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    terminal_writeln("System halted. Safe to power off.");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    __asm__ volatile("cli; hlt");
}

void system_get_stats(uint32_t* boot_count, uint32_t* clean_shutdown) {
    if (boot_count) *boot_count = current_state.boot_count;
    if (clean_shutdown) *clean_shutdown = current_state.clean_shutdown;
}

// NEW: Allow main.c to check if logging is available
int system_logger_ready(void) {
    return logger_ready;
}
