// src/kernel/logger.c - File-based logging system
#include "logger.h"
#include "exfat.h"
#include "kstring.h"
#include "heap.h"

static exfat_volume_t* log_volume = NULL;
static exfat_file_t log_file;
static int log_initialized = 0;
static int log_level = LOG_DEBUG;
static int log_to_serial = 0;  // Can enable serial for critical errors

// Initialize logging system
int logger_init(exfat_volume_t* volume) {
    if (!volume) return -1;
    
    log_volume = volume;
    log_initialized = 0;
    
    // CHANGE: Use flattened filename
    if (exfat_open(volume, ".kernel.system.log", &log_file) < 0) {
        if (exfat_create(volume, ".kernel.system.log") < 0) {
            return -1;
        }
        
        if (exfat_open(volume, ".kernel.system.log", &log_file) < 0) {
            return -1;
        }
    }
    
    // Verify file was opened correctly
    if (!log_file.is_open) {
        return -1;
    }
    
    // Seek to end for appending
    exfat_seek(&log_file, log_file.file_size);
    
    log_initialized = 1;  // Now mark as ready
    
    // Write header - but catch any errors
    const char* header = "[INFO] SYSTEM: === System Boot Log ===\n";
    exfat_write(log_volume, &log_file, header, strlen(header));
    
    return 0;
}

// Set log level
void logger_set_level(log_level_t level) {
    log_level = level;
}

// Enable/disable serial output for critical errors
void logger_set_serial(int enable) {
    log_to_serial = enable;
}

// Get level string
static const char* log_level_string(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "?????";
    }
}

// Write log entry
void log_write(log_level_t level, const char* subsystem, const char* message) {
    if (!log_initialized || !log_volume || level < log_level) {
        return;
    }
    
    // Format: [LEVEL] subsystem: message\n
    char buffer[512];
    int len = 0;
    
    // Level
    buffer[len++] = '[';
    const char* level_str = log_level_string(level);
    for (int i = 0; level_str[i]; i++) {
        buffer[len++] = level_str[i];
    }
    buffer[len++] = ']';
    buffer[len++] = ' ';
    
    // Subsystem
    for (int i = 0; subsystem[i] && len < 500; i++) {
        buffer[len++] = subsystem[i];
    }
    buffer[len++] = ':';
    buffer[len++] = ' ';
    
    // Message
    for (int i = 0; message[i] && len < 510; i++) {
        buffer[len++] = message[i];
    }
    buffer[len++] = '\n';
    buffer[len] = '\0';
    
    // Write to file using stored volume pointer
    exfat_write(log_volume, &log_file, buffer, len);
    
    // Also write to serial if critical and enabled
    if (log_to_serial && level >= LOG_ERROR) {
        extern void serial_puts(const char*);
        serial_puts(buffer);
    }
}

// Printf-style logging
void log_printf(log_level_t level, const char* subsystem, const char* format, ...) {
    if (!log_initialized || level < log_level) {
        return;
    }
    
    char message[256];
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    char* out = message;
    while (*format && out < message + 255) {
        if (*format == '%' && *(format + 1)) {
            format++;
            switch (*format) {
                case 'd':
                case 'u': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    char tmp[16];
                    int i = 0;
                    if (val == 0) {
                        *out++ = '0';
                    } else {
                        while (val > 0) {
                            tmp[i++] = '0' + (val % 10);
                            val /= 10;
                        }
                        while (i > 0) {
                            *out++ = tmp[--i];
                        }
                    }
                    break;
                }
                case 'x': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    char hex[] = "0123456789abcdef";
                    *out++ = '0';
                    *out++ = 'x';
                    for (int i = 28; i >= 0; i -= 4) {
                        *out++ = hex[(val >> i) & 0xF];
                    }
                    break;
                }
                case 's': {
                    const char* s = __builtin_va_arg(args, const char*);
                    while (s && *s && out < message + 255) {
                        *out++ = *s++;
                    }
                    break;
                }
                case 'c': {
                    *out++ = (char)__builtin_va_arg(args, int);
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
    log_write(level, subsystem, message);
}

// Flush log to disk
void logger_flush(void) {
    if (!log_initialized) return;
    
    // Close and reopen to flush
    exfat_close(&log_file);
    exfat_open(log_volume, "/.kernel/system.log", &log_file);
    exfat_seek(&log_file, log_file.file_size);
}

// Close logger
void logger_close(void) {
    if (!log_initialized) return;
    
    log_write(LOG_INFO, "SYSTEM", "=== Log Closed ===");
    exfat_close(&log_file);
    log_initialized = 0;
}
