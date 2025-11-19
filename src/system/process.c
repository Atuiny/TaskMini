#include "system.h" 
#include "performance.h"
#include "../ui/ui.h"
#include "../utils/utils.h"
#include "../common/config.h"
#include <time.h>
#include <ctype.h>

// SECURITY: Thread monitoring and timeout protection
gboolean update_thread_running = FALSE;
time_t update_start_time = 0;

// Forward declaration
extern long long prev_system_bytes_in;
extern long long prev_system_bytes_out; 
extern double prev_system_time;

// New function for the background thread. SECURED with timeout and resource limits.
gpointer update_thread_func(gpointer data) {
    (void)data; // Suppress unused parameter warning
    update_thread_running = TRUE;
    update_start_time = time(NULL);
    
    // SECURITY: Check for excessive consecutive failures
    if (consecutive_failures >= MAX_FAILURES) {
        g_usleep(1000000);  // Sleep 1 second to prevent CPU spinning
        consecutive_failures = 0;  // Reset after backoff
    }
    
    char* output = get_top_output();
    
    // Split all lines at once to avoid strtok conflicts with other functions
    char **lines = malloc(2000 * sizeof(char*));  // Should be enough for all lines
    int total_lines = 0;
    char *line = strtok(output, "\n");
    while (line && total_lines < 1999) {
        lines[total_lines] = strdup(line);
        total_lines++;
        line = strtok(NULL, "\n");
    }

    double current_time = (double)g_get_real_time() / 1000000.0;

    GList *processes = NULL;
    char summary_buffer[1024] = "";
    gboolean found_header = FALSE;
    int process_count = 0;  // SECURITY: Track process count
    
    for (int line_num = 0; line_num < total_lines; line_num++) {
        line = lines[line_num];
        
        // Collect and simplify system summary lines (Networks:, VM:, Disks:)
        if (strstr(line, "Networks:") || strstr(line, "VM:") || strstr(line, "Disks:")) {
            if (strlen(summary_buffer) > 0) strcat(summary_buffer, "\n");
            
            // Simplify the display text
            char simplified_line[256];
            if (strstr(line, "Networks:")) {
                // Parse network info - format: "Networks: packets: 21060567/26G in, 7375591/1598M out."
                char *net_info = line + 9; // Skip "Networks:"
                
                // Look for the data amounts after the packet counts
                char *in_pos = strstr(net_info, "/");
                
                char in_amount[32] = "";
                char out_amount[32] = "";
                long long in_bytes = 0, out_bytes = 0;
                
                if (in_pos) {
                    // Extract "in" amount - get everything between "/" and " in"
                    char *in_end = strstr(in_pos, " in");
                    if (in_end) {
                        int len = in_end - in_pos - 1; // Skip the "/"
                        if (len > 0 && len < 31) {
                            strncpy(in_amount, in_pos + 1, len);
                            in_amount[len] = '\0';
                            in_bytes = parse_memory_string(in_amount);
                        }
                        
                        // Look for the "out" amount after "in,"
                        char *out_start = strstr(in_end, ", ");
                        if (out_start) {
                            out_start += 2; // Skip ", "
                            char *out_slash = strstr(out_start, "/");
                            if (out_slash) {
                                char *out_end = strstr(out_slash, " out");
                                if (out_end) {
                                    int out_len = out_end - out_slash - 1;
                                    if (out_len > 0 && out_len < 31) {
                                        strncpy(out_amount, out_slash + 1, out_len);
                                        out_amount[out_len] = '\0';
                                        out_bytes = parse_memory_string(out_amount);
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Calculate rates if we have previous data
                double current_time = g_get_monotonic_time() / 1000000.0;
                char rate_info[128] = "";
                
                if (prev_system_bytes_in > 0 && prev_system_time > 0.0) {
                    double time_diff = current_time - prev_system_time;
                    if (time_diff > 0) {
                        double rate_in = (in_bytes - prev_system_bytes_in) / time_diff;
                        double rate_out = (out_bytes - prev_system_bytes_out) / time_diff;
                        
                        // Only show rates if they're reasonable (not negative due to resets)
                        if (rate_in >= 0 && rate_out >= 0) {
                            char *rate_in_str = format_bytes_human_readable((long long)rate_in);
                            char *rate_out_str = format_bytes_human_readable((long long)rate_out);
                            snprintf(rate_info, sizeof(rate_info), " (↓%s/s ↑%s/s)", rate_in_str, rate_out_str);
                            g_free(rate_in_str);
                            g_free(rate_out_str);
                        }
                    }
                }
                
                // Update previous values
                prev_system_bytes_in = in_bytes;
                prev_system_bytes_out = out_bytes;
                prev_system_time = current_time;
                
                // Build the simplified line with cumulative totals and rates
                if (strlen(in_amount) > 0 && strlen(out_amount) > 0) {
                    snprintf(simplified_line, sizeof(simplified_line), "Network: %s downloaded, %s uploaded%s", 
                            in_amount, out_amount, rate_info);
                } else if (strlen(in_amount) > 0) {
                    snprintf(simplified_line, sizeof(simplified_line), "Network: %s downloaded%s", 
                            in_amount, rate_info);
                } else {
                    sprintf(simplified_line, "Network: Active");
                }
            } else if (strstr(line, "VM:")) {
                // Parse and simplify virtual memory info for regular users
                char *vm_info = line + 3; // Skip "VM:"
                
                // Look for vsize (total virtual memory allocated by all processes)
                char *vsize_pos = strstr(vm_info, " vsize");
                char total_allocated[32] = "";
                
                if (vsize_pos) {
                    // Work backwards to find the size before " vsize"
                    char *size_start = vsize_pos;
                    while (size_start > vm_info && *(size_start-1) != ' ') {
                        size_start--;
                    }
                    if (size_start < vsize_pos) {
                        int len = vsize_pos - size_start;
                        if (len > 0 && len < 31) {
                            strncpy(total_allocated, size_start, len);
                            total_allocated[len] = '\0';
                        }
                    }
                }
                
                // Look for framework vsize (memory used by system frameworks)
                char *framework_pos = strstr(vm_info, " framework vsize");
                char framework_mem[32] = "";
                
                if (framework_pos) {
                    // Work backwards to find the size before " framework vsize"
                    char *size_start = framework_pos;
                    while (size_start > vm_info && *(size_start-1) != ' ') {
                        size_start--;
                    }
                    if (size_start < framework_pos) {
                        int len = framework_pos - size_start;
                        if (len > 0 && len < 31) {
                            strncpy(framework_mem, size_start, len);
                            framework_mem[len] = '\0';
                        }
                    }
                }
                
                // Parse swap numbers more accurately
                char *swapins_str = strstr(vm_info, " swapins");
                char *swapouts_str = strstr(vm_info, " swapouts");
                long long actual_swapins = 0, actual_swapouts = 0;
                
                if (swapins_str) {
                    // Look for the number before " swapins"
                    char *num_end = swapins_str;
                    char *num_start = num_end - 1;
                    while (num_start > vm_info && isdigit(*num_start)) {
                        num_start--;
                    }
                    if (num_start < num_end && isdigit(*(num_start + 1))) {
                        actual_swapins = strtoll(num_start + 1, NULL, 10);
                    }
                }
                
                if (swapouts_str) {
                    // Look for the number before " swapouts"
                    char *num_end = swapouts_str;
                    char *num_start = num_end - 1;
                    while (num_start > vm_info && isdigit(*num_start)) {
                        num_start--;
                    }
                    if (num_start < num_end && isdigit(*(num_start + 1))) {
                        actual_swapouts = strtoll(num_start + 1, NULL, 10);
                    }
                }
                
                // Show actual swap numbers with simplified format
                char swap_status[100] = "";
                if (actual_swapins > 0 || actual_swapouts > 0) {
                    sprintf(swap_status, " (Swap-ins: %lld, Swap-outs: %lld)", actual_swapins, actual_swapouts);
                } else {
                    sprintf(swap_status, " (Swap-ins: 0, Swap-outs: 0)");
                }
                
                if (strlen(total_allocated) > 0 && strlen(framework_mem) > 0) {
                    // Make it clear this is address space, not actual RAM usage                   
                    sprintf(simplified_line, "Virtual Memory: %s address space for apps, %s for system%s", 
                           total_allocated, framework_mem, swap_status);
                } else if (strlen(total_allocated) > 0) {
                    sprintf(simplified_line, "Virtual Memory: %s address space reserved (not actual RAM used)%s", 
                           total_allocated, swap_status);
                } else {
                    sprintf(simplified_line, "Virtual Memory: System managing address space%s", swap_status);
                }
            } else if (strstr(line, "Disks:")) {
                // Parse disk activity - format: "Disks: 32434568/892G read, 14229951/334G written."
                char *disk_info = line + 6; // Skip "Disks:"
                
                // Look for read amount - format: "number/sizeG read"
                char *read_slash = strstr(disk_info, "/");
                char *write_pos = strstr(disk_info, "written");
                
                char read_amount[32] = "";
                char write_amount[32] = "";
                
                if (read_slash) {
                    // Extract read amount - get everything between "/" and " read"
                    char *read_end = strstr(read_slash, " read");
                    if (read_end) {
                        int len = read_end - read_slash - 1; // Skip the "/"
                        if (len > 0 && len < 31) {
                            strncpy(read_amount, read_slash + 1, len);
                            read_amount[len] = '\0';
                        }
                    }
                }
                
                if (write_pos) {
                    // Look backwards from "written" to find the slash
                    char *write_search = write_pos;
                    while (write_search > disk_info && *write_search != '/') {
                        write_search--;
                    }
                    if (*write_search == '/') {
                        // Extract write amount - get everything between "/" and " written"
                        int len = write_pos - write_search - 1;
                        if (len > 0 && len < 31) {
                            strncpy(write_amount, write_search + 1, len);
                            write_amount[len] = '\0';
                        }
                    }
                }
                
                // Build the simplified line
                if (strlen(read_amount) > 0 && strlen(write_amount) > 0) {
                    sprintf(simplified_line, "Disk Activity: %s read, %s written", read_amount, write_amount);
                } else if (strlen(read_amount) > 0) {
                    sprintf(simplified_line, "Disk Activity: %s read", read_amount);
                } else if (strlen(write_amount) > 0) {
                    sprintf(simplified_line, "Disk Activity: %s written", write_amount);
                } else {
                    sprintf(simplified_line, "Disk Activity: Active");
                }
            } else {
                // For other lines, just use them as-is
                strncpy(simplified_line, line, 255);
                simplified_line[255] = '\0';
            }
            
            strcat(summary_buffer, simplified_line);
        }
        
        // Look for the PID COMMAND header to know when process lines start
        // We want the SECOND header (from the second sample) for better accuracy
        if (strstr(line, "PID") && strstr(line, "COMMAND")) {
            found_header = TRUE;
            continue;
        }
        
        // Only parse process lines after we've found the header
        if (found_header && line && *line != '\0') {
            // SECURITY: Check timeout and resource limits
            time_t now = time(NULL);
            if ((now - update_start_time) > (MAX_UPDATE_TIME_MS / 1000)) {
                break;  // Timeout protection
            }
            
            if (process_count >= MAX_PROCESSES_PER_UPDATE) {
                break;  // Process count limit
            }
            
            // Only parse lines that start with a digit (actual PIDs)
            if (isdigit(line[0])) {
                process_count++;  // SECURITY: Increment counter
                Process *proc = alloc_process();  // OPTIMIZATION: Use memory pool
                
                // Split line into tokens and parse from the end
                char *tokens[20];  // Should be enough for any reasonable line
                int token_count = 0;
                char *line_copy = strdup(line);
                char *token = strtok(line_copy, " \t");
                
                while (token && token_count < 20) {
                    tokens[token_count++] = strdup(token);
                    token = strtok(NULL, " \t");
                }
                
                if (token_count >= 5) {  // Need at least PID, NAME, CPU, MEM, TIME
                    // SECURITY: Safe PID copying with bounds checking
                    safe_strncpy(proc->pid, tokens[0], sizeof(proc->pid));
                    
                    // SECURITY: Validate CPU value before processing
                    if (tokens[token_count - 3]) {
                        float raw_cpu = atof(tokens[token_count - 3]);
                        // SECURITY: Clamp CPU values to reasonable range
                        if (raw_cpu < 0) raw_cpu = 0;
                        if (raw_cpu > 999.9) raw_cpu = 999.9;
                        
                        float normalized_cpu = raw_cpu / (cpu_cores > 0 ? cpu_cores : 1);
                        snprintf(proc->cpu, sizeof(proc->cpu), "%.1f", normalized_cpu);
                    } else {
                        safe_strncpy(proc->cpu, "0.0", sizeof(proc->cpu));
                    }
                    
                    // SECURITY: Safe memory format conversion
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
                    
                    // SECURITY: Safe runtime copying
                    if (tokens[token_count - 1]) {
                        safe_strncpy(proc->runtime, tokens[token_count - 1], sizeof(proc->runtime));
                    } else {
                        safe_strncpy(proc->runtime, "0:00", sizeof(proc->runtime));
                    }

                    // SECURITY: Safe command name construction with bounds checking
                    proc->name[0] = '\0';
                    for (int i = 1; i < token_count - 3 && i < 10; i++) {  // Limit iterations
                        if (tokens[i]) {
                            if (i > 1) safe_strncat(proc->name, " ", sizeof(proc->name));
                            safe_strncat(proc->name, tokens[i], sizeof(proc->name));
                        }
                    }
                    
                    // Clean up tokens
                    for (int i = 0; i < token_count; i++) {
                        free(tokens[i]);
                    }
                } else {
                    // Clean up tokens on error
                    for (int i = 0; i < token_count; i++) {
                        free(tokens[i]);
                    }
                    free(line_copy);
                    free_process(proc);
                    continue;
                }
                
                free(line_copy);
                
                // Override runtime with elapsed time
                char *runtime = get_run_time(proc->pid);
                safe_strncpy(proc->runtime, runtime, sizeof(proc->runtime));
                free(runtime);

                // GPU: N/A (per-process not easily available)
                safe_strncpy(proc->gpu, "N/A", sizeof(proc->gpu));
                
                // Determine process type (system vs user)
                determine_process_type(proc);

                // Network rate (simplified for now)
                long long current_bytes = get_net_bytes(proc->pid);
                char *pid_key = g_strdup(proc->pid);
                double rate = 0.0;

                g_mutex_lock(&hash_mutex);
                if (g_hash_table_contains(prev_net_bytes, pid_key)) {
                    long long *prev_b_ptr = (long long *)g_hash_table_lookup(prev_net_bytes, pid_key);
                    double *prev_t_ptr = (double *)g_hash_table_lookup(prev_times, pid_key);
                    if (prev_b_ptr && prev_t_ptr) {
                        double delta_time = current_time - *prev_t_ptr;
                        if (delta_time > 0.3) {  // Need at least 0.3 seconds for meaningful rate
                            long long bytes_diff = current_bytes - *prev_b_ptr;
                            if (bytes_diff > 0) {  // Only show positive rates
                                rate = (double)bytes_diff / delta_time / 1024.0;  // KB/s
                            }
                        }
                    }
                }
                
                // Update prev values
                long long *new_bytes = malloc(sizeof(long long));
                *new_bytes = current_bytes;
                g_hash_table_insert(prev_net_bytes, g_strdup(pid_key), new_bytes);

                double *new_time = malloc(sizeof(double));
                *new_time = current_time;
                g_hash_table_insert(prev_times, g_strdup(pid_key), new_time);
                g_mutex_unlock(&hash_mutex);

                if (rate > 0.1) {  // Only show rates above 0.1 KB/s
                    sprintf(proc->net, "%.1f KB/s", rate);
                } else {
                    sprintf(proc->net, "0.0 KB/s");
                }

                g_free(pid_key);

                // Add to list
                processes = g_list_append(processes, proc);
            }
        }
    }
    
    // Free the lines array
    for (int i = 0; i < total_lines; i++) {
        free(lines[i]);
    }
    free(lines);

    free(output);

    // SECURITY: Update success/failure tracking
    int final_process_count = g_list_length(processes);
    if (final_process_count > 0) {
        consecutive_failures = 0;  // Reset on success
    } else {
        consecutive_failures++;   // Increment on failure
    }

    char *gpu_usage = get_gpu_usage();

    // SECURITY: Validate allocations before use
    UpdateData *update_data = malloc(sizeof(UpdateData));
    if (!update_data) {
        // Cleanup on allocation failure
        g_list_free_full(processes, (GDestroyNotify)free_process);
        if (gpu_usage) free(gpu_usage);
        update_thread_running = FALSE;
        return NULL;
    }
    
    update_data->processes = processes;
    update_data->gpu_usage = gpu_usage ? gpu_usage : strdup("N/A");
    update_data->system_summary = strdup(summary_buffer);
    
    // Use optimized system usage collection (with fallback)
    update_data->system_cpu_usage = get_system_cpu_usage();
    update_data->system_memory_usage = get_system_memory_usage();

    g_idle_add(update_ui_func, update_data);
    
    update_thread_running = FALSE;
    return NULL;
}
