// src/include/logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include "../core/types.h"
#include "../fs/exfat.h"

// Log levels
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
} log_level_t;

// Logger functions
int logger_init(exfat_volume_t* volume);
void logger_set_level(log_level_t level);
void logger_set_serial(int enable);
void log_write(log_level_t level, const char* subsystem, const char* message);
void log_printf(log_level_t level, const char* subsystem, const char* format, ...);
void logger_flush(void);
void logger_close(void);

// Convenience macros
#define LOG_DEBUG_MSG(subsys, msg) log_write(LOG_DEBUG, subsys, msg)
#define LOG_INFO_MSG(subsys, msg)  log_write(LOG_INFO, subsys, msg)
#define LOG_WARN_MSG(subsys, msg)  log_write(LOG_WARN, subsys, msg)
#define LOG_ERROR_MSG(subsys, msg) log_write(LOG_ERROR, subsys, msg)

#endif // LOGGER_H
