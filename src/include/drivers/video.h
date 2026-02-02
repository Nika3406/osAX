// src/include/drivers/video.h
// Video Subsystem Abstraction Layer
// Supports both VGA text mode and framebuffer graphics

#ifndef VIDEO_H
#define VIDEO_H

#include "../core/types.h"

// ===== Video Modes =====

typedef enum {
    VIDEO_MODE_TEXT,         // VGA 80x25 text mode
    VIDEO_MODE_FRAMEBUFFER   // Linear framebuffer (VESA/GOP)
} video_mode_t;

// ===== Video Information =====

typedef struct {
    video_mode_t mode;
    
    // Framebuffer info (only valid if mode == VIDEO_MODE_FRAMEBUFFER)
    uint64_t fb_address;    // Physical address of framebuffer
    uint32_t fb_width;      // Width in pixels
    uint32_t fb_height;     // Height in pixels
    uint32_t fb_pitch;      // Bytes per scanline
    uint32_t fb_bpp;        // Bits per pixel (usually 32)
    
    // Text grid dimensions (valid for both modes)
    uint32_t text_cols;     // Columns (characters)
    uint32_t text_rows;     // Rows (characters)
} video_info_t;

// ===== Video Driver Interface =====

typedef struct video_driver {
    const char* name;
    
    // Initialization
    int (*init)(void);
    
    // Screen operations
    void (*clear)(void);
    void (*scroll)(void);
    
    // Pixel operations (NULL for text mode)
    void (*putpixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fillrect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    
    // Character operations
    void (*putchar)(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg);
    
    // Information
    video_info_t (*get_info)(void);
} video_driver_t;

// ===== Global Video API =====

// Initialize video subsystem
// preferred_mode: VIDEO_MODE_TEXT or VIDEO_MODE_FRAMEBUFFER
// Returns 0 on success, -1 on failure
int video_init(video_mode_t preferred_mode);

// Get current video mode
video_mode_t video_get_mode(void);

// Get video information
const video_info_t* video_get_info(void);

// Get current driver (for advanced usage)
const video_driver_t* video_get_driver(void);

// ===== Convenience Wrappers =====

// Screen operations
void video_clear(void);
void video_scroll(void);

// Pixel operations (no-op in text mode)
void video_putpixel(uint32_t x, uint32_t y, uint32_t color);
void video_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

// Character operations
void video_putchar(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg);

// ===== Color Utilities =====

// Create RGB color from components
static inline uint32_t video_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Convert VGA color (0-15) to RGB
uint32_t vga_to_rgb(uint8_t vga_color);

// Extract RGB components from color
void rgb_to_components(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b);

// ===== Standard VGA Color Palette (RGB) =====

#define VGA_RGB_BLACK         0x000000
#define VGA_RGB_BLUE          0x0000AA
#define VGA_RGB_GREEN         0x00AA00
#define VGA_RGB_CYAN          0x00AAAA
#define VGA_RGB_RED           0xAA0000
#define VGA_RGB_MAGENTA       0xAA00AA
#define VGA_RGB_BROWN         0xAA5500
#define VGA_RGB_LIGHT_GREY    0xAAAAAA
#define VGA_RGB_DARK_GREY     0x555555
#define VGA_RGB_LIGHT_BLUE    0x5555FF
#define VGA_RGB_LIGHT_GREEN   0x55FF55
#define VGA_RGB_LIGHT_CYAN    0x55FFFF
#define VGA_RGB_LIGHT_RED     0xFF5555
#define VGA_RGB_LIGHT_MAGENTA 0xFF55FF
#define VGA_RGB_YELLOW        0xFFFF55
#define VGA_RGB_WHITE         0xFFFFFF

// ===== Debug =====

void video_debug_info(void);

#endif // VIDEO_H
