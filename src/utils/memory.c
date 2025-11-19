#include "utils.h"
#include "../ui/ui.h"
#include "../common/config.h"
#include <time.h>

// OPTIMIZATION: Memory pool for Process structs to reduce malloc/free overhead
static Process *process_pool = NULL;
static gboolean *pool_used = NULL;
static int pool_initialized = 0;

// OPTIMIZATION: String buffer cache to reduce malloc overhead for command outputs
static char *string_cache[STRING_CACHE_SIZE];
static size_t cache_sizes[STRING_CACHE_SIZE];
static int cache_initialized = 0;

// OPTIMIZATION: Memory pool management for Process structs
void init_process_pool(void) {
    if (!pool_initialized) {
        process_pool = calloc(PROCESS_POOL_SIZE, sizeof(Process));
        pool_used = calloc(PROCESS_POOL_SIZE, sizeof(gboolean));
        pool_initialized = 1;
        
        // Initialize string cache
        for (int i = 0; i < STRING_CACHE_SIZE; i++) {
            string_cache[i] = NULL;
            cache_sizes[i] = 0;
        }
        cache_initialized = 1;
    }
}

Process* alloc_process(void) {
    if (!pool_initialized) init_process_pool();
    
    for (int i = 0; i < PROCESS_POOL_SIZE; i++) {
        if (!pool_used[i]) {
            pool_used[i] = TRUE;
            memset(&process_pool[i], 0, sizeof(Process));
            return &process_pool[i];
        }
    }
    
    // Fallback to malloc if pool exhausted (rare case)
    return malloc(sizeof(Process));
}

void free_process(Process *proc) {
    if (!proc) return;
    
    // Check if it's from our pool
    if (proc >= process_pool && proc < process_pool + PROCESS_POOL_SIZE) {
        int index = proc - process_pool;
        pool_used[index] = FALSE;
    } else {
        // It was a malloc'd fallback
        free(proc);
    }
}

void cleanup_process_pool(void) {
    if (pool_initialized) {
        if (process_pool) {
            free(process_pool);
            process_pool = NULL;
        }
        if (pool_used) {
            free(pool_used);
            pool_used = NULL;
        }
        pool_initialized = 0;
    }
}

char* get_cached_buffer(size_t min_size) {
    if (!cache_initialized) {
        for (int i = 0; i < STRING_CACHE_SIZE; i++) {
            string_cache[i] = NULL;
            cache_sizes[i] = 0;
        }
        cache_initialized = 1;
    }
    
    for (int i = 0; i < STRING_CACHE_SIZE; i++) {
        if (string_cache[i] && cache_sizes[i] >= min_size) {
            char *buffer = string_cache[i];
            string_cache[i] = NULL;
            cache_sizes[i] = 0;
            return buffer;
        }
    }
    
    return malloc(min_size);
}

void return_cached_buffer(char *buffer, size_t size) {
    if (!buffer) return;
    
    for (int i = 0; i < STRING_CACHE_SIZE; i++) {
        if (!string_cache[i]) {
            string_cache[i] = buffer;
            cache_sizes[i] = size;
            return;
        }
    }
    
    // Cache full, just free it
    free(buffer);
}

void cleanup_resources(void) {
    cleanup_process_pool();
    
    // Clean up UI resources
    cleanup_ui_resources();
    
    // Clean up string cache
    if (cache_initialized) {
        for (int i = 0; i < STRING_CACHE_SIZE; i++) {
            if (string_cache[i]) {
                free(string_cache[i]);
                string_cache[i] = NULL;
            }
        }
        cache_initialized = 0;
    }
}
