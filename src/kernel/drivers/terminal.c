// src/kernel/drivers/terminal.c
// Terminal Driver - Uses Video Abstraction Layer
// API remains unchanged - works in both text and framebuffer modes

#include "terminal.h"
#include "video.h"
#include "keyboard.h"
#include "io.h"
#include "kstring.h"

// Terminal state
static int cursor_x = 0;
static int cursor_y = 0;
static enum vga_color current_fg = VGA_COLOR_LIGHT_GREY;
static enum vga_color current_bg = VGA_COLOR_BLACK;
static int font_scale = 1;  // For future font scaling support

// ===== Core Terminal API =====

void terminal_init(void) {
    // Initialize video subsystem (tries framebuffer first, falls back to text)
    video_init(VIDEO_MODE_FRAMEBUFFER);
    
    cursor_x = 0;
    cursor_y = 0;
    current_fg = VGA_COLOR_LIGHT_GREY;
    current_bg = VGA_COLOR_BLACK;
    
    terminal_clear();
}

void terminal_clear(void) {
    video_clear();
    cursor_x = 0;
    cursor_y = 0;
}

void terminal_setcolor(enum vga_color fg, enum vga_color bg) {
    current_fg = fg;
    current_bg = bg;
}

void terminal_putchar(char c) {
    const video_info_t* info = video_get_info();
    
    // Handle special characters
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        // Backspace
        if (cursor_x > 0) {
            cursor_x--;
            // Erase character at cursor position
            uint32_t fg_rgb = vga_to_rgb(current_fg);
            uint32_t bg_rgb = vga_to_rgb(current_bg);
            video_putchar(cursor_x, cursor_y, ' ', fg_rgb, bg_rgb);
        }
    } else if (c == '\t') {
        // Tab - move to next 8-character boundary
        cursor_x = (cursor_x + 8) & ~7;
        if (cursor_x >= (int)info->text_cols) {
            cursor_x = 0;
            cursor_y++;
        }
    } else {
        // Normal character
        uint32_t fg_rgb = vga_to_rgb(current_fg);
        uint32_t bg_rgb = vga_to_rgb(current_bg);
        
        video_putchar(cursor_x, cursor_y, c, fg_rgb, bg_rgb);
        cursor_x++;
        
        // Wrap to next line if needed
        if (cursor_x >= (int)info->text_cols) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    
    // Scroll if we've gone past the bottom
    while (cursor_y >= (int)info->text_rows) {
        video_scroll();
        cursor_y = info->text_rows - 1;
    }
    
    // Update hardware cursor (VGA text mode only)
    terminal_update_cursor();
}

void terminal_write(const char* str) {
    if (!str) return;
    
    while (*str) {
        terminal_putchar(*str);
        str++;
    }
}

void terminal_writeln(const char* str) {
    terminal_write(str);
    terminal_putchar('\n');
}

void terminal_printf(const char* format, ...) {
    if (!format) return;
    
    char buffer[1024];
    
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    char* out = buffer;
    const char* fmt = format;
    
    while (*fmt && out < buffer + 1023) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            
            // Handle padding and alignment (e.g., %08x, %-12s)
            int width = 0;
            char pad_char = ' ';
            int left_align = 0;
            
            // Check for left alignment flag
            if (*fmt == '-') {
                left_align = 1;
                fmt++;
            }
            
            // Check for zero padding
            if (*fmt == '0' && !left_align) {
                pad_char = '0';
                fmt++;
            }
            
            // Parse width
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            switch (*fmt) {
                case 'd':
                case 'u': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    char tmp[16];
                    int i = 0;
                    
                    if (val == 0) {
                        tmp[i++] = '0';
                    } else {
                        while (val > 0) {
                            tmp[i++] = '0' + (val % 10);
                            val /= 10;
                        }
                    }
                    
                    // Apply padding
                    while (i < width && out < buffer + 1023) {
                        *out++ = pad_char;
                    }
                    
                    // Write digits in reverse
                    while (i > 0 && out < buffer + 1023) {
                        *out++ = tmp[--i];
                    }
                    break;
                }
                
                case 'x':
                case 'X': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    const char* hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    char tmp[16];
                    int i = 0;
                    
                    if (val == 0) {
                        tmp[i++] = '0';
                    } else {
                        while (val > 0) {
                            tmp[i++] = hex[val & 0xF];
                            val >>= 4;
                        }
                    }
                    
                    // Apply padding
                    while (i < width && out < buffer + 1023) {
                        *out++ = pad_char;
                    }
                    
                    // Write hex digits in reverse
                    while (i > 0 && out < buffer + 1023) {
                        *out++ = tmp[--i];
                    }
                    break;
                }
                
                case 's': {
                    const char* s = __builtin_va_arg(args, const char*);
                    if (!s) {
                        s = "(null)";
                    }
                    
                    // Calculate string length
                    int len = 0;
                    const char* temp = s;
                    while (*temp++) len++;
                    
                    // Apply padding
                    if (left_align) {
                        // Left-aligned: string first, then padding
                        while (*s && out < buffer + 1023) {
                            *out++ = *s++;
                        }
                        while (len < width && out < buffer + 1023) {
                            *out++ = ' ';
                            len++;
                        }
                    } else {
                        // Right-aligned: padding first, then string
                        while (len < width && out < buffer + 1023) {
                            *out++ = ' ';
                            len++;
                        }
                        while (*s && out < buffer + 1023) {
                            *out++ = *s++;
                        }
                    }
                    break;
                }
                
                case 'c': {
                    char c = (char)__builtin_va_arg(args, int);
                    *out++ = c;
                    break;
                }
                
                case '%': {
                    *out++ = '%';
                    break;
                }
                
                default:
                    *out++ = '%';
                    *out++ = *fmt;
                    break;
            }
        } else {
            *out++ = *fmt;
        }
        fmt++;
    }
    
    *out = '\0';
    
    __builtin_va_end(args);
    
    terminal_write(buffer);
}

// ===== Cursor Control =====

void terminal_update_cursor(void) {
    // Update hardware cursor (VGA text mode only)
    if (video_get_mode() == VIDEO_MODE_TEXT) {
        uint16_t pos = cursor_y * 80 + cursor_x;  // 80 columns in VGA
        
        // Send cursor position to VGA controller
        // Register 14: Cursor Location High
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
        
        // Register 15: Cursor Location Low
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
    }
    // For framebuffer mode, we would draw a software cursor
}

void terminal_get_cursor(int* x, int* y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void terminal_set_cursor(int x, int y) {
    const video_info_t* info = video_get_info();
    
    if (x >= 0 && x < (int)info->text_cols) {
        cursor_x = x;
    }
    
    if (y >= 0 && y < (int)info->text_rows) {
        cursor_y = y;
    }
}

// ===== Input =====

int terminal_readline(char* buffer, int max_len) {
    if (!buffer || max_len <= 0) {
        return -1;
    }
    
    int pos = 0;
    buffer[0] = '\0';
    
    while (1) {
        char c = keyboard_getchar();
        
        if (c == '\n' || c == '\r') {
            // Enter pressed
            buffer[pos] = '\0';
            terminal_putchar('\n');
            return pos;
        } else if (c == '\b') {
            // Backspace
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                terminal_putchar('\b');
            }
        } else if (c >= 32 && c < 127) {
            // Printable character
            if (pos < max_len - 1) {
                buffer[pos++] = c;
                buffer[pos] = '\0';
                terminal_putchar(c);
            }
        }
        // Ignore other characters
    }
}

void terminal_clear_input(void) {
    // Clear the current input line
    // Move cursor to beginning and clear to end of line
    cursor_x = 0;
    // Could implement clearing current line here
}

// ===== Graphics/Framebuffer Extensions =====

int terminal_is_graphics(void) {
    return video_get_mode() == VIDEO_MODE_FRAMEBUFFER;
}

int terminal_get_font_scale(void) {
    return font_scale;
}

int terminal_set_font_scale(int scale) {
    if (scale < 1 || scale > 4) {
        return -1;  // Invalid scale
    }
    
    font_scale = scale;
    // TODO: Implement font scaling in framebuffer mode
    // For now, just store the value
    
    return 0;
}

void terminal_get_gfx_info(int* width, int* height, int* pitch, int* bpp,
                           int* cols, int* rows) {
    const video_info_t* info = video_get_info();
    
    if (width)  *width = info->fb_width;
    if (height) *height = info->fb_height;
    if (pitch)  *pitch = info->fb_pitch;
    if (bpp)    *bpp = info->fb_bpp;
    if (cols)   *cols = info->text_cols;
    if (rows)   *rows = info->text_rows;
}
