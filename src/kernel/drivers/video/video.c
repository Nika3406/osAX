// src/kernel/drivers/video/video.c
// Video Subsystem - Abstraction Layer
#include "video.h"
#include "kstring.h"
#include "serial.h"

// Forward declarations of driver implementations
extern video_driver_t vga_text_driver;
extern video_driver_t vesa_fb_driver;

// Current state
static video_driver_t* current_driver = NULL;
static video_mode_t current_mode = VIDEO_MODE_TEXT;
static video_info_t current_info = {0};

// ===== Initialization =====

int video_init(video_mode_t preferred_mode) {
    kprintf("VIDEO: Initializing graphics subsystem...\n");
    
    // Try framebuffer mode first (if requested)
    if (preferred_mode == VIDEO_MODE_FRAMEBUFFER) {
        kprintf("VIDEO: Attempting framebuffer mode...\n");
        
        if (vesa_fb_driver.init() == 0) {
            current_driver = &vesa_fb_driver;
            current_mode = VIDEO_MODE_FRAMEBUFFER;
            current_info = current_driver->get_info();
            
            kprintf("VIDEO: Framebuffer mode active\n");
            kprintf("  Driver: %s\n", current_driver->name);
            kprintf("  Resolution: %dx%d\n", 
                    current_info.fb_width, current_info.fb_height);
            kprintf("  Pitch: %d bytes\n", current_info.fb_pitch);
            kprintf("  Text grid: %dx%d\n", 
                    current_info.text_cols, current_info.text_rows);
            return 0;
        }
        
        kprintf("VIDEO: Framebuffer not available, falling back to text mode\n");
    }
    
    // Fall back to VGA text mode (always works)
    kprintf("VIDEO: Using VGA text mode\n");
    
    if (vga_text_driver.init() == 0) {
        current_driver = &vga_text_driver;
        current_mode = VIDEO_MODE_TEXT;
        current_info = current_driver->get_info();
        
        kprintf("VIDEO: Text mode active\n");
        kprintf("  Driver: %s\n", current_driver->name);
        kprintf("  Text grid: %dx%d\n", 
                current_info.text_cols, current_info.text_rows);
        return 0;
    }
    
    kprintf("VIDEO: ERROR - No video driver available!\n");
    return -1;
}

// ===== Query Functions =====

video_mode_t video_get_mode(void) {
    return current_mode;
}

const video_info_t* video_get_info(void) {
    return &current_info;
}

const video_driver_t* video_get_driver(void) {
    return current_driver;
}

// ===== Wrapper Functions =====

void video_clear(void) {
    if (current_driver && current_driver->clear) {
        current_driver->clear();
    }
}

void video_scroll(void) {
    if (current_driver && current_driver->scroll) {
        current_driver->scroll();
    }
}

void video_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (current_driver && current_driver->putpixel) {
        current_driver->putpixel(x, y, color);
    }
    // Silently ignore if not supported (e.g., text mode)
}

void video_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                    uint32_t color) {
    if (current_driver && current_driver->fillrect) {
        current_driver->fillrect(x, y, w, h, color);
    }
    // Silently ignore if not supported (e.g., text mode)
}

void video_putchar(uint32_t col, uint32_t row, char c, 
                   uint32_t fg, uint32_t bg) {
    if (current_driver && current_driver->putchar) {
        current_driver->putchar(col, row, c, fg, bg);
    }
}

// ===== Color Utilities =====


uint32_t vga_to_rgb(uint8_t vga_color) {
    // VGA 16-color palette converted to RGB
    static const uint32_t vga_palette[16] = {
        VGA_RGB_BLACK,         // 0
        VGA_RGB_BLUE,          // 1
        VGA_RGB_GREEN,         // 2
        VGA_RGB_CYAN,          // 3
        VGA_RGB_RED,           // 4
        VGA_RGB_MAGENTA,       // 5
        VGA_RGB_BROWN,         // 6
        VGA_RGB_LIGHT_GREY,    // 7
        VGA_RGB_DARK_GREY,     // 8
        VGA_RGB_LIGHT_BLUE,    // 9
        VGA_RGB_LIGHT_GREEN,   // 10
        VGA_RGB_LIGHT_CYAN,    // 11
        VGA_RGB_LIGHT_RED,     // 12
        VGA_RGB_LIGHT_MAGENTA, // 13
        VGA_RGB_YELLOW,        // 14
        VGA_RGB_WHITE          // 15
    };
    
    return vga_palette[vga_color & 0x0F];
}

void rgb_to_components(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (r) *r = (color >> 16) & 0xFF;
    if (g) *g = (color >> 8) & 0xFF;
    if (b) *b = color & 0xFF;
}

// ===== Debug Functions =====

void video_debug_info(void) {
    kprintf("\n=== Video Subsystem Info ===\n");
    
    if (!current_driver) {
        kprintf("No driver loaded\n");
        return;
    }
    
    kprintf("Driver: %s\n", current_driver->name);
    kprintf("Mode: %s\n", 
            current_mode == VIDEO_MODE_TEXT ? "Text" : "Framebuffer");
    
    if (current_mode == VIDEO_MODE_FRAMEBUFFER) {
        kprintf("Framebuffer:\n");
        kprintf("  Address: 0x%llx\n", current_info.fb_address);
        kprintf("  Resolution: %dx%d\n", 
                current_info.fb_width, current_info.fb_height);
        kprintf("  Pitch: %d bytes\n", current_info.fb_pitch);
        kprintf("  BPP: %d\n", current_info.fb_bpp);
    }
    
    kprintf("Text grid: %dx%d\n", 
            current_info.text_cols, current_info.text_rows);
    kprintf("============================\n\n");
}
