#ifndef TERMINAL_H
#define TERMINAL_H

#include "../core/types.h"

// ===== VGA COLORS =====
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15,
};

// ===== CORE TERMINAL API =====
void terminal_init(void);
void terminal_clear(void);
void terminal_setcolor(enum vga_color fg, enum vga_color bg);
void terminal_putchar(char c);
void terminal_write(const char* str);
void terminal_writeln(const char* str);
void terminal_printf(const char* format, ...);

// ===== CURSOR CONTROL =====
void terminal_update_cursor(void);
void terminal_get_cursor(int* x, int* y);
void terminal_set_cursor(int x, int y);

// ===== INPUT =====
int terminal_readline(char* buffer, int max_len);
void terminal_clear_input(void);

// ======================================================
//  GRAPHICS / FRAMEBUFFER EXTENSIONS
//  (Safe to call even if running in VGA text mode)
// ======================================================

// Returns 1 if framebuffer graphics mode is active, 0 if VGA text mode
int terminal_is_graphics(void);

// Font scaling (framebuffer mode only)
// Scale range: 1..4
int terminal_get_font_scale(void);
int terminal_set_font_scale(int scale);

// Query framebuffer + text grid info
// Any pointer may be NULL if you don't care about that value
void terminal_get_gfx_info(
    int* width,
    int* height,
    int* pitch,
    int* bpp,
    int* cols,
    int* rows
);

#endif // TERMINAL_H
