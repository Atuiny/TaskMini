#include "utils.h"
#include "memory_pool.h"
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
    // Use the optimized memory pool
    return get_process_from_pool_fast();
}

// Compatibility wrapper for existing code
Process* get_process_from_pool(void) {
    return get_process_from_pool_fast();
}

// Compatibility wrapper for returning processes
void return_process_to_pool(Process *proc) {
    return_process_to_pool_fast(proc);
}

void free_process(Process *proc) {
    // Use the optimized pool return function
    return_process_to_pool_fast(proc);
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

Process* copy_process(const Process *proc) {
    if (!proc) return NULL;
    
    Process *copy = alloc_process();
    if (!copy) return NULL;
    
    // Copy all fields
    safe_strncpy(copy->pid, proc->pid, sizeof(copy->pid));
    safe_strncpy(copy->name, proc->name, sizeof(copy->name));
    safe_strncpy(copy->cpu, proc->cpu, sizeof(copy->cpu));
    safe_strncpy(copy->gpu, proc->gpu, sizeof(copy->gpu));
    safe_strncpy(copy->mem, proc->mem, sizeof(copy->mem));
    safe_strncpy(copy->net, proc->net, sizeof(copy->net));
    safe_strncpy(copy->runtime, proc->runtime, sizeof(copy->runtime));
    safe_strncpy(copy->type, proc->type, sizeof(copy->type));
    
    return copy;
}

void free_update_data(UpdateData *data) {
    if (!data) return;
    
    // Free process list
    if (data->processes) {
        GList *current = data->processes;
        while (current) {
            Process *proc = (Process*)current->data;
            free_process(proc);
            current = current->next;
        }
        g_list_free(data->processes);
    }
    
    // Free GPU usage string
    if (data->gpu_usage) {
        g_free(data->gpu_usage);
    }
    
    // Free the UpdateData structure itself
    g_free(data);
}
