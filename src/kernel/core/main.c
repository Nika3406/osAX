// src/kernel/core/main.c - FIXED VERSION
#include "terminal.h"
#include "logger.h"
#include "system.h"
#include "shell.h"
#include "keyboard.h"
#include "heap.h"
#include "exfat.h"
#include "metafs.h"
#include "io.h"
#include "kstring.h"

extern void pic_init(void);
extern int system_logger_ready(void);  // NEW

void boot_splash(void) {
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeln("  ___  ____    _    __  __");
    terminal_writeln(" / _ \\/ ___|  / \\   \\ \\/ /");
    terminal_writeln("| | | \\___ \\ / _ \\   \\  / ");
    terminal_writeln("| |_| |___) / ___ \\  /  \\ ");
    terminal_writeln(" \\___/|____/_/   \\_\\/_/\\_\\");
    terminal_writeln("");
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("Objects are truth. Paths are views.");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeln("========================================");
    terminal_writeln("");
}

void os_main(void) {
    terminal_init();
    terminal_writeln("Terminal initialized");
    
    boot_splash();
    
    terminal_write("Starting OSAX");
    
    __asm__ volatile("cli");
    
    pic_init();
    terminal_write(" [PIC]");
    
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    terminal_write(" [Masked]");
    
    keyboard_init();
    terminal_write(" [KB]");
    
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    
    exfat_volume_t* volume = (exfat_volume_t*)kmalloc(sizeof(exfat_volume_t));
    terminal_write(".");
    
    // System boot handles logger initialization internally now
    metafs_context_t* metafs = system_boot(volume);
    terminal_write(".");
    terminal_writeln(" Ready!");
    terminal_writeln("");
    
    // Show system info
    extern void paging_get_stats(uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    uint32_t total_virt, used_virt, total_phys, used_phys;
    paging_get_stats(&total_virt, &used_virt, &total_phys, &used_phys);
    
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_printf("Memory: %d MB free | ", (total_phys - used_phys) / 1024 / 1024);
    terminal_printf("Filesystem: 10 MB | ");
    
    uint32_t boot_count;
    system_get_stats(&boot_count, NULL);
    terminal_printf("Boot #%d\n", boot_count);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeln("");
    
    shell_init(metafs);
    terminal_writeln("");
    
    // Only log if logger is ready
    if (system_logger_ready()) {
        log_write(LOG_INFO, "SHELL", "Interactive shell started");
    }
    
    char line[256];
    
    terminal_writeln("Type 'help' for commands, or 'shutdown' to exit:");
    
    while (1) {
        shell_prompt();
        
        int len = keyboard_readline(line, sizeof(line));
        
        if (len > 0) {
            if (strcmp(line, "shutdown") == 0 || strcmp(line, "halt") == 0) {
                system_shutdown();
                break;
            }
            
            shell_execute(line);
        }
    }
    
    for(;;) __asm__ volatile("hlt");
}