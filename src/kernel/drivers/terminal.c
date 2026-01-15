// src/kernel/terminal.c - FIXED: Proper framebuffer initialization
#include "terminal.h"
#include "kstring.h"
#include "io.h"
#include "font_data.h"

// External variables from entry.asm
extern uint32_t framebuffer_address;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;

// Mode detection
static int is_graphics_mode = 0;

// VGA text mode buffer
static volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;

// Framebuffer state
static uint8_t* framebuffer = NULL;
static int fb_width = 0;
static int fb_height = 0;
static int fb_pitch = 0;

// Font settings for framebuffer
static const int FONT_WIDTH = 8;
static const int FONT_HEIGHT = 16;
static int cols = 0;
static int rows = 0;

// Terminal state
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = 0x07;
static uint32_t current_fg_color = 0xAAAAAA;
static uint32_t current_bg_color = 0x000000;

static char input_buffer[256];
static int input_pos = 0;

#define MAX_HISTORY 10
static char history[MAX_HISTORY][256];
static int history_count = 0;
static int history_pos = 0;

static const uint32_t vga_to_rgb[] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};



// ===== HELPER: Safe memory write =====
static int is_framebuffer_valid(void) {
    return framebuffer != NULL && 
           (uint32_t)framebuffer >= 0xA0000 &&
           fb_width > 0 && fb_width <= 4096 &&
           fb_height > 0 && fb_height <= 4096 &&
           fb_pitch > 0 && fb_pitch <= 16384;
}

// ===== FRAMEBUFFER FUNCTIONS =====
static void draw_char_fb(char c, int x, int y, uint32_t fg, uint32_t bg) {
    if (!is_framebuffer_valid()) return;
    if (c < 32 || c > 126) c = ' ';
    
    const uint8_t* glyph = get_font_glyph(c);
    
    for (int row = 0; row < FONT_HEIGHT; row++) {
        // Bounds check
        if (y + row >= fb_height) break;
        
        uint8_t bits = glyph[row];
        uint8_t* pixel_row = framebuffer + ((y + row) * fb_pitch) + (x * 3);
        
        for (int col = 0; col < FONT_WIDTH; col++) {
            // Bounds check
            if (x + col >= fb_width) break;
            
            uint32_t color = (bits & 0x80) ? fg : bg;
            
            pixel_row[col * 3 + 0] = (color >> 0) & 0xFF;
            pixel_row[col * 3 + 1] = (color >> 8) & 0xFF;
            pixel_row[col * 3 + 2] = (color >> 16) & 0xFF;
            
            bits <<= 1;
        }
    }
}

static void terminal_scroll_fb(void) {
    if (!is_framebuffer_valid()) return;
    
    for (int y = 0; y < (rows - 1) * FONT_HEIGHT; y++) {
        uint8_t* dest = framebuffer + (y * fb_pitch);
        uint8_t* src = framebuffer + ((y + FONT_HEIGHT) * fb_pitch);
        memcpy(dest, src, fb_pitch);
    }
    
    for (int y = (rows - 1) * FONT_HEIGHT; y < rows * FONT_HEIGHT; y++) {
        uint8_t* row = framebuffer + (y * fb_pitch);
        for (int x = 0; x < fb_width; x++) {
            row[x * 3 + 0] = (current_bg_color >> 0) & 0xFF;
            row[x * 3 + 1] = (current_bg_color >> 8) & 0xFF;
            row[x * 3 + 2] = (current_bg_color >> 16) & 0xFF;
        }
    }
    
    cursor_y = rows - 1;
}

static void terminal_clear_fb(void) {
    if (!is_framebuffer_valid()) {
        extern void serial_puts(const char*);
        serial_puts("TERMINAL: Cannot clear - framebuffer invalid!\n");
        return;
    }
    
    // Clear screen to background color
    for (int y = 0; y < fb_height; y++) {
        uint8_t* row = framebuffer + (y * fb_pitch);
        for (int x = 0; x < fb_width; x++) {
            row[x * 3 + 0] = (current_bg_color >> 0) & 0xFF;
            row[x * 3 + 1] = (current_bg_color >> 8) & 0xFF;
            row[x * 3 + 2] = (current_bg_color >> 16) & 0xFF;
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

// ===== VGA TEXT MODE FUNCTIONS =====
static void terminal_scroll_vga(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            (uint16_t)' ' | ((uint16_t)current_color << 8);
    }
    
    cursor_y = VGA_HEIGHT - 1;
}

static void terminal_clear_vga(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)' ' | ((uint16_t)current_color << 8);
    }
    cursor_x = 0;
    cursor_y = 0;
}

// ===== PUBLIC API =====
void terminal_init(void) {
    extern void serial_puts(const char*);
    extern void kprintf(const char*, ...);
    
    serial_puts("TERMINAL: Starting initialization...\n");
    
    // CRITICAL: Validate framebuffer BEFORE using it
    if (framebuffer_address == 0 || 
        framebuffer_address == 0xFFFFFFFF ||
        framebuffer_address == 0xB8000 ||
        framebuffer_address < 0xA0000) {
        
        serial_puts("TERMINAL: Using VGA text mode (no valid framebuffer)\n");
        is_graphics_mode = 0;
        terminal_clear_vga();
    } else {
        // Validate dimensions
        if (framebuffer_width == 0 || framebuffer_width > 4096 ||
            framebuffer_height == 0 || framebuffer_height > 4096 ||
            framebuffer_pitch == 0 || framebuffer_pitch > 16384) {
            
            serial_puts("TERMINAL: Invalid framebuffer dimensions, using text mode\n");
            kprintf("  Width=%d, Height=%d, Pitch=%d\n", 
                    framebuffer_width, framebuffer_height, framebuffer_pitch);
            is_graphics_mode = 0;
            terminal_clear_vga();
        } else {
            // Use identity-mapped framebuffer address
            // After paging is enabled, the framebuffer is identity-mapped
            framebuffer = (uint8_t*)(uintptr_t)framebuffer_address;
            fb_width = framebuffer_width;
            fb_height = framebuffer_height;
            fb_pitch = framebuffer_pitch;
            
            cols = fb_width / FONT_WIDTH;
            rows = fb_height / FONT_HEIGHT;
            
            serial_puts("TERMINAL: Graphics mode initialized\n");
            kprintf("  Framebuffer at 0x%08x (%dx%d, pitch=%d)\n",
                    framebuffer_address, fb_width, fb_height, fb_pitch);
            kprintf("  Text grid: %dx%d\n", cols, rows);
            
            // Verify we can actually write to it
            if (is_framebuffer_valid()) {
                is_graphics_mode = 1;
                terminal_clear_fb();
                serial_puts("TERMINAL: Framebuffer cleared successfully\n");
            } else {
                serial_puts("TERMINAL: Framebuffer validation failed, using text mode\n");
                is_graphics_mode = 0;
                terminal_clear_vga();
            }
        }
    }
    
    cursor_x = 0;
    cursor_y = 0;
    input_pos = 0;
    history_count = 0;
    
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    if (!is_graphics_mode) {
        terminal_update_cursor();
    }
    
    serial_puts("TERMINAL: Initialization complete\n");
}

void terminal_clear(void) {
    if (is_graphics_mode) {
        terminal_clear_fb();
    } else {
        terminal_clear_vga();
    }
}

void terminal_setcolor(enum vga_color fg, enum vga_color bg) {
    current_color = (bg << 4) | fg;
    current_fg_color = vga_to_rgb[fg];
    current_bg_color = vga_to_rgb[bg];
}

void terminal_putchar(char c) {
    if (is_graphics_mode) {
        if (c == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else if (c == '\r') {
            cursor_x = 0;
        } else if (c == '\t') {
            cursor_x = (cursor_x + 8) & ~7;
        } else if (c == '\b') {
            if (cursor_x > 0) {
                cursor_x--;
                draw_char_fb(' ', cursor_x * FONT_WIDTH, cursor_y * FONT_HEIGHT,
                           current_fg_color, current_bg_color);
            }
        } else {
            draw_char_fb(c, cursor_x * FONT_WIDTH, cursor_y * FONT_HEIGHT,
                       current_fg_color, current_bg_color);
            cursor_x++;
        }
        
        if (cursor_x >= cols) {
            cursor_x = 0;
            cursor_y++;
        }
        
        if (cursor_y >= rows) {
            terminal_scroll_fb();
        }
    } else {
        if (c == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else if (c == '\r') {
            cursor_x = 0;
        } else if (c == '\t') {
            cursor_x = (cursor_x + 8) & ~7;
        } else if (c == '\b') {
            if (cursor_x > 0) {
                cursor_x--;
                vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
                    (uint16_t)' ' | ((uint16_t)current_color << 8);
            }
        } else {
            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
                (uint16_t)c | ((uint16_t)current_color << 8);
            cursor_x++;
        }
        
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
        
        if (cursor_y >= VGA_HEIGHT) {
            terminal_scroll_vga();
        }
        
        terminal_update_cursor();
    }
}

void terminal_write(const char* str) {
    while (*str) {
        terminal_putchar(*str++);
    }
}

void terminal_writeln(const char* str) {
    terminal_write(str);
    terminal_putchar('\n');
}

// Helper functions for printf (keep your existing implementation)
typedef struct {
    int width;
    int precision;
    char pad_char;
    int left_align;
    int zero_pad;
} format_spec_t;

static const char* parse_format_spec(const char* format, format_spec_t* spec) {
    spec->width = 0;
    spec->precision = -1;
    spec->pad_char = ' ';
    spec->left_align = 0;
    spec->zero_pad = 0;
    
    while (*format == '-' || *format == '0') {
        if (*format == '-') {
            spec->left_align = 1;
        } else if (*format == '0') {
            spec->zero_pad = 1;
            spec->pad_char = '0';
        }
        format++;
    }
    
    while (*format >= '0' && *format <= '9') {
        spec->width = spec->width * 10 + (*format - '0');
        format++;
    }
    
    if (*format == '.') {
        format++;
        spec->precision = 0;
        while (*format >= '0' && *format <= '9') {
            spec->precision = spec->precision * 10 + (*format - '0');
            format++;
        }
    }
    
    return format;
}

static int format_uint(char* buf, uint32_t val, int base, const char* digits, format_spec_t* spec) {
    char tmp[32];
    int i = 0;
    
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = digits[val % base];
            val /= base;
        }
    }
    
    int total_len = i;
    if (spec->width > total_len) {
        total_len = spec->width;
    }
    
    int out_pos = 0;
    
    if (!spec->left_align) {
        while (out_pos < total_len - i) {
            buf[out_pos++] = spec->pad_char;
        }
    }
    
    while (i > 0) {
        buf[out_pos++] = tmp[--i];
    }
    
    if (spec->left_align) {
        while (out_pos < total_len) {
            buf[out_pos++] = ' ';
        }
    }
    
    return out_pos;
}

static int format_string(char* buf, const char* str, format_spec_t* spec) {
    if (!str) {
        str = "(null)";
    }
    
    int str_len = 0;
    while (str[str_len]) str_len++;
    
    int total_len = str_len;
    if (spec->width > total_len) {
        total_len = spec->width;
    }
    
    int out_pos = 0;
    
    if (!spec->left_align) {
        while (out_pos < total_len - str_len) {
            buf[out_pos++] = ' ';
        }
    }
    
    for (int i = 0; i < str_len; i++) {
        buf[out_pos++] = str[i];
    }
    
    if (spec->left_align) {
        while (out_pos < total_len) {
            buf[out_pos++] = ' ';
        }
    }
    
    return out_pos;
}

void terminal_printf(const char* format, ...) {
    char buffer[512];
    __builtin_va_list args;
    __builtin_va_start(args, format);

    char* out = buffer;
    
    while (*format && (out - buffer) < 510) {
        if (*format == '%' && *(format + 1)) {
            format++;
            
            format_spec_t spec;
            format = parse_format_spec(format, &spec);
            
            char tmp[64];
            int len = 0;
            
            switch (*format) {
                case 'd':
                case 'i': {
                    int32_t val = __builtin_va_arg(args, int32_t);
                    if (val < 0) {
                        *out++ = '-';
                        val = -val;
                    }
                    len = format_uint(tmp, (uint32_t)val, 10, "0123456789", &spec);
                    for (int i = 0; i < len; i++) {
                        *out++ = tmp[i];
                    }
                    break;
                }
                case 'u': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    len = format_uint(tmp, val, 10, "0123456789", &spec);
                    for (int i = 0; i < len; i++) {
                        *out++ = tmp[i];
                    }
                    break;
                }
                case 'x': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    len = format_uint(tmp, val, 16, "0123456789abcdef", &spec);
                    for (int i = 0; i < len; i++) {
                        *out++ = tmp[i];
                    }
                    break;
                }
                case 'X': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    len = format_uint(tmp, val, 16, "0123456789ABCDEF", &spec);
                    for (int i = 0; i < len; i++) {
                        *out++ = tmp[i];
                    }
                    break;
                }
                case 's': {
                    const char* s = __builtin_va_arg(args, const char*);
                    len = format_string(tmp, s, &spec);
                    for (int i = 0; i < len && (out - buffer) < 510; i++) {
                        *out++ = tmp[i];
                    }
                    break;
                }
                case 'c': {
                    *out++ = (char)__builtin_va_arg(args, int);
                    break;
                }
                case '%': {
                    *out++ = '%';
                    break;
                }
                default: {
                    *out++ = '%';
                    *out++ = *format;
                    break;
                }
            }
        } else {
            *out++ = *format;
        }
        format++;
    }
    *out = '\0';

    __builtin_va_end(args);
    terminal_write(buffer);
}

void terminal_update_cursor(void) {
    if (is_graphics_mode) {
        // No hardware cursor in framebuffer mode
        return;
    }
    
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void terminal_get_cursor(int* x, int* y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void terminal_set_cursor(int x, int y) {
    if (is_graphics_mode) {
        if (x >= 0 && x < cols) cursor_x = x;
        if (y >= 0 && y < rows) cursor_y = y;
    } else {
        if (x >= 0 && x < VGA_WIDTH) cursor_x = x;
        if (y >= 0 && y < VGA_HEIGHT) cursor_y = y;
        terminal_update_cursor();
    }
}

int terminal_readline(char* buffer, int max_len) {
    input_pos = 0;
    
    while (1) {
        if (input_pos > 0) {
            buffer[input_pos] = '\0';
            
            if (history_count < MAX_HISTORY) {
                for (int i = 0; i < input_pos; i++) {
                    history[history_count][i] = buffer[i];
                }
                history[history_count][input_pos] = '\0';
                history_count++;
            }
            
            return input_pos;
        }
    }
    
    return 0;
}

void terminal_clear_input(void) {
    input_pos = 0;
    input_buffer[0] = '\0';
}
