#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "../common/types.h"

// Memory management functions
void init_process_pool(void);
void cleanup_process_pool(void);
Process* alloc_process(void);
void free_process(Process *proc);
Process* copy_process(const Process *proc);
void free_update_data(UpdateData *data);

char* get_cached_buffer(size_t min_size);
void return_cached_buffer(char *buffer, size_t size);

// Security functions
gboolean is_safe_command(const char *cmd);
void safe_strncpy(char *dest, const char *src, size_t dest_size);
void safe_strncat(char *dest, const char *src, size_t dest_size);

// Command execution functions
char* run_command(const char *cmd);
char* get_full_output(const char *cmd);

// String parsing and formatting functions
long long parse_bytes(const char *str);
char* format_bytes_human_readable(long long bytes);
long long parse_memory_string(const char *str);
char* format_memory_human_readable(const char *mem_str);
long long parse_runtime_to_seconds(const char *str);

// Cleanup functions
void cleanup_resources(void);

#endif // UTILS_H
