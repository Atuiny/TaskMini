#include "system.h"
#include "../utils/utils.h"
#include "../common/config.h"
#include <time.h>

// OPTIMIZATION: GPU detection caching and state management
gboolean powermetrics_unavailable = FALSE;
char *cached_gpu_result = NULL;
time_t last_gpu_check = 0;
int gpu_check_interval = GPU_CHECK_INTERVAL;

// Forward declaration
char* get_gpu_usage_fallback(void);

// OPTIMIZATION: GPU usage with caching and reduced system calls
char* get_gpu_usage(void) {
    time_t now = time(NULL);
    
    // OPTIMIZATION: Return cached result if still fresh
    if (cached_gpu_result && (now - last_gpu_check) < gpu_check_interval) {
        return strdup(cached_gpu_result);
    }
    
    // Clear old cache
    if (cached_gpu_result) {
        free(cached_gpu_result);
        cached_gpu_result = NULL;
    }
    
    // If we've already determined powermetrics won't work, use fallback immediately
    if (powermetrics_unavailable) {
        cached_gpu_result = get_gpu_usage_fallback();
        last_gpu_check = now;
        return strdup(cached_gpu_result);
    }
    
    // Try powermetrics once - if it fails, mark it as unavailable and use fallback
    char *output = get_full_output("powermetrics --samplers gpu_power -n1 -i100 2>/dev/null");
    if (!output || strstr(output, "must be invoked as the superuser") || strlen(output) < 10) {
        if (output) free(output);
        powermetrics_unavailable = TRUE;  // Mark as unavailable permanently
        return get_gpu_usage_fallback();
    }

    char *pos = strstr(output, "GPU active residency:");
    if (pos) {
        pos += strlen("GPU active residency:");
        float perc = 0.0;
        if (sscanf(pos, " %f%%", &perc) == 1) {
            char *result = malloc(20);
            sprintf(result, "%.2f%%", perc);
            free(output);
            
            // Cache the result
            cached_gpu_result = strdup(result);
            last_gpu_check = now;
            return result;
        }
    }
    
    free(output);
    // If parsing failed, also mark powermetrics as unavailable and use fallback
    powermetrics_unavailable = TRUE;
    cached_gpu_result = get_gpu_usage_fallback();
    last_gpu_check = now;
    return strdup(cached_gpu_result);
}

// Fallback GPU usage detection using alternative methods
char* get_gpu_usage_fallback(void) {
    static int fallback_call_count = 0;
    fallback_call_count++;
    
    // Method 1: Check WindowServer CPU usage as a GPU activity indicator
    char *ws_output = get_full_output("ps -eo pid,pcpu,comm | grep -E 'WindowServer|kernel_task' | head -2");
    if (ws_output) {
        float windowserver_cpu = 0.0;
        char *ws_line = strstr(ws_output, "WindowServer");
        if (ws_line) {
            // Parse WindowServer CPU usage
            char *cpu_start = ws_line;
            while (cpu_start > ws_output && *cpu_start != ' ') cpu_start--;
            if (cpu_start != ws_output) {
                cpu_start++;
                windowserver_cpu = atof(cpu_start);
            }
        }
        free(ws_output);
        
        // Method 2: Check for graphics-intensive processes
        char *gpu_processes = get_full_output("ps -eo pid,pcpu,comm | grep -E 'Safari|Chrome|Firefox|Final Cut|Motion|Compressor|Logic|GarageBand|Photoshop|Illustrator|Premiere|After Effects|Blender|Unity|Unreal|Steam' 2>/dev/null | head -5");
        float total_graphics_cpu = 0.0;
        int graphics_process_count = 0;
        
        if (gpu_processes && strlen(gpu_processes) > 10) {
            char *line = strtok(gpu_processes, "\n");
            while (line && graphics_process_count < 5) {
                float cpu = 0.0;
                if (sscanf(line, "%*d %f", &cpu) == 1) {
                    total_graphics_cpu += cpu;
                    graphics_process_count++;
                }
                line = strtok(NULL, "\n");
            }
        }
        if (gpu_processes) free(gpu_processes);
        
        // Method 3: Estimate GPU usage based on system activity
        float estimated_gpu = 0.0;
        
        // WindowServer CPU is a good indicator of GPU activity on macOS
        if (windowserver_cpu > 15.0) {
            estimated_gpu = windowserver_cpu * 2.0; // Rough correlation
        }
        
        // Add graphics-intensive process contributions
        if (graphics_process_count > 0) {
            estimated_gpu += (total_graphics_cpu * 0.5); // Graphics apps often use GPU
        }
        
        // Cap at reasonable limits
        if (estimated_gpu > 95.0) estimated_gpu = 95.0;
        if (estimated_gpu < 0.0) estimated_gpu = 0.0;
        
        char *result = malloc(20);
        
        // Provide qualitative feedback every few calls to show it's working
        if (fallback_call_count % 10 == 0) {
            if (estimated_gpu < 5.0) {
                sprintf(result, "Idle");
            } else if (estimated_gpu < 25.0) {
                sprintf(result, "Light");
            } else if (estimated_gpu < 50.0) {
                sprintf(result, "Active");
            } else if (estimated_gpu < 75.0) {
                sprintf(result, "Busy");
            } else {
                sprintf(result, "Heavy");
            }
        } else {
            // Show estimated percentage most of the time
            sprintf(result, "~%.0f%%", estimated_gpu);
        }
        
        return result;
    }
    
    // Fallback to simple status
    char *result = malloc(20);
    sprintf(result, "Active");
    return result;
}
