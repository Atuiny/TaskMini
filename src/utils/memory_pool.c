#include "memory_pool.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

// Global memory pools
static ProcessPool g_process_pool = {0};
static StringPool g_string_pool = {0};
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize all memory pools
int init_memory_pools(void) {
    pthread_mutex_lock(&pool_mutex);
    
    // Initialize process pool
    memset(&g_process_pool, 0, sizeof(g_process_pool));
    g_process_pool.next_free = 0;
    g_process_pool.total_allocated = 0;
    
    // Initialize string pool
    memset(&g_string_pool, 0, sizeof(g_string_pool));
    g_string_pool.next_free = 0;
    
    pthread_mutex_unlock(&pool_mutex);
    return 0;
}

// Cleanup memory pools
void cleanup_memory_pools(void) {
    pthread_mutex_lock(&pool_mutex);
    
    // Clear process pool
    memset(&g_process_pool, 0, sizeof(g_process_pool));
    
    // Clear string pool
    memset(&g_string_pool, 0, sizeof(g_string_pool));
    
    pthread_mutex_unlock(&pool_mutex);
}

// Get a process from the pool (fast, no malloc)
Process* get_process_from_pool_fast(void) {
    pthread_mutex_lock(&pool_mutex);
    
    // Find next available process
    int start = g_process_pool.next_free;
    for (int i = 0; i < POOL_SIZE; i++) {
        int idx = (start + i) % POOL_SIZE;
        if (!g_process_pool.used[idx]) {
            g_process_pool.used[idx] = 1;
            g_process_pool.next_free = (idx + 1) % POOL_SIZE;
            g_process_pool.total_allocated++;
            
            // Clear the process structure
            Process *proc = &g_process_pool.processes[idx];
            memset(proc, 0, sizeof(Process));
            
            pthread_mutex_unlock(&pool_mutex);
            return proc;
        }
    }
    
    pthread_mutex_unlock(&pool_mutex);
    
    // Pool is full, fallback to malloc
    Process *proc = malloc(sizeof(Process));
    if (proc) {
        memset(proc, 0, sizeof(Process));
    }
    return proc;
}

// Return a process to the pool
void return_process_to_pool_fast(Process *proc) {
    if (!proc) return;
    
    pthread_mutex_lock(&pool_mutex);
    
    // Check if this process is from our pool
    if (proc >= &g_process_pool.processes[0] && 
        proc < &g_process_pool.processes[POOL_SIZE]) {
        
        int idx = proc - &g_process_pool.processes[0];
        if (g_process_pool.used[idx]) {
            g_process_pool.used[idx] = 0;
            g_process_pool.total_allocated--;
            
            // Clear sensitive data
            memset(proc, 0, sizeof(Process));
        }
    } else {
        // This was a malloc'd process, free it
        free(proc);
    }
    
    pthread_mutex_unlock(&pool_mutex);
}

// Reset the entire process pool
void reset_process_pool(void) {
    pthread_mutex_lock(&pool_mutex);
    
    memset(g_process_pool.used, 0, sizeof(g_process_pool.used));
    g_process_pool.next_free = 0;
    g_process_pool.total_allocated = 0;
    
    pthread_mutex_unlock(&pool_mutex);
}

// Get a string buffer from the pool
char* get_string_buffer_from_pool(void) {
    pthread_mutex_lock(&pool_mutex);
    
    // Find next available buffer
    int start = g_string_pool.next_free;
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        int idx = (start + i) % STRING_POOL_SIZE;
        if (!g_string_pool.used[idx]) {
            g_string_pool.used[idx] = 1;
            g_string_pool.next_free = (idx + 1) % STRING_POOL_SIZE;
            
            // Clear the buffer
            memset(g_string_pool.buffers[idx], 0, STRING_BUFFER_SIZE);
            
            pthread_mutex_unlock(&pool_mutex);
            return g_string_pool.buffers[idx];
        }
    }
    
    pthread_mutex_unlock(&pool_mutex);
    
    // Pool is full, fallback to malloc
    char *buffer = malloc(STRING_BUFFER_SIZE);
    if (buffer) {
        memset(buffer, 0, STRING_BUFFER_SIZE);
    }
    return buffer;
}

// Return a string buffer to the pool
void return_string_buffer_to_pool(char *buffer) {
    if (!buffer) return;
    
    pthread_mutex_lock(&pool_mutex);
    
    // Check if this buffer is from our pool
    for (int i = 0; i < STRING_POOL_SIZE; i++) {
        if (buffer == g_string_pool.buffers[i]) {
            if (g_string_pool.used[i]) {
                g_string_pool.used[i] = 0;
                // Clear the buffer for security
                memset(buffer, 0, STRING_BUFFER_SIZE);
            }
            pthread_mutex_unlock(&pool_mutex);
            return;
        }
    }
    
    // This was a malloc'd buffer, free it
    free(buffer);
    
    pthread_mutex_unlock(&pool_mutex);
}

// Duplicate a string using the pool
char* duplicate_string_pooled(const char *src) {
    if (!src) return NULL;
    
    char *buffer = get_string_buffer_from_pool();
    if (!buffer) return NULL;
    
    size_t len = strlen(src);
    if (len >= STRING_BUFFER_SIZE) {
        len = STRING_BUFFER_SIZE - 1;
    }
    
    memcpy(buffer, src, len);
    buffer[len] = '\0';
    
    return buffer;
}

// Get memory pool usage statistics
int get_pool_usage_stats(int *process_used, int *string_used) {
    pthread_mutex_lock(&pool_mutex);
    
    if (process_used) {
        *process_used = g_process_pool.total_allocated;
    }
    
    if (string_used) {
        int count = 0;
        for (int i = 0; i < STRING_POOL_SIZE; i++) {
            if (g_string_pool.used[i]) count++;
        }
        *string_used = count;
    }
    
    pthread_mutex_unlock(&pool_mutex);
    return 0;
}



// Bulk operations for efficiency
void return_all_processes_to_pool(GList *process_list) {
    GList *current = process_list;
    while (current) {
        Process *proc = (Process *)current->data;
        return_process_to_pool_fast(proc);
        current = current->next;
    }
}

// Pre-allocate a list of processes from the pool
GList* allocate_process_list_from_pool(int count) {
    GList *list = NULL;
    
    for (int i = 0; i < count && i < POOL_SIZE; i++) {
        Process *proc = get_process_from_pool_fast();
        if (proc) {
            list = g_list_prepend(list, proc);
        } else {
            break; // Pool exhausted
        }
    }
    
    return list;
}
