// src/kernel/string.c - Ultra-safe version
#include "kstring.h"

// Mark as used to prevent optimization/inlining
void* __attribute__((used, noinline)) memset(void* ptr, int value, size_t num) {
    unsigned char* p = (unsigned char*)ptr;
    unsigned char val = (unsigned char)value;

    // Force byte-by-byte to avoid any SIMD instructions
    while (num--) {
        *p++ = val;
    }

    return ptr;
}

void* __attribute__((used, noinline)) memcpy(void* destination, const void* source, size_t num) {
    unsigned char* dest = (unsigned char*)destination;
    const unsigned char* src = (const unsigned char*)source;

    while (num--) {
        *dest++ = *src++;
    }

    return destination;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

void itoa(uint32_t val, char *out) {
    char buf[16];
    int i = 0;
    if (val == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (val) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    int p = 0;
    while (i) out[p++] = buf[--i];
    out[p] = 0;
}

char* strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    
    if (*needle == '\0') return (char*)haystack;
    
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        
        if (*n == '\0') {
            return (char*)haystack;
        }
        
        haystack++;
    }
    
    return NULL;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    if (!s1 || !s2) {
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    }
    
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    
    if (n == 0) {
        return 0;
    }
    
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strcpy(char* dest, const char* src) {
    if (!dest || !src) {
        return dest;
    }
    
    char* original_dest = dest;
    
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    
    return original_dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src) {
        return dest;
    }
    
    char* original_dest = dest;
    size_t i;
    
    // Copy up to n characters from src to dest
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    // Pad with null bytes if src is shorter than n
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return original_dest;
}