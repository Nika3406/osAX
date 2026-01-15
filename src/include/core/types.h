// src/include/types.h
#ifndef TYPES_H
#define TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef unsigned long      size_t;
typedef long               ssize_t;

// Add these pointer-sized integer types
typedef unsigned int       uintptr_t;  // For 32-bit addresses
typedef signed int         intptr_t;

typedef __builtin_va_list  va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

#define NULL ((void*)0)

// offsetof macro - calculates offset of a field in a struct
#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type*)0)->member)
#endif

#endif
