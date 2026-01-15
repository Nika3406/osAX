#ifndef KSTRING_H
#define KSTRING_H

#include "../core/types.h"

void* memset(void* ptr, int value, size_t num);
void* memcpy(void* destination, const void* source, size_t num);
size_t strlen(const char* str);
void itoa(uint32_t val, char *out);

int ksprintf(char* str, const char* format, ...);
int ksscanf_hex(const char* str, uint32_t* high, uint32_t* low);
char* strchr(const char* str, int ch);
char* strrchr(const char* str, int ch);
char* strstr(const char* haystack, const char* needle);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));

#endif