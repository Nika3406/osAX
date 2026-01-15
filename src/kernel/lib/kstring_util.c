// src/kernel/kstring_util.c - COMPLETELY FIXED VERSION with qsort
#include "kstring.h"

// Simple sprintf implementation with padding support
int ksprintf(char* str, const char* format, ...) {
    if (!str || !format) return -1;

    __builtin_va_list args;
    __builtin_va_start(args, format);

    char* out = str;

    while (*format) {
        if (*format == '%' && *(format + 1)) {
            format++;

            // Check for padding (e.g., %08x)
            int width = 0;
            char pad_char = ' ';

            if (*format == '0') {
                pad_char = '0';
                format++;
            }

            // Parse width
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }

            switch (*format) {
                case 'd':
                case 'u': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    char buf[16];
                    int i = 0;

                    if (val == 0) {
                        buf[i++] = '0';
                    } else {
                        while (val > 0) {
                            buf[i++] = '0' + (val % 10);
                            val /= 10;
                        }
                    }

                    // Apply padding
                    while (i < width) {
                        buf[i++] = pad_char;
                    }

                    // Write digits in correct order
                    while (i > 0) {
                        *out++ = buf[--i];
                    }
                    break;
                }
                case 'x': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    char hex[] = "0123456789abcdef";
                    char buf[16];
                    int i = 0;

                    if (val == 0) {
                        buf[i++] = '0';
                    } else {
                        while (val > 0) {
                            buf[i++] = hex[val & 0xF];
                            val >>= 4;
                        }
                    }

                    // Apply padding
                    while (i < width) {
                        buf[i++] = pad_char;
                    }

                    // Write hex digits in correct order
                    while (i > 0) {
                        *out++ = buf[--i];
                    }
                    break;
                }
                case 's': {
                    const char* s = __builtin_va_arg(args, const char*);
                    if (s) {
                        while (*s) {
                            *out++ = *s++;
                        }
                    } else {
                        const char* null_str = "(null)";
                        while (*null_str) {
                            *out++ = *null_str++;
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

    return out - str;
}

// Helper function to convert hex char to value
static int hex_char_to_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// Simple sscanf for hex values - ROBUSTLY FIXED
int ksscanf_hex(const char* str, uint32_t* high, uint32_t* low) {
    if (!str || !high || !low) {
        return -1;
    }

    // Initialize to zero
    *high = 0;
    *low = 0;

    // Check minimum length
    int len = 0;
    while (str[len]) len++;

    if (len < 16) {
        return -1;  // Need at least 16 hex digits
    }

    // Parse high part (first 8 hex digits)
    for (int i = 0; i < 8; i++) {
        int val = hex_char_to_value(str[i]);
        if (val < 0) {
            return -1;  // Invalid hex character
        }
        *high = (*high << 4) | val;
    }

    // Parse low part (next 8 hex digits)
    for (int i = 8; i < 16; i++) {
        int val = hex_char_to_value(str[i]);
        if (val < 0) {
            return -1;  // Invalid hex character
        }
        *low = (*low << 4) | val;
    }

    return 0;
}

// Find character in string
char* strchr(const char* str, int ch) {
    if (!str) return NULL;

    while (*str) {
        if (*str == (char)ch) {
            return (char*)str;
        }
        str++;
    }
    return NULL;
}

// Find LAST occurrence of character in string
char* strrchr(const char* str, int ch) {
    if (!str) return NULL;
    
    const char* last = NULL;
    
    // Scan through entire string, remembering last match
    while (*str) {
        if (*str == (char)ch) {
            last = str;
        }
        str++;
    }
    
    // Special case: searching for null terminator
    if (ch == '\0') {
        return (char*)str;
    }
    
    return (char*)last;
}

// String compare
int strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) {
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    }

    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// ===== qsort implementation =====

// Helper: swap bytes between two memory locations
static void swap_bytes(void* a, void* b, size_t size) {
    unsigned char* pa = (unsigned char*)a;
    unsigned char* pb = (unsigned char*)b;
    unsigned char temp;
    
    for (size_t i = 0; i < size; i++) {
        temp = pa[i];
        pa[i] = pb[i];
        pb[i] = temp;
    }
}

// Simple bubble sort implementation (good enough for kernel use with small arrays)
// For better performance, you could implement quicksort later
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*)) {
    if (!base || nmemb <= 1 || size == 0 || !compar) {
        return;
    }
    
    unsigned char* arr = (unsigned char*)base;
    
    // Bubble sort
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - i - 1; j++) {
            void* elem_j = arr + (j * size);
            void* elem_j1 = arr + ((j + 1) * size);
            
            if (compar(elem_j, elem_j1) > 0) {
                swap_bytes(elem_j, elem_j1, size);
            }
        }
    }
}
