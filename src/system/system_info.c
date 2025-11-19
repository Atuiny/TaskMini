#include "system.h"
#include "performance.h"
#include "../utils/utils.h"
#include "../common/config.h"
#include <unistd.h>
#include <ctype.h>
#include <sys/sysctl.h>
#include <mach/mach.h>

// Global variable for CPU core count
int cpu_cores = 0;

// Global variables for network tracking 
long long prev_system_bytes_in = 0;
long long prev_system_bytes_out = 0;
double prev_system_time = 0.0;

// Get static system specs once: CPU, RAM, GPU name, macOS version, etc.
char* get_static_specs(void) {
    char *cpu = run_command("sysctl -n machdep.cpu.brand_string");
    char *mem_str = run_command("sysctl -n hw.memsize");
    long long mem_bytes = atoll(mem_str);
    free(mem_str);
    double mem_gb = mem_bytes / 1073741824.0;
    
    char *gpu_name = run_command("system_profiler SPDisplaysDataType | awk '/Chipset Model:/ {print $3}' | head -1");
    char *os_ver = run_command("sw_vers -productVersion");
    
    // Get motherboard/hardware info
    char *model = run_command("system_profiler SPHardwareDataType | awk '/Model Name:/ {print $3, $4, $5}' | head -1");
    char *model_id = run_command("system_profiler SPHardwareDataType | awk '/Model Identifier:/ {print $3}' | head -1");
    char *serial = run_command("system_profiler SPHardwareDataType | awk '/Serial Number/ {print $NF}' | head -1");
    
    // Get storage info - total capacity of main drive
    char *storage_info = run_command("df -h / | awk 'NR==2 {print $2}' | head -1");
    
    // Get more detailed hardware info
    char *memory_type = run_command("system_profiler SPMemoryDataType | awk '/Type:/ {print $2; exit}' | head -1");
    char *memory_speed = run_command("system_profiler SPMemoryDataType | awk '/Speed:/ {print $2 \" \" $3; exit}' | head -1");
    
    // Get CPU core count for percentage normalization
    char *cores_str = run_command("sysctl -n hw.ncpu");
    cpu_cores = atoi(cores_str);
    free(cores_str);

    // Simplify CPU name (remove technical details)
    char simplified_cpu[100];
    if (strstr(cpu, "Apple")) {
        // For Apple Silicon, extract just the chip name
        if (strstr(cpu, "M1 Pro")) strcpy(simplified_cpu, "Apple M1 Pro");
        else if (strstr(cpu, "M1 Max")) strcpy(simplified_cpu, "Apple M1 Max");  
        else if (strstr(cpu, "M1 Ultra")) strcpy(simplified_cpu, "Apple M1 Ultra");
        else if (strstr(cpu, "M2 Pro")) strcpy(simplified_cpu, "Apple M2 Pro");
        else if (strstr(cpu, "M2 Max")) strcpy(simplified_cpu, "Apple M2 Max");
        else if (strstr(cpu, "M2 Ultra")) strcpy(simplified_cpu, "Apple M2 Ultra");
        else if (strstr(cpu, "M3 Pro")) strcpy(simplified_cpu, "Apple M3 Pro");
        else if (strstr(cpu, "M3 Max")) strcpy(simplified_cpu, "Apple M3 Max");
        else if (strstr(cpu, "M1")) strcpy(simplified_cpu, "Apple M1");
        else if (strstr(cpu, "M2")) strcpy(simplified_cpu, "Apple M2");
        else if (strstr(cpu, "M3")) strcpy(simplified_cpu, "Apple M3");
        else strcpy(simplified_cpu, "Apple Silicon");
    } else {
        // For Intel, just use the brand name
        strncpy(simplified_cpu, cpu, 99);
        simplified_cpu[99] = '\0';
    }

    // Build comprehensive system info
    char *specs = malloc(1024);
    char memory_details[100] = "";
    
    // Format memory info with type and speed if available
    if (strlen(memory_type) > 3 && strlen(memory_speed) > 3) {
        sprintf(memory_details, "Memory: %.0f GB %s @ %s", mem_gb, memory_type, memory_speed);
    } else {
        sprintf(memory_details, "Memory: %.0f GB installed", mem_gb);
    }
    
    // Clean up model name (remove extra spaces)
    char *clean_model = model;
    while (*clean_model == ' ') clean_model++;
    
    sprintf(specs, "Machine: %s (%s)\nProcessor: %s (%d-core)\n%s\nStorage: %s main drive\nGraphics: %s chip\nSystem: macOS %s\nSerial: %s", 
            clean_model, model_id, simplified_cpu, cpu_cores, memory_details, 
            storage_info, gpu_name, os_ver, serial);

    free(cpu);
    free(gpu_name);
    free(os_ver);
    free(model);
    free(model_id);
    free(serial);
    free(storage_info);
    free(memory_type);
    free(memory_speed);
    return specs;
}

// Get elapsed run time for a PID using ps -o etime=
char* get_run_time(const char *pid) {
    char cmd[50];
    sprintf(cmd, "ps -p %s -o etime=", pid);
    return run_command(cmd);
}

// Function to get top output with better CPU sampling
char* get_top_output(void) {
    FILE *fp;
    char *buffer = malloc(65536);  // Much larger buffer for all processes
    size_t buffer_size = 65536;
    size_t total_len = 0;
    size_t bytes_read = 0;
    char chunk[1024];
    
    buffer[0] = '\0';

    // Take 2 samples 1 second apart for better CPU calculation
    // Remove -n limit to show ALL processes like a real task manager
    fp = popen("top -l 2 -s 1 -o cpu -stats pid,command,cpu,mem,time", "r");
    if (fp == NULL) {
        perror("popen failed");
        exit(1);
    }

    while ((bytes_read = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (total_len + bytes_read + 1 > buffer_size) {
            buffer_size *= 2;
            buffer = realloc(buffer, buffer_size);
        }
        memcpy(buffer + total_len, chunk, bytes_read);
        total_len += bytes_read;
        buffer[total_len] = '\0';
    }

    pclose(fp);
    
    // Find the second sample by looking for the second occurrence of "Processes:"
    char *first_processes = strstr(buffer, "Processes:");
    if (first_processes) {
        char *second_processes = strstr(first_processes + 1, "Processes:");
        if (second_processes) {
            // Return from the second sample which has better CPU data
            return strdup(second_processes);
        }
    }
    
    // If we can't find second sample, return the whole output
    return buffer;
}

// Determine if a process is a critical system process
gboolean is_system_process(const char *name, const char *pid) {
    // Critical system processes that should not be killed
    const char *system_processes[] = {
        "kernel_task", "launchd", "SystemUIServer", "Dock", "Finder", 
        "WindowServer", "loginwindow", "cfprefsd", "systemstats",
        "syslogd", "kextd", "fseventsd", "distnoted", "notifyd",
        "UserEventAgent", "coreservicesd", "lsd", "securityd",
        "sandboxd", "mds", "mdworker", "spotlight", "mdfind",
        "coreaudiod", "audiomxd", "bluetoothd", "wifid",
        "networkd", "dhcpcd", "ntpd", "chronod", "timed",
        "powerd", "thermald", "kernel", "hibernate",
        "AppleSpell", "spindump", "ReportCrash", "CrashReporter",
        "activitymonitord", "SubmitDiagInfo", "DiagnosticReporting",
        NULL
    };
    
    // Check against known system process names
    for (int i = 0; system_processes[i] != NULL; i++) {
        if (strstr(name, system_processes[i]) != NULL) {
            return TRUE;
        }
    }
    
    // Check if PID is very low (typically system processes)
    int pid_num = atoi(pid);
    if (pid_num <= 10 && pid_num > 0) {
        return TRUE;
    }
    
    // Check if running as root (UID 0) - system processes often run as root
    char cmd[100];
    sprintf(cmd, "ps -p %s -o uid= 2>/dev/null", pid);
    char *uid_str = run_command(cmd);
    
    // Only process UID if we got valid output from ps command
    if (uid_str && strlen(uid_str) > 0 && strstr(uid_str, "N/A") == NULL) {
        // Strip whitespace and verify we have numeric content
        char *trimmed = uid_str;
        while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n') trimmed++;
        
        // Check if the result is actually a number
        if (*trimmed >= '0' && *trimmed <= '9') {
            int uid = atoi(trimmed);
            if (uid == 0) {
                // Root process - likely system, but check if it's a user-launched root process
                if (strstr(name, "sudo") != NULL || 
                    strstr(name, "Terminal") != NULL ||
                    strstr(name, "iTerm") != NULL) {
                    free(uid_str);
                    return FALSE; // User-launched root process
                }
                free(uid_str);
                return TRUE;
            } else {
                free(uid_str);
                return FALSE;
            }
        }
    }
    if (uid_str) free(uid_str);
    
    return FALSE;
}

// Determine and set the process type - SECURED with bounds checking
void determine_process_type(Process *proc) {
    if (!proc) return;  // SECURITY: Null pointer check
    
    if (is_system_process(proc->name, proc->pid)) {
        safe_strncpy(proc->type, "🛡️ System", sizeof(proc->type));
        proc->is_system = TRUE;
    } else {
        safe_strncpy(proc->type, "User", sizeof(proc->type));
        proc->is_system = FALSE;
    }
}

// Get system-wide CPU usage percentage (optimized)
float get_system_cpu_usage(void) {
    // Use optimized version first
    double cpu_fast = get_system_cpu_usage_fast();
    if (cpu_fast > 0.0) {
        return (float)cpu_fast;
    }
    
    // Fallback to traditional method if needed
    FILE *fp = popen("top -l 1 -n 0 | grep 'CPU usage:'", "r");
    if (!fp) return 0.0;
    
    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        pclose(fp);
        return 0.0;
    }
    pclose(fp);
    
    // Parse: "CPU usage: 37.57% user, 26.53% sys, 35.88% idle"
    float user = 0.0, sys = 0.0, idle = 0.0;
    if (sscanf(line, "CPU usage: %f%% user, %f%% sys, %f%% idle", &user, &sys, &idle) == 3) {
        // CPU usage = user + sys (exclude idle)
        return user + sys;
    }
    
    return 0.0;
}

// Get system-wide memory usage percentage (optimized)
float get_system_memory_usage(void) {
    // Use optimized version first
    double mem_fast = get_system_memory_usage_fast();
    if (mem_fast > 0.0) {
        return (float)mem_fast;
    }
    
    // Fallback to traditional method if needed
    char *total_mem_str = run_command("sysctl -n hw.memsize");
    if (!total_mem_str || strcmp(total_mem_str, "N/A") == 0) {
        if (total_mem_str) free(total_mem_str);
        return 0.0;
    }
    
    long long total_bytes = atoll(total_mem_str);
    free(total_mem_str);
    
    if (total_bytes == 0) return 0.0;
    
    // Get used pages using Activity Monitor's method: sum of active, inactive, speculative, wired, and compressed
    FILE *fp = popen("vm_stat | awk 'BEGIN{total=0} /Pages active|Pages inactive|Pages speculative|Pages wired down|Pages occupied by compressor/ {gsub(/[^0-9]/, \"\", $NF); total+=$NF} END{print total}'", "r");
    if (!fp) return 0.0;
    
    char buffer[64];
    if (!fgets(buffer, sizeof(buffer), fp)) {
        pclose(fp);
        return 0.0;
    }
    pclose(fp);
    
    // Remove newline
    char *newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';
    
    long used_pages = atol(buffer);
    
    // Get actual page size from vm_stat header
    FILE *fp2 = popen("vm_stat | head -1 | grep -o '[0-9]*' | tail -1", "r");
    if (!fp2) return 0.0;
    
    char page_size_str[32];
    if (!fgets(page_size_str, sizeof(page_size_str), fp2)) {
        pclose(fp2);
        return 0.0;
    }
    pclose(fp2);
    
    long page_size = atol(page_size_str);
    if (page_size == 0) page_size = 16384; // fallback
    
    long long used_bytes = used_pages * page_size;
    
    return ((float)used_bytes / (float)total_bytes) * 100.0;
}

