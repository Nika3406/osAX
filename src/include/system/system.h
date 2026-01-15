// src/include/system.h
#ifndef SYSTEM_H
#define SYSTEM_H

#include "../core/types.h"
#include "../fs/exfat.h"
#include "../fs/metafs.h"

// System boot - returns initialized MetaFS context
metafs_context_t* system_boot(exfat_volume_t* volume);

// Clean shutdown
void system_shutdown(void);

// Get system statistics
void system_get_stats(uint32_t* boot_count, uint32_t* clean_shutdown);

#endif // SYSTEM_H
