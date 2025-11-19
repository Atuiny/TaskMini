#include "performance.h"
#include "../common/types.h"
#include "../utils/utils.h"
#include "../utils/memory_pool.h"
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <mach/mach.h>

// Cache durations (in seconds)
#define CPU_CACHE_DURATION 1    // Update CPU every second for accuracy
#define MEMORY_CACHE_DURATION 2 // Memory can be cached for 2 seconds
#define PROCESS_BUFFER_SIZE (2 * 1024 * 1024)  // 2MB buffer

// Global system cache
static SystemCache g_system_cache = {0};
static int cache_initialized = 0;

// Initialize the system cache with static data
int init_system_cache(SystemCache *cache) {
    if (!cache) return -1;
    
    // Get total memory (only needs to be read once)
    size_t size = sizeof(cache->total_memory);
    if (sysctlbyname("hw.memsize", &cache->total_memory, &size, NULL, 0) != 0) {
        cache->total_memory = 0;
        return -1;
    }
    
    // Get CPU count (only needs to be read once)
    size = sizeof(cache->cpu_count);
    if (sysctlbyname("hw.ncpu", &cache->cpu_count, &size, NULL, 0) != 0) {
        cache->cpu_count = 1;
    }
    
    // Get page size (only needs to be read once)
    cache->page_size = getpagesize();
    
    // Initialize process buffer
    cache->process_buffer = malloc(PROCESS_BUFFER_SIZE);
    if (!cache->process_buffer) return -1;
    
    cache->buffer_size = PROCESS_BUFFER_SIZE;
    cache->buffer_used = 0;
    
    // Initialize cache timestamps
    cache->last_cpu_update = 0;
    cache->last_memory_update = 0;
    
    // Initialize CPU stats
    memset(&cache->prev_cpu_info, 0, sizeof(cache->prev_cpu_info));
    memset(&cache->curr_cpu_info, 0, sizeof(cache->curr_cpu_info));
    
    return 0;
}

// Fast CPU usage calculation using Mach system calls
int update_cpu_stats_fast(SystemCache *cache) {
    if (!cache) return -1;
    
    time_t now = time(NULL);
    if (now - cache->last_cpu_update < CPU_CACHE_DURATION) {
        return 0; // Use cached value
    }
    
    // Move current stats to previous
    cache->prev_cpu_info = cache->curr_cpu_info;
    
    // Get current CPU statistics
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                   (host_info_t)&cache->curr_cpu_info, &count);
    
    // Calculate CPU usage if we have previous data
    if (cache->last_cpu_update > 0) {
        unsigned int user_diff = cache->curr_cpu_info.cpu_ticks[CPU_STATE_USER] - 
                               cache->prev_cpu_info.cpu_ticks[CPU_STATE_USER];
        unsigned int system_diff = cache->curr_cpu_info.cpu_ticks[CPU_STATE_SYSTEM] - 
                                 cache->prev_cpu_info.cpu_ticks[CPU_STATE_SYSTEM];
        unsigned int idle_diff = cache->curr_cpu_info.cpu_ticks[CPU_STATE_IDLE] - 
                               cache->prev_cpu_info.cpu_ticks[CPU_STATE_IDLE];
        unsigned int nice_diff = cache->curr_cpu_info.cpu_ticks[CPU_STATE_NICE] - 
                               cache->prev_cpu_info.cpu_ticks[CPU_STATE_NICE];
        
        unsigned int total_diff = user_diff + system_diff + idle_diff + nice_diff;
        
        if (total_diff > 0) {
            cache->cpu_usage = ((double)(user_diff + system_diff + nice_diff) / total_diff) * 100.0;
        }
    }
    
    cache->last_cpu_update = now;
    return 0;
}

// Fast memory usage calculation using direct VM calls
int update_memory_stats_fast(SystemCache *cache) {
    if (!cache) return -1;
    
    time_t now = time(NULL);
    if (now - cache->last_memory_update < MEMORY_CACHE_DURATION) {
        return 0; // Use cached value
    }
    
    // Get VM statistics directly
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         (host_info64_t)&cache->vm_stats, &count) != KERN_SUCCESS) {
        return -1;
    }
    
    // Calculate memory usage using Activity Monitor's method
    uint64_t used_pages = cache->vm_stats.active_count +
                         cache->vm_stats.inactive_count +
                         cache->vm_stats.speculative_count +
                         cache->vm_stats.wire_count +
                         cache->vm_stats.compressor_page_count;
    
    uint64_t used_bytes = used_pages * cache->page_size;
    cache->memory_usage = ((double)used_bytes / cache->total_memory) * 100.0;
    
    cache->last_memory_update = now;
    return 0;
}

// Get cached CPU percentage (fast)
double calculate_cpu_percentage_fast(const SystemCache *cache) {
    if (!cache || cache->last_cpu_update == 0) {
        // Fallback to quick calculation if cache is empty
        FILE *fp = popen("top -l 1 -n 0 | awk '/CPU usage:/ {print 100-$(NF-1)}'", "r");
        if (fp) {
            char buffer[32];
            if (fgets(buffer, sizeof(buffer), fp)) {
                pclose(fp);
                return atof(buffer);
            }
            pclose(fp);
        }
        return 0.0;
    }
    
    return cache->cpu_usage;
}

// Get cached memory percentage (fast)
double calculate_memory_percentage_fast(const SystemCache *cache) {
    if (!cache || cache->last_memory_update == 0) {
        return 0.0;
    }
    
    return cache->memory_usage;
}

// Optimized string operations
char* fast_string_copy(char *dest, const char *src, size_t max_len) {
    if (!dest || !src || max_len == 0) return dest;
    
    size_t len = 0;
    while (len < max_len - 1 && src[len] != '\0') {
        dest[len] = src[len];
        len++;
    }
    dest[len] = '\0';
    
    return dest;
}

// Fast string to double conversion (optimized for system metrics)
int fast_string_to_double(const char *str, double *result) {
    if (!str || !result) return -1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t') str++;
    
    // Simple fast conversion for percentage values
    double value = 0.0;
    int decimal_places = 0;
    int in_decimal = 0;
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            if (in_decimal) {
                value = value + (*str - '0') / pow(10, ++decimal_places);
            } else {
                value = value * 10 + (*str - '0');
            }
        } else if (*str == '.' && !in_decimal) {
            in_decimal = 1;
        } else {
            break; // Stop at non-numeric characters
        }
        str++;
    }
    
    *result = value;
    return 0;
}

// Fast string to long conversion
int fast_string_to_long(const char *str, long *result) {
    if (!str || !result) return -1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t') str++;
    
    long value = 0;
    int negative = 0;
    
    if (*str == '-') {
        negative = 1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        value = value * 10 + (*str - '0');
        str++;
    }
    
    *result = negative ? -value : value;
    return 0;
}

// Batch system statistics collection
int collect_all_system_stats(SystemCache *cache) {
    if (!cache_initialized) {
        if (init_system_cache(&g_system_cache) != 0) {
            return -1;
        }
        cache_initialized = 1;
    }
    
    if (!cache) cache = &g_system_cache;
    
    // Update all cached statistics in one go
    update_cpu_stats_fast(cache);
    update_memory_stats_fast(cache);
    
    return 0;
}

// Get fast process list using optimized parsing
int get_process_list_fast(SystemCache *cache) {
    if (!cache) return -1;
    
    // Reset buffer
    cache->buffer_used = 0;
    
    // Use single top command for all process data
    FILE *fp = popen("top -l 1 -o cpu -stats pid,command,cpu,mem,time", "r");
    if (!fp) return -1;
    
    char line[1024];
    while (fgets(line, sizeof(line), fp) && cache->buffer_used < cache->buffer_size - 1024) {
        size_t line_len = strlen(line);
        memcpy(cache->process_buffer + cache->buffer_used, line, line_len);
        cache->buffer_used += line_len;
    }
    
    pclose(fp);
    cache->process_buffer[cache->buffer_used] = '\0';
    
    return 0;
}

// Fast process line parsing (optimized for speed)
int parse_process_line_fast(const char *line, Process *proc) {
    if (!line || !proc) return -1;
    
    // Fast parsing using pointer arithmetic instead of strtok
    const char *ptr = line;
    
    // Skip leading whitespace
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    
    // Parse PID (first field)
    long pid;
    if (fast_string_to_long(ptr, &pid) != 0) return -1;
    snprintf(proc->pid, sizeof(proc->pid), "%ld", pid);
    
    // Move to next field
    while (*ptr && *ptr != ' ' && *ptr != '\t') ptr++;
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    
    // Parse command name (second field)
    const char *cmd_start = ptr;
    while (*ptr && *ptr != ' ' && *ptr != '\t') ptr++;
    size_t cmd_len = ptr - cmd_start;
    if (cmd_len >= sizeof(proc->name)) cmd_len = sizeof(proc->name) - 1;
    memcpy(proc->name, cmd_start, cmd_len);
    proc->name[cmd_len] = '\0';
    
    // Parse CPU percentage (third field)
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    double cpu_val;
    if (fast_string_to_double(ptr, &cpu_val) == 0) {
        snprintf(proc->cpu, sizeof(proc->cpu), "%.1f%%", cpu_val);
    }
    
    return 0;
}

// Batch process statistics update
int update_process_stats_batch(GList **processes, SystemCache *cache) {
    if (!processes || !cache) return -1;
    
    // Collect fresh process data
    if (get_process_list_fast(cache) != 0) {
        return -1;
    }
    
    // Clear existing process list
    g_list_free_full(*processes, (GDestroyNotify)free_process);
    *processes = NULL;
    
    // Parse all processes from buffer
    char *buffer = cache->process_buffer;
    char *line_start = buffer;
    char *line_end;
    
    int process_count = 0;
    const int MAX_PROCESSES = 2000; // Security limit
    
    while ((line_end = strchr(line_start, '\n')) != NULL && process_count < MAX_PROCESSES) {
        *line_end = '\0'; // Temporarily null-terminate
        
        Process *proc = get_process_from_pool_fast();
        if (proc && parse_process_line_fast(line_start, proc) == 0) {
            *processes = g_list_prepend(*processes, proc);
            process_count++;
        } else if (proc) {
            return_process_to_pool_fast(proc); // Return unused process to pool
        }
        
        *line_end = '\n'; // Restore newline
        line_start = line_end + 1;
    }
    
    return process_count;
}

// Public interface functions for the optimized system
double get_system_cpu_usage_fast(void) {
    collect_all_system_stats(&g_system_cache);
    return calculate_cpu_percentage_fast(&g_system_cache);
}

double get_system_memory_usage_fast(void) {
    collect_all_system_stats(&g_system_cache);
    return calculate_memory_percentage_fast(&g_system_cache);
}
