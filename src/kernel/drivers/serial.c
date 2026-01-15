// src/kernel/serial.c - FIXED VERSION
#include "serial.h"
#include "io.h"
#include "kstring.h"

static int serial_received(void) {
    return inb(COM1 + 5) & 1;
}

static int is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);    // Disable interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x03);    // Set divisor to 3 (38400 baud)
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7);    // Enable FIFO, clear with 14-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

void serial_putc(char c) {
    // Add timeout to prevent infinite loop
    int timeout = 100000;
    while (!is_transmit_empty() && timeout-- > 0);
    if (timeout > 0) {
        outb(COM1, c);
    }
}

void serial_puts(const char* str) {
    if (!str) return;

    while (*str) {
        if (*str == '\n') {
            serial_putc('\r');  // Add carriage return for newlines
        }
        serial_putc(*str++);
    }
}

void serial_put_hex(uint32_t val) {
    char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

void serial_put_dec(uint32_t val) {
    if (val == 0) {
        serial_putc('0');
        return;
    }

    char buf[16];
    int i = 0;
    while (val > 0 && i < 15) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

// FIXED: Simple printf implementation with better safety
void kprintf(const char* format, ...) {
    if (!format) return;

    // Use a static buffer on the stack to avoid issues
    __builtin_va_list args;
    __builtin_va_start(args, format);

    while (*format) {
        if (*format == '%' && *(format + 1)) {
            format++;
            switch (*format) {
                case 'd':
                case 'u': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    serial_put_dec(val);
                    break;
                }
                case 'x': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    serial_put_hex(val);
                    break;
                }
                case 's': {
                    const char* str = __builtin_va_arg(args, const char*);
                    if (str) {
                        serial_puts(str);
                    } else {
                        serial_puts("(null)");
                    }
                    break;
                }
                case 'c': {
                    char c = (char)__builtin_va_arg(args, int);
                    serial_putc(c);
                    break;
                }
                case '%': {
                    serial_putc('%');
                    break;
                }
                default:
                    serial_putc('%');
                    serial_putc(*format);
                    break;
            }
        } else {
            serial_putc(*format);
        }
        format++;
    }

    __builtin_va_end(args);
}
