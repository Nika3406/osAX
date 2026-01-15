// src/include/shell.h
#ifndef SHELL_H
#define SHELL_H

#include "../fs/metafs.h"

void shell_init(metafs_context_t* metafs);
void shell_prompt(void);
void shell_execute(char* line);

#endif // SHELL_H
