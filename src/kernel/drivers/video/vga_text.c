// src/kernel/drivers/video/vga_text.c
// VGA Text Mode Driver (80x25)
#include "video.h"
#include "io.h"
#include "kstring.h"

#ifndef INT32_MAX
#define INT32_MAX 0x7FFFFFFF
#endif

#define VGA_MEMORY    0xB8000
#define VGA_WIDTH     80
#define VGA_HEIGHT    25
#define VGA_CTRL_PORT 0x3D4
#define VGA_DATA_PORT 0x3D5

static volatile uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;

// ===== Driver Implementation =====

static int vga_text_init(void) {
    // VGA text mode is already set up by BIOS
    // Just verify the buffer is accessible
    vga_buffer = (uint16_t*)VGA_MEMORY;
    return 0;
}

static void vga_text_clear(void) {
    uint16_t blank = 0x0F00 | ' ';  // White on black space
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
}

static void vga_text_scroll(void) {
    // Move all lines up by one
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = 
                vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    // Clear the bottom line
    uint16_t blank = 0x0F00 | ' ';
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
}

// Convert RGB back to VGA color index (best match)
static uint8_t rgb_to_vga(uint32_t rgb) {
    // Extract RGB components
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    
    // VGA color palette (same as in video.c)
    static const uint32_t vga_palette[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    
    // If it's already a VGA index (0-15), return it
    if (rgb <= 15) {
        return (uint8_t)rgb;
    }
    
    // Find closest VGA color by Euclidean distance
    int best_match = 0;
    int best_distance = INT32_MAX;
    
    for (int i = 0; i < 16; i++) {
        uint8_t pr = (vga_palette[i] >> 16) & 0xFF;
        uint8_t pg = (vga_palette[i] >> 8) & 0xFF;
        uint8_t pb = vga_palette[i] & 0xFF;
        
        int dr = (int)r - (int)pr;
        int dg = (int)g - (int)pg;
        int db = (int)b - (int)pb;
        
        int distance = dr*dr + dg*dg + db*db;
        
        if (distance < best_distance) {
            best_distance = distance;
            best_match = i;
        }
    }
    
    return (uint8_t)best_match;
}

static void vga_text_putchar(uint32_t col, uint32_t row, char c, 
                              uint32_t fg, uint32_t bg) {
    if (col >= VGA_WIDTH || row >= VGA_HEIGHT) {
        return;
    }
    
    // Convert RGB colors back to VGA indices
    uint8_t fg_color = rgb_to_vga(fg);
    uint8_t bg_color = rgb_to_vga(bg);
    
    // VGA attribute byte (8 bits): [bg3 bg2 bg1 bg0 fg3 fg2 fg1 fg0]
    // Background is upper 4 bits, foreground is lower 4 bits
    uint8_t attribute = ((bg_color & 0x0F) << 4) | (fg_color & 0x0F);
    
    // VGA text mode entry (16 bits): [attribute byte][character byte]
    uint16_t entry = ((uint16_t)attribute << 8) | (uint8_t)c;
    vga_buffer[row * VGA_WIDTH + col] = entry;
}

static video_info_t vga_text_get_info(void) {
    video_info_t info = {0};
    info.mode = VIDEO_MODE_TEXT;
    info.fb_address = 0;  // Not applicable for text mode
    info.fb_width = 0;
    info.fb_height = 0;
    info.fb_pitch = 0;
    info.fb_bpp = 0;
    info.text_cols = VGA_WIDTH;
    info.text_rows = VGA_HEIGHT;
    return info;
}

// ===== Hardware Cursor Control (Optional) =====

void vga_update_cursor(uint32_t col, uint32_t row) {
    uint16_t pos = row * VGA_WIDTH + col;
    
    // Send low byte
    outb(VGA_CTRL_PORT, 0x0F);
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
    
    // Send high byte
    outb(VGA_CTRL_PORT, 0x0E);
    outb(VGA_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_disable_cursor(void) {
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 0x20);
}

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xC0) | cursor_start);
    
    outb(VGA_CTRL_PORT, 0x0B);
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xE0) | cursor_end);
}

// ===== Driver Structure (exported) =====

video_driver_t vga_text_driver = {
    .name = "VGA Text Mode (80x25)",
    .init = vga_text_init,
    .clear = vga_text_clear,
    .scroll = vga_text_scroll,
    .putpixel = NULL,      // Not supported in text mode
    .fillrect = NULL,      // Not supported in text mode
    .putchar = vga_text_putchar,
    .get_info = vga_text_get_info
};
