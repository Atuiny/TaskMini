#include "threaded_collector.h"
#include "system.h"
#include "performance.h"
#include "../utils/utils.h"
#include "../common/config.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

// Global collector instance
static ThreadedCollector *g_collector = NULL;

// Helper function to parse process line for basic info only (fast)
Process* parse_process_line_basic(const char *line) {
    if (!line || strlen(line) < 10) return NULL;
    
    // Parse the top output line - similar to original but only extract basics
    char pid_str[20] = "";
    char name[100] = "";
    
    // Top format: PID COMMAND %CPU TIME #TH #WQ #PORT MEM PURG CMPRS PGRP PPID STATE BOOSTS %CPU_ME %CPU_OTHERS UID FAULTS COW MSGSENT MSGRECV SYSBSD SYSMACH CSW PAGEINS USER #MREGS RPRVT VPRVT VSIZE KPRVT KSHRD
    if (sscanf(line, "%19s %99s", pid_str, name) >= 2) {
        Process *proc = alloc_process();
        if (proc) {
            safe_strncpy(proc->pid, pid_str, sizeof(proc->pid));
            safe_strncpy(proc->name, name, sizeof(proc->name));
            
            // Determine process type
            determine_process_type(proc);
            
            // Set default placeholder values
            safe_strncpy(proc->cpu, "...", sizeof(proc->cpu));
            safe_strncpy(proc->mem, "...", sizeof(proc->mem));
            safe_strncpy(proc->gpu, "...", sizeof(proc->gpu));
            safe_strncpy(proc->net, "...", sizeof(proc->net));
            safe_strncpy(proc->runtime, get_run_time(proc->pid), sizeof(proc->runtime));
            
            return proc;
        }
    }
    
    return NULL;
}

// Create a new threaded collector
ThreadedCollector* threaded_collector_create(void) {
    ThreadedCollector *collector = g_malloc0(sizeof(ThreadedCollector));
    
    // Initialize result structures
    collector->process_list = g_malloc0(sizeof(ProcessListResult));
    collector->process_list->system_summary = NULL;
    collector->cpu_data = g_malloc0(sizeof(CPUDataResult));
    collector->memory_data = g_malloc0(sizeof(MemoryDataResult));
    collector->gpu_data = g_malloc0(sizeof(GPUDataResult));
    collector->network_data = g_malloc0(sizeof(NetworkDataResult));
    
    // Initialize mutexes
    g_mutex_init(&collector->process_list->mutex);
    g_mutex_init(&collector->cpu_data->mutex);
    g_mutex_init(&collector->memory_data->mutex);
    g_mutex_init(&collector->gpu_data->mutex);
    g_mutex_init(&collector->network_data->mutex);
    g_mutex_init(&collector->coordinator_mutex);
    
    // Initialize hash tables
    collector->cpu_data->process_cpu = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    collector->memory_data->process_memory = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    collector->network_data->process_network = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    collector->network_data->prev_net_bytes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    collector->network_data->prev_times = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    // Set all states to idle
    collector->process_list->state = THREAD_STATE_IDLE;
    collector->cpu_data->state = THREAD_STATE_IDLE;
    collector->memory_data->state = THREAD_STATE_IDLE;
    collector->gpu_data->state = THREAD_STATE_IDLE;
    collector->network_data->state = THREAD_STATE_IDLE;
    
    // Initialize collector/bin system
    collector->data_bin = NULL;
    g_mutex_init(&collector->bin_mutex);
    collector->collector_thread = NULL;
    collector->continuous_mode = FALSE;
    
    collector->shutdown_requested = FALSE;
    
    return collector;
}

// Destroy the collector and cleanup resources
void threaded_collector_destroy(ThreadedCollector *collector) {
    if (!collector) return;
    
    // Signal shutdown
    g_mutex_lock(&collector->coordinator_mutex);
    collector->shutdown_requested = TRUE;
    g_mutex_unlock(&collector->coordinator_mutex);
    
    // Wait for threads to complete (with timeout)
    if (collector->process_thread) {
        g_thread_join(collector->process_thread);
    }
    if (collector->cpu_thread) {
        g_thread_join(collector->cpu_thread);
    }
    if (collector->memory_thread) {
        g_thread_join(collector->memory_thread);
    }
    if (collector->gpu_thread) {
        g_thread_join(collector->gpu_thread);
    }
    if (collector->network_thread) {
        g_thread_join(collector->network_thread);
    }
    
    // Wait for continuous collector thread
    if (collector->collector_thread) {
        g_thread_join(collector->collector_thread);
    }
    
    // Cleanup result data
    cleanup_process_list_result(collector->process_list);
    cleanup_cpu_data_result(collector->cpu_data);
    cleanup_memory_data_result(collector->memory_data);
    cleanup_gpu_data_result(collector->gpu_data);
    cleanup_network_data_result(collector->network_data);
    
    // Cleanup data bin
    g_mutex_lock(&collector->bin_mutex);
    if (collector->data_bin) {
        free_update_data(collector->data_bin);
        collector->data_bin = NULL;
    }
    g_mutex_unlock(&collector->bin_mutex);
    
    // Cleanup mutexes
    g_mutex_clear(&collector->coordinator_mutex);
    g_mutex_clear(&collector->bin_mutex);
    
    g_free(collector);
}

// Start data collection in all threads
void threaded_collector_start_collection(ThreadedCollector *collector) {
    if (!collector) return;
    
    g_mutex_lock(&collector->coordinator_mutex);
    collector->collection_start_time = time(NULL);
    collector->shutdown_requested = FALSE;
    g_mutex_unlock(&collector->coordinator_mutex);
    
    // Start threads with different priorities
    // Highest priority: Process list (fast, needed immediately)
    collector->process_thread = g_thread_new("process_collector", collect_process_list_thread, collector);
    
    // High priority: CPU and Memory (relatively fast)
    collector->cpu_thread = g_thread_new("cpu_collector", collect_cpu_data_thread, collector);
    collector->memory_thread = g_thread_new("memory_collector", collect_memory_data_thread, collector);
    
    // Lower priority: GPU and Network (slower operations)
    collector->gpu_thread = g_thread_new("gpu_collector", collect_gpu_data_thread, collector);
    collector->network_thread = g_thread_new("network_collector", collect_network_data_thread, collector);
}

// Check if basic data (process list) is available
gboolean threaded_collector_has_basic_data(ThreadedCollector *collector) {
    if (!collector || !collector->process_list) return FALSE;
    
    g_mutex_lock(&collector->process_list->mutex);
    gboolean has_data = (collector->process_list->state == THREAD_STATE_COMPLETED && 
                        collector->process_list->processes != NULL);
    g_mutex_unlock(&collector->process_list->mutex);
    
    return has_data;
}

// Check if all data collection is complete
gboolean threaded_collector_has_complete_data(ThreadedCollector *collector) {
    if (!collector) return FALSE;
    
    return (collector->process_list->state == THREAD_STATE_COMPLETED &&
            collector->cpu_data->state == THREAD_STATE_COMPLETED &&
            collector->memory_data->state == THREAD_STATE_COMPLETED &&
            collector->gpu_data->state == THREAD_STATE_COMPLETED &&
            collector->network_data->state == THREAD_STATE_COMPLETED);
}

// Get available data merged into UpdateData structure
UpdateData* threaded_collector_get_available_data(ThreadedCollector *collector) {
    if (!collector) return NULL;
    
    UpdateData *data = g_malloc0(sizeof(UpdateData));
    
    // Get process list and system summary if available
    g_mutex_lock(&collector->process_list->mutex);
    if (collector->process_list->state == THREAD_STATE_COMPLETED && collector->process_list->processes) {
        data->processes = g_list_copy_deep(collector->process_list->processes, 
                                          (GCopyFunc)copy_process, NULL);
        if (collector->process_list->system_summary) {
            data->system_summary = g_strdup(collector->process_list->system_summary);
        }
    }
    g_mutex_unlock(&collector->process_list->mutex);
    
    // Get system-wide usage data if available
    g_mutex_lock(&collector->cpu_data->mutex);
    if (collector->cpu_data->state == THREAD_STATE_COMPLETED) {
        data->system_cpu_usage = collector->cpu_data->cpu_usage;
    }
    g_mutex_unlock(&collector->cpu_data->mutex);
    
    g_mutex_lock(&collector->memory_data->mutex);
    if (collector->memory_data->state == THREAD_STATE_COMPLETED) {
        data->system_memory_usage = collector->memory_data->memory_usage;
    }
    g_mutex_unlock(&collector->memory_data->mutex);
    
    g_mutex_lock(&collector->gpu_data->mutex);
    if (collector->gpu_data->state == THREAD_STATE_COMPLETED) {
        data->gpu_usage = g_strdup(collector->gpu_data->gpu_status);
    }
    g_mutex_unlock(&collector->gpu_data->mutex);
    
    // Merge per-process data into the process list
    if (data->processes) {
        merge_process_data(data->processes, collector->cpu_data, collector->memory_data,
                          collector->gpu_data, collector->network_data);
    }
    
    return data;
}

// Fast process list collection (just PID, name, type)
gpointer collect_process_list_thread(gpointer data) {
    ThreadedCollector *collector = (ThreadedCollector*)data;
    ProcessListResult *result = collector->process_list;
    
    g_mutex_lock(&result->mutex);
    result->state = THREAD_STATE_RUNNING;
    result->timestamp = time(NULL);
    g_mutex_unlock(&result->mutex);
    
    // Use the same top output parsing as the original for compatibility
    char* output = get_top_output();
    if (!output) {
        g_mutex_lock(&result->mutex);
        result->state = THREAD_STATE_FAILED;
        g_mutex_unlock(&result->mutex);
        return NULL;
    }
    
    // Split all lines
    char **lines = malloc(2000 * sizeof(char*));
    int total_lines = 0;
    char *line = strtok(output, "\n");
    while (line && total_lines < 1999) {
        lines[total_lines] = strdup(line);
        total_lines++;
        line = strtok(NULL, "\n");
    }

    GList *processes = NULL;
    gboolean found_header = FALSE;
    char system_summary[1024] = "";
    
    for (int line_num = 0; line_num < total_lines; line_num++) {
        line = lines[line_num];
        
        // Check for shutdown request
        g_mutex_lock(&collector->coordinator_mutex);
        gboolean shutdown = collector->shutdown_requested;
        g_mutex_unlock(&collector->coordinator_mutex);
        if (shutdown) break;
        
        // Collect and simplify system summary lines (Networks:, VM:, Disks:)
        if (strstr(line, "Networks:") || strstr(line, "VM:") || strstr(line, "Disks:")) {
            if (strlen(system_summary) > 0) strcat(system_summary, "\n");
            
            // Simplify the display text similar to original
            if (strstr(line, "Networks:")) {
                // Extract key network info
                char *net_info = line + 9; // Skip "Networks:"
                char simplified_line[256];
                snprintf(simplified_line, sizeof(simplified_line), "Networks:%s", net_info);
                strcat(system_summary, simplified_line);
            } else if (strstr(line, "VM:")) {
                // Keep VM info as-is (shows swap usage, etc.)
                strcat(system_summary, line);
            } else if (strstr(line, "Disks:")) {
                // Keep disk I/O info as-is
                strcat(system_summary, line);
            }
            continue;
        }
        
        // Look for process header line
        if (strstr(line, "PID") && strstr(line, "COMMAND")) {
            found_header = TRUE;
            continue;
        }
        
        // Process data lines (after header)
        if (found_header && line && strlen(line) > 10) {
            Process *proc = parse_process_line_basic(line);
            if (proc) {
                    
                processes = g_list_prepend(processes, proc);
            }
        }
    }
    
    // Cleanup
    for (int i = 0; i < total_lines; i++) {
        free(lines[i]);
    }
    free(lines);
    free(output);
    
    // Update result
    g_mutex_lock(&result->mutex);
    if (result->processes) {
        g_list_free_full(result->processes, (GDestroyNotify)free_process);
    }
    if (result->system_summary) {
        g_free(result->system_summary);
    }
    result->processes = g_list_reverse(processes); // Restore original order
    result->system_summary = g_strdup(system_summary);
    result->state = THREAD_STATE_COMPLETED;
    result->timestamp = time(NULL);
    g_mutex_unlock(&result->mutex);
    
    return NULL;
}

// CPU data collection
gpointer collect_cpu_data_thread(gpointer data) {
    ThreadedCollector *collector = (ThreadedCollector*)data;
    CPUDataResult *result = collector->cpu_data;
    
    g_mutex_lock(&result->mutex);
    result->state = THREAD_STATE_RUNNING;
    result->timestamp = time(NULL);
    g_mutex_unlock(&result->mutex);
    
    // Get system-wide CPU usage
    float system_cpu = get_system_cpu_usage();
    
    // Get per-process CPU usage
    g_hash_table_remove_all(result->process_cpu);
    
    FILE *fp = popen("ps -eo pid,pcpu", "r");
    if (fp) {
        char line[256];
        // Skip header
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                // Check for shutdown
                g_mutex_lock(&collector->coordinator_mutex);
                gboolean shutdown = collector->shutdown_requested;
                g_mutex_unlock(&collector->coordinator_mutex);
                if (shutdown) break;
                
                int pid;
                float cpu;
                if (sscanf(line, "%d %f", &pid, &cpu) == 2) {
                    char *pid_str = g_strdup_printf("%d", pid);
                    float *cpu_val = g_malloc(sizeof(float));
                    *cpu_val = cpu;
                    g_hash_table_insert(result->process_cpu, pid_str, cpu_val);
                }
            }
        }
        pclose(fp);
    }
    
    g_mutex_lock(&result->mutex);
    result->cpu_usage = system_cpu;
    result->state = THREAD_STATE_COMPLETED;
    result->timestamp = time(NULL);
    g_mutex_unlock(&result->mutex);
    
    return NULL;
}

// Memory data collection
gpointer collect_memory_data_thread(gpointer data) {
    ThreadedCollector *collector = (ThreadedCollector*)data;
    MemoryDataResult *result = collector->memory_data;
    
    g_mutex_lock(&result->mutex);
    result->state = THREAD_STATE_RUNNING;
    g_mutex_unlock(&result->mutex);
    
    // Get system-wide memory usage
    float system_memory = get_system_memory_usage();
    
    // Get per-process memory usage
    g_hash_table_remove_all(result->process_memory);
    
    FILE *fp = popen("ps -eo pid,rss", "r");
    if (fp) {
        char line[256];
        // Skip header
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                // Check for shutdown
                g_mutex_lock(&collector->coordinator_mutex);
                gboolean shutdown = collector->shutdown_requested;
                g_mutex_unlock(&collector->coordinator_mutex);
                if (shutdown) break;
                
                int pid;
                long rss; // RSS in KB
                if (sscanf(line, "%d %ld", &pid, &rss) == 2) {
                    char *pid_str = g_strdup_printf("%d", pid);
                    long long *bytes = g_malloc(sizeof(long long));
                    *bytes = rss * 1024; // Convert KB to bytes
                    g_hash_table_insert(result->process_memory, pid_str, bytes);
                }
            }
        }
        pclose(fp);
    }
    
    g_mutex_lock(&result->mutex);
    result->memory_usage = system_memory;
    result->state = THREAD_STATE_COMPLETED;
    result->timestamp = time(NULL);
    g_mutex_unlock(&result->mutex);
    
    return NULL;
}

// GPU data collection (slowest operation)
gpointer collect_gpu_data_thread(gpointer data) {
    ThreadedCollector *collector = (ThreadedCollector*)data;
    GPUDataResult *result = collector->gpu_data;
    
    g_mutex_lock(&result->mutex);
    result->state = THREAD_STATE_RUNNING;
    g_mutex_unlock(&result->mutex);
    
    // Get GPU usage (with fallback)
    char *gpu_status = get_gpu_usage();
    float gpu_percent = 0.0;
    
    if (gpu_status && sscanf(gpu_status, "%f%%", &gpu_percent) == 1) {
        // Successfully parsed percentage
    } else {
        gpu_percent = 0.0;
        if (gpu_status) {
            g_free(gpu_status);
            gpu_status = g_strdup("N/A");
        }
    }
    
    g_mutex_lock(&result->mutex);
    if (gpu_status) {
        safe_strncpy(result->gpu_status, gpu_status, sizeof(result->gpu_status));
        g_free(gpu_status);
    } else {
        safe_strncpy(result->gpu_status, "N/A", sizeof(result->gpu_status));
    }
    result->gpu_percentage = gpu_percent;
    result->state = THREAD_STATE_COMPLETED;
    result->timestamp = time(NULL);
    g_mutex_unlock(&result->mutex);
    
    return NULL;
}

// Network data collection (slow operation)
gpointer collect_network_data_thread(gpointer data) {
    ThreadedCollector *collector = (ThreadedCollector*)data;
    NetworkDataResult *result = collector->network_data;
    
    g_mutex_lock(&result->mutex);
    result->state = THREAD_STATE_RUNNING;
    g_mutex_unlock(&result->mutex);
    
    // Clear previous data
    g_hash_table_remove_all(result->process_network);
    
    // Collect network data directly using nettop
    FILE *fp = popen("nettop -P -L1 -x", "r");  // Use -x for better parsing
    if (fp) {
        char line[1024];
        double current_time = (double)g_get_real_time() / 1000000.0;
        
        // Skip header lines
        int skip_lines = 0;
        while (fgets(line, sizeof(line), fp) && skip_lines < 2) {
            skip_lines++;
        }
        
        // Process each line
        while (fgets(line, sizeof(line), fp)) {
            // Check for shutdown
            g_mutex_lock(&collector->coordinator_mutex);
            gboolean shutdown = collector->shutdown_requested;
            g_mutex_unlock(&collector->coordinator_mutex);
            if (shutdown) break;
            
            // Parse nettop CSV: time,processname.pid,interface,state,bytes_in,bytes_out,...
            // Split by commas
            char *fields[8];
            int field_count = 0;
            char *pos = line;
            
            while (*pos && field_count < 8) {
                fields[field_count] = pos;
                while (*pos && *pos != ',') pos++;
                if (*pos) {
                    *pos = '\0';
                    pos++;
                }
                field_count++;
            }
            
            if (field_count < 6) continue;
            
            // Extract PID from "processname.pid" format (field 1)
            char *process_pid = fields[1];
            char *dot = strrchr(process_pid, '.');  // Find last dot
            if (!dot) continue;
            
            char *pid_str = dot + 1;  // PID is after the dot
            char *bytes_in_str = fields[4];
            char *bytes_out_str = fields[5];
            long long bytes_in = atoll(bytes_in_str);
            long long bytes_out = atoll(bytes_out_str);
            long long total_bytes = bytes_in + bytes_out;
            
            if (total_bytes > 0 && strlen(pid_str) > 0) {
                // Calculate rate if we have previous data
                double rate = 0.0;
                if (g_hash_table_contains(result->prev_net_bytes, pid_str)) {
                    long long *prev_bytes_ptr = g_hash_table_lookup(result->prev_net_bytes, pid_str);
                    double *prev_time_ptr = g_hash_table_lookup(result->prev_times, pid_str);
                    
                    if (prev_bytes_ptr && prev_time_ptr) {
                        double time_diff = current_time - *prev_time_ptr;
                        if (time_diff > 0.3) {  // Need at least 0.3 seconds
                            long long bytes_diff = total_bytes - *prev_bytes_ptr;
                            if (bytes_diff > 0) {
                                rate = (double)bytes_diff / time_diff / 1024.0;  // KB/s
                            }
                        }
                    }
                }
                
                // Update previous values
                long long *new_bytes = g_malloc(sizeof(long long));
                *new_bytes = total_bytes;
                g_hash_table_insert(result->prev_net_bytes, g_strdup(pid_str), new_bytes);
                
                double *new_time = g_malloc(sizeof(double));
                *new_time = current_time;
                g_hash_table_insert(result->prev_times, g_strdup(pid_str), new_time);
                
                // Store rate for this process
                char rate_str[32];
                if (rate > 0.1) {
                    snprintf(rate_str, sizeof(rate_str), "%.1f KB/s", rate);
                } else {
                    snprintf(rate_str, sizeof(rate_str), "0.0 KB/s");
                }
                
                g_hash_table_insert(result->process_network, 
                                   g_strdup(pid_str), 
                                   g_strdup(rate_str));
            }
        }
        pclose(fp);
    }
    
    g_mutex_lock(&result->mutex);
    result->state = THREAD_STATE_COMPLETED;
    result->timestamp = time(NULL);
    g_mutex_unlock(&result->mutex);
    
    return NULL;
}

// Merge collected data into process structures
void merge_process_data(GList *processes, CPUDataResult *cpu, MemoryDataResult *memory, 
                       GPUDataResult *gpu, NetworkDataResult *network) {
    if (!processes) return;
    
    for (GList *l = processes; l != NULL; l = l->next) {
        Process *proc = (Process*)l->data;
        if (!proc) continue;
        
        // Update CPU data
        if (cpu && cpu->state == THREAD_STATE_COMPLETED) {
            g_mutex_lock(&cpu->mutex);
            float *cpu_val = g_hash_table_lookup(cpu->process_cpu, proc->pid);
            if (cpu_val) {
                snprintf(proc->cpu, sizeof(proc->cpu), "%.1f", *cpu_val);
            }
            g_mutex_unlock(&cpu->mutex);
        }
        
        // Update Memory data
        if (memory && memory->state == THREAD_STATE_COMPLETED) {
            g_mutex_lock(&memory->mutex);
            long long *mem_bytes = g_hash_table_lookup(memory->process_memory, proc->pid);
            if (mem_bytes) {
                char *mem_str = format_bytes_human_readable(*mem_bytes);
                safe_strncpy(proc->mem, mem_str, sizeof(proc->mem));
                g_free(mem_str);
            }
            g_mutex_unlock(&memory->mutex);
        }
        
        // Update GPU data (system-wide, same for all processes)
        if (gpu && gpu->state == THREAD_STATE_COMPLETED) {
            g_mutex_lock(&gpu->mutex);
            safe_strncpy(proc->gpu, gpu->gpu_status, sizeof(proc->gpu));
            g_mutex_unlock(&gpu->mutex);
        }
        
        // Network data (when available)
        if (network && network->state == THREAD_STATE_COMPLETED) {
            g_mutex_lock(&network->mutex);
            char *net_rate = g_hash_table_lookup(network->process_network, proc->pid);
            if (net_rate) {
                safe_strncpy(proc->net, net_rate, sizeof(proc->net));
            } else {
                // Set default network rate for processes without specific data
                safe_strncpy(proc->net, "0.0 KB/s", sizeof(proc->net));
            }
            g_mutex_unlock(&network->mutex);
        }
    }
}

// Cleanup functions
void cleanup_process_list_result(ProcessListResult *result) {
    if (!result) return;
    g_mutex_lock(&result->mutex);
    if (result->processes) {
        g_list_free_full(result->processes, (GDestroyNotify)free_process);
        result->processes = NULL;
    }
    if (result->system_summary) {
        g_free(result->system_summary);
        result->system_summary = NULL;
    }
    g_mutex_unlock(&result->mutex);
    g_mutex_clear(&result->mutex);
    g_free(result);
}

void cleanup_cpu_data_result(CPUDataResult *result) {
    if (!result) return;
    g_mutex_lock(&result->mutex);
    if (result->process_cpu) {
        g_hash_table_destroy(result->process_cpu);
        result->process_cpu = NULL;
    }
    g_mutex_unlock(&result->mutex);
    g_mutex_clear(&result->mutex);
    g_free(result);
}

void cleanup_memory_data_result(MemoryDataResult *result) {
    if (!result) return;
    g_mutex_lock(&result->mutex);
    if (result->process_memory) {
        g_hash_table_destroy(result->process_memory);
        result->process_memory = NULL;
    }
    g_mutex_unlock(&result->mutex);
    g_mutex_clear(&result->mutex);
    g_free(result);
}

void cleanup_gpu_data_result(GPUDataResult *result) {
    if (!result) return;
    g_mutex_clear(&result->mutex);
    g_free(result);
}

void cleanup_network_data_result(NetworkDataResult *result) {
    if (!result) return;
    g_mutex_lock(&result->mutex);
    if (result->process_network) {
        g_hash_table_destroy(result->process_network);
        result->process_network = NULL;
    }
    if (result->prev_net_bytes) {
        g_hash_table_destroy(result->prev_net_bytes);
        result->prev_net_bytes = NULL;
    }
    if (result->prev_times) {
        g_hash_table_destroy(result->prev_times);
        result->prev_times = NULL;
    }
    g_mutex_unlock(&result->mutex);
    g_mutex_clear(&result->mutex);
    g_free(result);
}

// ============================================================================
// COLLECTOR/BIN SYSTEM - Efficient background data collection
// ============================================================================

// Background collector thread that continuously updates the data bin
// Synchronous data collection function (based on update_thread_func but without GUI)
UpdateData* collect_complete_data_sync(void) {
    // SECURITY: Track timing for timeout protection  
    time_t update_start_time = time(NULL);
    
    // Get process data using the same logic as update_thread_func
    char* output = get_top_output();
    if (!output) {
        return NULL;
    }

    // Split all lines
    char **lines = NULL;
    int total_lines = 0;
    char *token = strtok(output, "\n");
    while (token != NULL) {
        lines = realloc(lines, sizeof(char*) * (total_lines + 1));
        lines[total_lines] = strdup(token);
        total_lines++;
        token = strtok(NULL, "\n");
    }

    GList *processes = NULL;
    char summary_buffer[2048] = "";
    gboolean found_header = FALSE;
    int process_count = 0;
    
    // Use limits from config.h

    // Process lines using the same logic as update_thread_func
    for (int i = 0; i < total_lines; i++) {
        char *line = lines[i];
        
        // Build summary buffer from initial system info lines
        if (i < 15 && (strstr(line, "Processes:") || strstr(line, "Load Avg:") || 
                       strstr(line, "CPU usage:") || strstr(line, "PhysMem:") ||
                       strstr(line, "Networks:") || strstr(line, "VM:") || 
                       strstr(line, "Disks:"))) {
            
            // System-friendly simplification (same as original)
            char simplified_line[256];
            if (strstr(line, "Networks:")) {
                // Parse network info
                char *net_info = line + 9; // Skip "Networks:"
                char *read_pos = strstr(net_info, "packets:");
                char *write_pos = strstr(net_info, "data received");
                
                char in_amount[32] = "";
                
                if (read_pos && write_pos) {
                    // Extract amounts using similar logic as original
                    char *slash_pos = strstr(write_pos, "/");
                    if (slash_pos) {
                        char *space_pos = strstr(slash_pos, " ");
                        if (space_pos) {
                            int len = space_pos - slash_pos - 1;
                            if (len > 0 && len < 31) {
                                strncpy(in_amount, slash_pos + 1, len);
                                in_amount[len] = '\0';
                            }
                        }
                    }
                }
                
                if (strlen(in_amount) > 0) {
                    snprintf(simplified_line, sizeof(simplified_line), "Network: %s received", in_amount);
                } else {
                    sprintf(simplified_line, "Network: Active");
                }
            } else if (strstr(line, "VM:")) {
                // Simplified VM info
                sprintf(simplified_line, "Virtual Memory: Active");
            } else if (strstr(line, "Disks:")) {
                // Simplified disk info
                sprintf(simplified_line, "Disk Activity: Active");
            } else {
                // Use line as-is for other system info
                strncpy(simplified_line, line, 255);
                simplified_line[255] = '\0';
            }
            
            strncat(summary_buffer, simplified_line, sizeof(summary_buffer) - strlen(summary_buffer) - 2);
            if (strlen(summary_buffer) < sizeof(summary_buffer) - 1) {
                strcat(summary_buffer, "\n");
            }
        }
        
        // Look for the PID COMMAND header to know when process lines start
        if (strstr(line, "PID") && strstr(line, "COMMAND")) {
            found_header = TRUE;
            continue;
        }
        
        // Only parse process lines after we've found the header
        if (found_header && line && *line != '\0') {
            // SECURITY: Check timeout and resource limits (same as original)
            time_t now = time(NULL);
            if ((now - update_start_time) > (MAX_UPDATE_TIME_MS / 1000)) {
                break;  // Timeout protection
            }
            
            if (process_count >= MAX_PROCESSES_PER_UPDATE) {
                break;  // Process count limit
            }
            
            // Only parse lines that start with a digit (actual PIDs)
            if (isdigit(line[0])) {
                process_count++;
                Process *proc = alloc_process();
                
                // Use the same parsing logic as original
                char *tokens[20];
                int token_count = 0;
                char *line_copy = strdup(line);
                char *token_ptr = strtok(line_copy, " \t");
                
                while (token_ptr && token_count < 20) {
                    tokens[token_count++] = strdup(token_ptr);
                    token_ptr = strtok(NULL, " \t");
                }
                
                if (token_count >= 5) {
                    // Parse PID
                    safe_strncpy(proc->pid, tokens[0], sizeof(proc->pid));
                    
                    // Parse CPU (same normalization as original)
                    if (tokens[token_count - 3]) {
                        float raw_cpu = atof(tokens[token_count - 3]);
                        if (raw_cpu < 0) raw_cpu = 0;
                        if (raw_cpu > 999.9) raw_cpu = 999.9;
                        
                        extern int cpu_cores; // From system.h
                        float normalized_cpu = raw_cpu / (cpu_cores > 0 ? cpu_cores : 1);
                        snprintf(proc->cpu, sizeof(proc->cpu), "%.1f", normalized_cpu);
                    } else {
                        safe_strncpy(proc->cpu, "0.0", sizeof(proc->cpu));
                    }
                    
                    // Parse memory
                    if (tokens[token_count - 2]) {
                        char *formatted_mem = format_memory_human_readable(tokens[token_count - 2]);
                        if (formatted_mem) {
                            safe_strncpy(proc->mem, formatted_mem, sizeof(proc->mem));
                            free(formatted_mem);
                        } else {
                            safe_strncpy(proc->mem, "0 B", sizeof(proc->mem));
                        }
                    } else {
                        safe_strncpy(proc->mem, "0 B", sizeof(proc->mem));
                    }
                    
                    // Parse runtime
                    if (tokens[token_count - 1]) {
                        safe_strncpy(proc->runtime, tokens[token_count - 1], sizeof(proc->runtime));
                    } else {
                        safe_strncpy(proc->runtime, "0:00", sizeof(proc->runtime));
                    }
                    
                    // Build command name
                    proc->name[0] = '\0';
                    for (int j = 1; j < token_count - 3 && j < 10; j++) {
                        if (tokens[j]) {
                            if (j > 1) safe_strncat(proc->name, " ", sizeof(proc->name));
                            safe_strncat(proc->name, tokens[j], sizeof(proc->name));
                        }
                    }
                    
                    // Clean up tokens
                    for (int j = 0; j < token_count; j++) {
                        free(tokens[j]);
                    }
                    
                    // Get runtime
                    char *runtime = get_run_time(proc->pid);
                    safe_strncpy(proc->runtime, runtime, sizeof(proc->runtime));
                    free(runtime);
                    
                    // Set defaults for GPU and network (will be filled later if needed)
                    safe_strncpy(proc->gpu, "N/A", sizeof(proc->gpu));
                    safe_strncpy(proc->net, "0.0 KB/s", sizeof(proc->net));
                    
                    // Determine process type
                    determine_process_type(proc);
                    
                    processes = g_list_append(processes, proc);
                } else {
                    // Clean up on parsing error
                    for (int j = 0; j < token_count; j++) {
                        free(tokens[j]);
                    }
                    free_process(proc);
                }
                
                free(line_copy);
            }
        }
    }
    
    // Free the lines array
    for (int i = 0; i < total_lines; i++) {
        free(lines[i]);
    }
    free(lines);
    free(output);

    // Get additional data
    char *gpu_usage = get_gpu_usage();

    // Create UpdateData structure
    UpdateData *update_data = g_malloc0(sizeof(UpdateData));
    if (!update_data) {
        g_list_free_full(processes, (GDestroyNotify)free_process);
        if (gpu_usage) free(gpu_usage);
        return NULL;
    }
    
    update_data->processes = processes;
    update_data->gpu_usage = gpu_usage ? gpu_usage : g_strdup("N/A");
    update_data->system_summary = g_strdup(summary_buffer);
    
    // Use system usage collection
    update_data->system_cpu_usage = get_system_cpu_usage();
    update_data->system_memory_usage = get_system_memory_usage();

    return update_data;
}

gpointer continuous_collector_thread(gpointer data) {
    ThreadedCollector *collector = (ThreadedCollector*)data;
    
    while (!collector->shutdown_requested) {
        // Collect all data using our new synchronous function
        UpdateData *new_data = collect_complete_data_sync();
        
        if (new_data && !collector->shutdown_requested) {
            // Update the data bin (thread-safe)
            g_mutex_lock(&collector->bin_mutex);
            
            // Free old data if it exists
            if (collector->data_bin) {
                free_update_data(collector->data_bin);
            }
            
            // Store the new complete dataset
            collector->data_bin = new_data;
            
            g_mutex_unlock(&collector->bin_mutex);
        }
        
        // Sleep before next collection cycle
        if (!collector->shutdown_requested) {
            g_usleep(1500000); // 1.5 seconds between collections
        }
    }
    
    return NULL;
}

// Start continuous background data collection
void threaded_collector_start_continuous_collection(ThreadedCollector *collector) {
    if (!collector || collector->continuous_mode) return;
    
    collector->continuous_mode = TRUE;
    collector->shutdown_requested = FALSE;
    
    // Start the background collector thread
    collector->collector_thread = g_thread_new("continuous_collector", 
                                               continuous_collector_thread, 
                                               collector);
}

// Get the latest complete data from the bin (fast operation)
UpdateData* threaded_collector_get_latest_complete_data(ThreadedCollector *collector) {
    if (!collector) return NULL;
    
    UpdateData *data_copy = NULL;
    
    g_mutex_lock(&collector->bin_mutex);
    
    if (collector->data_bin) {
        // Create a deep copy of the current data
        data_copy = g_malloc0(sizeof(UpdateData));
        
        // Copy system-wide values
        data_copy->system_cpu_usage = collector->data_bin->system_cpu_usage;
        data_copy->system_memory_usage = collector->data_bin->system_memory_usage;
        
        // Deep copy process list
        if (collector->data_bin->processes) {
            data_copy->processes = g_list_copy_deep(collector->data_bin->processes, 
                                                   (GCopyFunc)copy_process, NULL);
        }
        
        // Copy string data
        if (collector->data_bin->system_summary) {
            data_copy->system_summary = g_strdup(collector->data_bin->system_summary);
        }
        if (collector->data_bin->gpu_usage) {
            data_copy->gpu_usage = g_strdup(collector->data_bin->gpu_usage);
        }
    }
    
    g_mutex_unlock(&collector->bin_mutex);
    
    return data_copy;
}
