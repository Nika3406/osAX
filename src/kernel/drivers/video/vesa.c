// src/kernel/drivers/video/vesa.c
// VESA/VBE Framebuffer Driver
#include "video.h"
#include "font_data.h"
#include "paging.h"
#include "kstring.h"
#include "serial.h"

// Framebuffer info from bootloader (defined in entry.asm)
extern uint64_t framebuffer_address;
extern uint64_t framebuffer_width;
extern uint64_t framebuffer_height;
extern uint64_t framebuffer_pitch;

static uint8_t* fb_ptr = NULL;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_bpp = 32;  // Assume 32 bits per pixel (BGRA)

// Character cell dimensions
#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

// ===== Driver Implementation =====

static int vesa_init(void) {
    kprintf("VESA: Checking for framebuffer...\n");
    
    // Check if framebuffer was provided by bootloader
    if (framebuffer_address == 0 || 
        framebuffer_address == 0xB8000 ||  // VGA text mode address
        framebuffer_address == 0xFFFFFFFFFFFFFFFFULL) {
        kprintf("VESA: No framebuffer available (addr=0x%llx)\n", framebuffer_address);
        return -1;
    }
    
    // Validate framebuffer parameters
    if (framebuffer_width == 0 || framebuffer_height == 0) {
        kprintf("VESA: Invalid framebuffer dimensions (%lldx%lld)\n",
                framebuffer_width, framebuffer_height);
        return -1;
    }
    
    fb_ptr = (uint8_t*)framebuffer_address;
    fb_width = (uint32_t)framebuffer_width;
    fb_height = (uint32_t)framebuffer_height;
    fb_pitch = (uint32_t)framebuffer_pitch;
    
    kprintf("VESA: Framebuffer initialized\n");
    kprintf("  Address: 0x%llx\n", framebuffer_address);
    kprintf("  Resolution: %dx%d\n", fb_width, fb_height);
    kprintf("  Pitch: %d bytes\n", fb_pitch);
    kprintf("  BPP: %d\n", fb_bpp);
    kprintf("  Text grid: %dx%d chars\n", 
            fb_width / CHAR_WIDTH, fb_height / CHAR_HEIGHT);
    
    return 0;
}

static void vesa_clear(void) {
    // Clear to black
    memset(fb_ptr, 0, fb_pitch * fb_height);
}

static void vesa_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) {
        return;
    }
    
    // Assume 32-bit BGRA format
    uint32_t* pixel = (uint32_t*)(fb_ptr + y * fb_pitch + x * 4);
    *pixel = color;
}

static void vesa_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                           uint32_t color) {
    for (uint32_t py = y; py < y + h && py < fb_height; py++) {
        for (uint32_t px = x; px < x + w && px < fb_width; px++) {
            vesa_putpixel(px, py, color);
        }
    }
}

static void vesa_putchar(uint32_t col, uint32_t row, char c, 
                         uint32_t fg, uint32_t bg) {
    // Get font glyph for this character
    const uint8_t* glyph = get_font_glyph(c);
    
    // Calculate pixel position (top-left of character cell)
    uint32_t pixel_x = col * CHAR_WIDTH;
    uint32_t pixel_y = row * CHAR_HEIGHT;
    
    // Check bounds
    if (pixel_x + CHAR_WIDTH > fb_width || 
        pixel_y + CHAR_HEIGHT > fb_height) {
        return;
    }
    
    // Render the character using the 8x16 font
    for (int glyph_row = 0; glyph_row < CHAR_HEIGHT; glyph_row++) {
        uint8_t bits = glyph[glyph_row];
        
        for (int glyph_col = 0; glyph_col < CHAR_WIDTH; glyph_col++) {
            // Check if this pixel should be foreground or background
            int bit_set = (bits & (1 << (7 - glyph_col))) != 0;
            uint32_t pixel_color = bit_set ? fg : bg;
            
            vesa_putpixel(pixel_x + glyph_col, 
                         pixel_y + glyph_row, 
                         pixel_color);
        }
    }
}

static void vesa_scroll(void) {
    // Scroll up by one character height (16 pixels)
    uint32_t scroll_bytes = CHAR_HEIGHT * fb_pitch;
    
    // Move framebuffer content up
    memcpy(fb_ptr, 
           fb_ptr + scroll_bytes,
           fb_pitch * (fb_height - CHAR_HEIGHT));
    
    // Clear the bottom character row (set to black)
    memset(fb_ptr + fb_pitch * (fb_height - CHAR_HEIGHT),
           0,
           scroll_bytes);
}

static video_info_t vesa_get_info(void) {
    video_info_t info = {0};
    info.mode = VIDEO_MODE_FRAMEBUFFER;
    info.fb_address = framebuffer_address;
    info.fb_width = fb_width;
    info.fb_height = fb_height;
    info.fb_pitch = fb_pitch;
    info.fb_bpp = fb_bpp;
    info.text_cols = fb_width / CHAR_WIDTH;
    info.text_rows = fb_height / CHAR_HEIGHT;
    return info;
}

// ===== Advanced Drawing Functions (Optional) =====

void vesa_draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                             uint32_t color) {
    // Top edge
    vesa_fillrect(x, y, w, 1, color);
    
    // Bottom edge
    vesa_fillrect(x, y + h - 1, w, 1, color);
    
    // Left edge
    vesa_fillrect(x, y, 1, h, color);
    
    // Right edge
    vesa_fillrect(x + w - 1, y, 1, h, color);
}

void vesa_draw_horizontal_line(uint32_t x, uint32_t y, uint32_t length, 
                                uint32_t color) {
    vesa_fillrect(x, y, length, 1, color);
}

void vesa_draw_vertical_line(uint32_t x, uint32_t y, uint32_t length, 
                              uint32_t color) {
    vesa_fillrect(x, y, 1, length, color);
}

// ===== Driver Structure (exported) =====

video_driver_t vesa_fb_driver = {
    .name = "VESA Framebuffer",
    .init = vesa_init,
    .clear = vesa_clear,
    .scroll = vesa_scroll,
    .putpixel = vesa_putpixel,
    .fillrect = vesa_fillrect,
    .putchar = vesa_putchar,
    .get_info = vesa_get_info
};
