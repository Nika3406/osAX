// src/include/core/types.h - x86_64 VERSION
#ifndef TYPES_H
#define TYPES_H

// Basic integer types
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

// Pointer and size types - CHANGED FOR 64-BIT
typedef unsigned long long size_t;     // Was: unsigned long
typedef signed long long   ssize_t;    // Was: long

// Pointer-sized integer types - CHANGED FOR 64-BIT
typedef unsigned long long uintptr_t;  // Was: unsigned int
typedef signed long long   intptr_t;   // Was: signed int

// Variable argument list support
typedef __builtin_va_list  va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

// NULL pointer
#define NULL ((void*)0)

// Boolean type - Updated for C23 compatibility
#ifndef __cplusplus
// Check if we're using C23 or later where bool is a keyword
#if __STDC_VERSION__ >= 202311L
    // C23+: bool is a keyword, just define true/false
    #define true  1
    #define false 0
#else
    // Pre-C23: Define bool via typedef
    #ifndef bool
    typedef _Bool bool;
    #define true  1
    #define false 0
    #endif
#endif
#endif

// Convenient macros for limits
#define UINT8_MAX  0xFF
#define UINT16_MAX 0xFFFF
#define UINT32_MAX 0xFFFFFFFF
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL

#define INT8_MAX   0x7F
#define INT16_MAX  0x7FFF
#define INT32_MAX  0x7FFFFFFF
#define INT64_MAX  0x7FFFFFFFFFFFFFFFLL

#define INT8_MIN   (-INT8_MAX - 1)
#define INT16_MIN  (-INT16_MAX - 1)
#define INT32_MIN  (-INT32_MAX - 1)
#define INT64_MIN  (-INT64_MAX - 1LL)

// Size type limits
#define SIZE_MAX   UINT64_MAX
#define SSIZE_MAX  INT64_MAX
#define SSIZE_MIN  INT64_MIN

// Alignment macros
#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))
#define ALIGN_UP(addr, align)   (((addr) + (align) - 1) & ~((align) - 1))
#define IS_ALIGNED(addr, align) (((addr) & ((align) - 1)) == 0)

// offsetof macro - calculates offset of a field in a struct
#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type*)0)->member)
#endif

// Packed and aligned attributes
#define PACKED __attribute__((packed))
#define ALIGNED(x) __attribute__((aligned(x)))

#endif // TYPES_H
