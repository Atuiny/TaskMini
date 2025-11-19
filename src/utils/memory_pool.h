#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include "../common/types.h"
#include <stdlib.h>
#include <string.h>

// Memory pool configuration
#define POOL_SIZE 1024
#define STRING_POOL_SIZE 4096
#define STRING_BUFFER_SIZE 256

// Process memory pool
typedef struct ProcessPool {
    Process processes[POOL_SIZE];
    int used[POOL_SIZE];
    int next_free;
    int total_allocated;
} ProcessPool;

// String buffer pool for temporary allocations
typedef struct StringPool {
    char buffers[STRING_POOL_SIZE][STRING_BUFFER_SIZE];
    int used[STRING_POOL_SIZE];
    int next_free;
} StringPool;

// Memory pool management functions
int init_memory_pools(void);
void cleanup_memory_pools(void);

// Process pool functions
Process* get_process_from_pool_fast(void);
void return_process_to_pool_fast(Process *proc);
void reset_process_pool(void);

// String pool functions  
char* get_string_buffer_from_pool(void);
void return_string_buffer_to_pool(char *buffer);
char* duplicate_string_pooled(const char *src);

// Memory statistics
int get_pool_usage_stats(int *process_used, int *string_used);
void print_memory_pool_stats(void);

// Bulk operations
void return_all_processes_to_pool(GList *process_list);
GList* allocate_process_list_from_pool(int count);

#endif // MEMORY_POOL_H
