#ifndef SERIAL_H
#define SERIAL_H

#include "../core/types.h"

// Serial ports
#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

// Initialize serial port
void serial_init(void);

// Write single character
void serial_putc(char c);

// Write string
void serial_puts(const char* str);

// Printf-like function
void kprintf(const char* format, ...);

// Write hex value
void serial_put_hex(uint32_t val);

// Write decimal value
void serial_put_dec(uint32_t val);

#endif
