#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <mach/vm_map.h>
#include <glib.h>
#include "../common/types.h"

// Performance-optimized system data collection
typedef struct {
    // Cached system information (read once)
    size_t total_memory;
    int cpu_count;
    size_t page_size;
    
    // Cached for short periods
    double cpu_usage;
    double memory_usage;
    time_t last_cpu_update;
    time_t last_memory_update;
    
    // High-performance buffers
    char *process_buffer;
    size_t buffer_size;
    size_t buffer_used;
    
    // System statistics cache
    host_cpu_load_info_data_t prev_cpu_info;
    host_cpu_load_info_data_t curr_cpu_info;
    vm_statistics64_data_t vm_stats;
    
} SystemCache;

// Fast system data collection functions
int init_system_cache(SystemCache *cache);
int update_cpu_stats_fast(SystemCache *cache);
int update_memory_stats_fast(SystemCache *cache);
int get_process_list_fast(SystemCache *cache);

// Optimized parsing functions
int parse_process_line_fast(const char *line, Process *proc);
double calculate_cpu_percentage_fast(const SystemCache *cache);
double calculate_memory_percentage_fast(const SystemCache *cache);

// Memory-efficient string operations
char* fast_string_copy(char *dest, const char *src, size_t max_len);
int fast_string_to_double(const char *str, double *result);
int fast_string_to_long(const char *str, long *result);

// Batch operations for efficiency
int collect_all_system_stats(SystemCache *cache);
int update_process_stats_batch(GList **processes, SystemCache *cache);

// Public fast interface functions
double get_system_cpu_usage_fast(void);
double get_system_memory_usage_fast(void);

#endif // PERFORMANCE_H
