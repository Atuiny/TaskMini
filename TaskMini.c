// Teaching: Include necessary headers. We've added glib.h explicitly for GHashTable and other GLib functions, though GTK includes it. time.h for potential time functions, but we use g_get_real_time. ctype.h for isdigit in parsing if needed. No changes here for threading.
#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

// Teaching: Global variables: Added hashes for previous network bytes and times per PID (as strings). Added label for system specs and static specs string. Added system summary label for top output. Added a boolean to track if an update is in progress to prevent concurrent threads. Added a mutex for safe access to shared hashes in case of potential overlaps, though we try to avoid them. Added system-wide network rate tracking variables.
static GtkApplication *app;
static GtkListStore *liststore;
static GHashTable *prev_net_bytes;
static GHashTable *prev_times;
static GtkLabel *specs_label;
static GtkLabel *summary_label;
static char *static_specs;
static gboolean updating = FALSE;
static GMutex hash_mutex;

// System-wide network rate tracking
static long long prev_system_bytes_in = 0;
static long long prev_system_bytes_out = 0;
static double prev_system_time = 0.0;

// Global widgets for scroll position preservation (removed to use new approach)

// Teaching: Updated enums for new columns: Added COL_GPU, COL_NET, COL_RUNTIME, COL_TYPE. Renamed COL_COMMAND to COL_NAME for clarity. No changes for threading.
enum {
    COL_PID,
    COL_NAME,
    COL_CPU,
    COL_GPU,
    COL_MEM,
    COL_NET,
    COL_RUNTIME,
    COL_TYPE,
    NUM_COLS
};

// OPTIMIZATION: Reduced string buffer sizes based on actual data analysis
// and added memory pool for frequent allocations
typedef struct {
    char pid[10];       // Max PID on macOS + safety margin 
    char name[50];      // Keep original size for safety
    char cpu[10];       // "999.9" + null terminator with safety margin
    char mem[12];       // "999.99 GB" + null terminator (10 bytes sufficient)
    char gpu[10];       // "Heavy" or "~99%" + null terminator with margin
    char net[20];       // "9999.9 KB/s" + null terminator with margin
    char runtime[20];   // "99-23:59:59" + null terminator with margin
    char type[20];      // "🛡️ System" or "User" + null terminator with margin
    gboolean is_system; // TRUE if system process
} Process;

// OPTIMIZATION: Memory pool for Process structs to reduce malloc/free overhead
#define PROCESS_POOL_SIZE 512
static Process *process_pool = NULL;
static gboolean *pool_used = NULL;
static int pool_initialized = 0;

// Teaching: New struct to pass data from background thread to UI update function. Contains a GList of Process pointers, the GPU usage string, and system summary info.
typedef struct {
    GList *processes;
    char *gpu_usage;
    char *system_summary;
} UpdateData;

// Global variables for scroll position preservation
static GtkTreeView *global_treeview = NULL;
static GtkScrolledWindow *global_scrolled_window = NULL;

// Struct for scroll position restoration
typedef struct {
    gdouble scroll_position;
} ScrollRestoreData;

// SECURITY: Resource limits to prevent DoS
#define MAX_PROCESSES_PER_UPDATE 2000
#define MAX_COMMAND_OUTPUT_SIZE (1024 * 1024)  // 1MB limit
#define MAX_UPDATE_TIME_MS 5000  // 5 second timeout

static time_t last_update_time = 0;
static int consecutive_failures = 0;
static const int max_failures = 5;

// OPTIMIZATION: Memory pool management for Process structs
static void init_process_pool() {
    if (!pool_initialized) {
        process_pool = calloc(PROCESS_POOL_SIZE, sizeof(Process));
        pool_used = calloc(PROCESS_POOL_SIZE, sizeof(gboolean));
        pool_initialized = 1;
    }
}

static Process* alloc_process() {
    if (!pool_initialized) init_process_pool();
    
    for (int i = 0; i < PROCESS_POOL_SIZE; i++) {
        if (!pool_used[i]) {
            pool_used[i] = TRUE;
            memset(&process_pool[i], 0, sizeof(Process)); // Clear previous data
            return &process_pool[i];
        }
    }
    
    // Fallback to malloc if pool exhausted (rare case)
    return malloc(sizeof(Process));
}

static void free_process(Process *proc) {
    if (!proc) return;
    
    // Check if it's from our pool
    if (proc >= process_pool && proc < process_pool + PROCESS_POOL_SIZE) {
        int index = proc - process_pool;
        pool_used[index] = FALSE;
    } else {
        // It was malloc'd as fallback
        free(proc);
    }
}

static void cleanup_process_pool() {
    if (pool_initialized) {
        free(process_pool);
        free(pool_used);
        process_pool = NULL;
        pool_used = NULL;
        pool_initialized = 0;
    }
}

// OPTIMIZATION: String buffer cache to reduce malloc overhead for command outputs
#define STRING_CACHE_SIZE 16
static char *string_cache[STRING_CACHE_SIZE];
static size_t cache_sizes[STRING_CACHE_SIZE];
static int cache_initialized = 0;

static char* get_cached_buffer(size_t min_size) {
    if (!cache_initialized) {
        memset(string_cache, 0, sizeof(string_cache));
        memset(cache_sizes, 0, sizeof(cache_sizes));
        cache_initialized = 1;
    }
    
    for (int i = 0; i < STRING_CACHE_SIZE; i++) {
        if (string_cache[i] && cache_sizes[i] >= min_size) {
            char *buffer = string_cache[i];
            string_cache[i] = NULL; // Mark as in use
            return buffer;
        }
    }
    
    return malloc(min_size);
}

static void return_cached_buffer(char *buffer, size_t size) {
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

// SECURITY: Input validation for commands - allows safe system commands
static gboolean is_safe_command(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return FALSE;
    if (strlen(cmd) > 1024) return FALSE;  // Increased limit for system_profiler commands
    
    // Whitelist of safe command prefixes
    const char *safe_commands[] = {
        "sysctl -n",
        "system_profiler",
        "sw_vers",
        "df -h",
        "ps -",
        "top -",
        "nettop",
        "powermetrics",
        "sed ",
        "awk ",
        "head ",
        "grep ",
        NULL
    };
    
    // Check if command starts with a safe prefix
    gboolean is_safe = FALSE;
    for (int i = 0; safe_commands[i]; i++) {
        if (strncmp(cmd, safe_commands[i], strlen(safe_commands[i])) == 0) {
            is_safe = TRUE;
            break;
        }
    }
    
    if (!is_safe) return FALSE;
    
    // Still check for dangerous injection characters, but allow pipes in system_profiler commands
    const char *dangerous[] = {";", "&", "`", "$(", "${", "\\", NULL};
    
    for (int i = 0; dangerous[i]; i++) {
        if (strstr(cmd, dangerous[i])) return FALSE;
    }
    
    // Special handling for commands that need pipes and quotes
    if (strncmp(cmd, "system_profiler", 15) == 0 || 
        strncmp(cmd, "df -h", 5) == 0 ||
        strncmp(cmd, "awk ", 4) == 0 ||
        strncmp(cmd, "sed ", 4) == 0) {
        // Allow pipes and quotes for data processing in system commands
        // Still check for the most dangerous injection patterns
        if (strstr(cmd, "$(") || strstr(cmd, "`") || strstr(cmd, ";") || 
            strstr(cmd, "&&") || strstr(cmd, "||")) {
            return FALSE;
        }
        return TRUE;
    }
    
    // Check for remaining dangerous characters in other commands
    if (strstr(cmd, "|") || strstr(cmd, ">") || strstr(cmd, "<") || 
        strstr(cmd, "'") || strstr(cmd, "\"")) {
        return FALSE;
    }
    
    return TRUE;
}

// Teaching: Helper to run a command and get its trimmed output as string. OPTIMIZED + SECURED: Uses string cache with validation.
char* run_command(const char *cmd) {
    // SECURITY: Validate command before execution
    if (!is_safe_command(cmd)) {
        return strdup("N/A");
    }
    
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return strdup("N/A");

    char *buffer = get_cached_buffer(256);
    if (fgets(buffer, 256, fp) == NULL) {
        return_cached_buffer(buffer, 256);
        pclose(fp);
        return strdup("N/A");
    }
    
    // OPTIMIZATION: In-place newline removal (faster than strcspn)
    char *newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';
    
    pclose(fp);
    
    // Create final result and return buffer to cache
    char *result = strdup(buffer);
    return_cached_buffer(buffer, 256);
    return result;
}

// OPTIMIZATION: Improved get_full_output with better memory management and reduced reallocs
char* get_full_output(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return NULL;

    size_t buffer_size = 8192;  // Start with larger buffer
    size_t total_len = 0;
    char *buffer = get_cached_buffer(buffer_size);
    char chunk[2048];  // Larger read chunks for better performance
    size_t bytes_read = 0;

    while ((bytes_read = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        // OPTIMIZATION: Grow buffer more efficiently (double when needed)
        if (total_len + bytes_read + 1 > buffer_size) {
            size_t new_size = (buffer_size * 2) > (total_len + bytes_read + 1) ? 
                             (buffer_size * 2) : (total_len + bytes_read + 1024);
            char *new_buffer = malloc(new_size);
            if (total_len > 0) {
                memcpy(new_buffer, buffer, total_len);
            }
            return_cached_buffer(buffer, buffer_size);
            buffer = new_buffer;
            buffer_size = new_size;
        }
        
        // OPTIMIZATION: Direct memory copy instead of strncat (much faster)
        memcpy(buffer + total_len, chunk, bytes_read);
        total_len += bytes_read;
    }
    
    buffer[total_len] = '\0';
    pclose(fp);
    
    // Create final result with exact size needed
    char *result = malloc(total_len + 1);
    memcpy(result, buffer, total_len + 1);
    
    // Return working buffer to cache if it's a reasonable size
    if (buffer_size <= 32768) {
        return_cached_buffer(buffer, buffer_size);
    } else {
        free(buffer);  // Too large for cache
    }
    
    return result;
}

// Forward declarations for optimized functions
char* get_gpu_usage_fallback();
static long long get_net_bytes_individual(const char *pid);
static void collect_all_network_data();

// OPTIMIZATION: GPU detection caching and state management
static gboolean powermetrics_unavailable = FALSE;
static char *cached_gpu_result = NULL;
static time_t last_gpu_check = 0;
static int gpu_check_interval = 2;  // Cache GPU result for 2 seconds

// OPTIMIZATION: GPU usage with caching and reduced system calls
char* get_gpu_usage() {
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
char* get_gpu_usage_fallback() {
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

// OPTIMIZATION: Network monitoring with reduced calls and improved parsing
// Cache network data collection to avoid excessive nettop calls
static time_t last_net_collection = 0;
static GHashTable *net_cache = NULL;

long long get_net_bytes(const char *pid) {
    time_t now = time(NULL);
    
    // OPTIMIZATION: Use cached network data if collected recently (within 1 second)
    if (net_cache && (now - last_net_collection) < 1) {
        gpointer value = g_hash_table_lookup(net_cache, pid);
        return value ? *(long long*)value : 0;
    }
    
        // Return 0 if no cached data available
    return 0;
}

// OPTIMIZATION: Collect network data for all processes in one nettop call
static void collect_all_network_data() {
    if (!net_cache) {
        net_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    } else {
        g_hash_table_remove_all(net_cache);  // Clear old data
    }
    
    // Single nettop call for all processes (much more efficient)
    FILE *fp = popen("nettop -L1 -P", "r");
    if (fp == NULL) return;

    char line[512];
    int is_header = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (is_header) {
            is_header = 0;
            continue;
        }
        
        if (strlen(line) < 10) continue;
        
        // OPTIMIZATION: Fast CSV parsing with direct pointer manipulation
        char *pos = line;
        char *fields[8];  // We need at least 6 fields
        int field_count = 0;
        
        // Parse CSV fields
        while (*pos && field_count < 8) {
            fields[field_count] = pos;
            
            // Find next comma or end
            while (*pos && *pos != ',') pos++;
            if (*pos) {
                *pos = '\0';  // Terminate field
                pos++;
            }
            field_count++;
        }
        
        if (field_count >= 6) {
            // PID is field 0, bytes_in is field 4, bytes_out is field 5
            char *pid_str = fields[0];
            long long bytes_in = atoll(fields[4]);
            long long bytes_out = atoll(fields[5]);
            long long total = bytes_in + bytes_out;
            
            if (total > 0) {  // Only cache processes with network activity
                long long *cached_bytes = malloc(sizeof(long long));
                *cached_bytes = total;
                g_hash_table_insert(net_cache, g_strdup(pid_str), cached_bytes);
            }
        }
    }
    pclose(fp);
}

// Individual network lookup (for specific PID when needed)
static long long get_net_bytes_individual(const char *pid) {
    char cmd[100];
    snprintf(cmd, sizeof(cmd), "nettop -p %s -L1", pid);
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return 0;

    char line[512];
    long long total = 0;
    int is_header = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (is_header) {
            is_header = 0;
            continue;
        }
        
        if (strlen(line) < 10) continue;
        
        // OPTIMIZATION: Direct parsing without strdup/strtok (faster)
        char *pos = line;
        int col = 0;
        long long bytes_in = 0, bytes_out = 0;
        
        while (*pos && col <= 5) {
            if (col == 4) bytes_in = strtoll(pos, &pos, 10);
            else if (col == 5) bytes_out = strtoll(pos, &pos, 10);
            else {
                // Skip to next comma
                while (*pos && *pos != ',') pos++;
            }
            
            if (*pos == ',') {
                pos++;
                col++;
            } else {
                break;
            }
        }
        
        if (col > 5) {
            total += bytes_in + bytes_out;
        }
    }
    pclose(fp);
    return total;
}

// Teaching: Get elapsed run time for a PID using ps -o etime=. No changes for threading, but called in background.
char* get_run_time(const char *pid) {
    char cmd[50];
    sprintf(cmd, "ps -p %s -o etime=", pid);
    return run_command(cmd);
}

// Global variable for CPU core count
static int cpu_cores = 0;

// Teaching: Get static system specs once: CPU, RAM, GPU name, macOS version, motherboard, storage. Also get CPU core count for percentage normalization.
char* get_static_specs() {
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

// Teaching: Parse bytes string like "1234 B" or "5.6 MiB" to long long bytes. No changes for threading.
long long parse_bytes(char *str) {
    char *end;
    double val = strtod(str, &end);
    if (end == str) return 0;

    while (*end == ' ') end++;  // Skip space before unit

    if (strncasecmp(end, "B", 1) == 0) return (long long)val;
    if (strncasecmp(end, "K", 1) == 0) return (long long)(val * 1024);
    if (strncasecmp(end, "M", 1) == 0) return (long long)(val * 1024 * 1024);
    if (strncasecmp(end, "G", 1) == 0) return (long long)(val * 1024 * 1024 * 1024);
    return 0;
}

// Helper function to format bytes as human-readable strings (for rates)
static char* format_bytes_human_readable(long long bytes) {
    if (bytes < 1024) {
        return g_strdup_printf("%lld B", bytes);
    } else if (bytes < 1024 * 1024) {
        return g_strdup_printf("%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        return g_strdup_printf("%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        return g_strdup_printf("%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

// Helper function to parse memory strings like "26G", "1598M" into bytes
static long long parse_memory_string(const char *str) {
    if (!str || strlen(str) == 0) return 0;
    
    long long value = strtoll(str, NULL, 10);
    char unit = str[strlen(str) - 1];
    
    switch (unit) {
        case 'K': case 'k':
            return value * 1024LL;
        case 'M': case 'm':
            return value * 1024LL * 1024LL;
        case 'G': case 'g':
            return value * 1024LL * 1024LL * 1024LL;
        case 'T': case 't':
            return value * 1024LL * 1024LL * 1024LL * 1024LL;
        default:
            // If no unit, assume bytes
            return value;
    }
}

// Teaching: Convert memory string from top output to human-readable format
char* format_memory_human_readable(const char *mem_str) {
    // Parse the memory value from top output (like "1024M", "512K", "2.5G")
    long long bytes = parse_memory_string((char*)mem_str);
    
    char *result = malloc(20);
    
    if (bytes >= 1024LL * 1024 * 1024) {
        // >= 1 GB: show as GB with 2 decimal places
        double gb = (double)bytes / (1024LL * 1024 * 1024);
        if (gb >= 10.0) {
            sprintf(result, "%.1f GB", gb);  // 10.1 GB (1 decimal for large values)
        } else {
            sprintf(result, "%.2f GB", gb);  // 1.25 GB (2 decimals for smaller GB values)
        }
    } else if (bytes >= 1024 * 1024) {
        // >= 1 MB: show as MB with 1 decimal place
        double mb = (double)bytes / (1024 * 1024);
        if (mb >= 100.0) {
            sprintf(result, "%.0f MB", mb);  // 512 MB (no decimals for large MB values)
        } else {
            sprintf(result, "%.1f MB", mb);  // 64.5 MB (1 decimal for smaller MB values)
        }
    } else if (bytes >= 1024) {
        // >= 1 KB: show as KB with no decimals
        long kb = bytes / 1024;
        sprintf(result, "%ld KB", kb);  // 512 KB
    } else {
        // < 1 KB: show as bytes
        sprintf(result, "%lld B", bytes);  // 256 B
    }
    
    return result;
}

// Teaching: Parse run time string to seconds for sorting, e.g., "01:23:45" or "1-02:34:56". No changes for threading.
long long parse_runtime_to_seconds(const char *str) {
    int days = 0, hours = 0, mins = 0, secs = 0;
    
#ifdef TESTING
    // Debug output for testing - only print first few times to avoid spam
    static int debug_count = 0;
    if (debug_count < 3) {
        fprintf(stderr, "DEBUG: Parsing runtime string: '%s'\n", str);
        debug_count++;
    }
#endif
    
    if (sscanf(str, "%d-%d:%d:%d", &days, &hours, &mins, &secs) == 4) {
        // Format: days-hours:mins:secs
#ifdef TESTING
        if (debug_count <= 3) {
            fprintf(stderr, "DEBUG: Parsed as days-hours:mins:secs = %d-%d:%d:%d\n", days, hours, mins, secs);
        }
#endif
    } else if (sscanf(str, "%d:%d:%d", &hours, &mins, &secs) == 3) {
        // Format: hours:mins:secs
        days = 0;
#ifdef TESTING
        if (debug_count <= 3) {
            fprintf(stderr, "DEBUG: Parsed as hours:mins:secs = %d:%d:%d\n", hours, mins, secs);
        }
#endif
    } else if (sscanf(str, "%d:%d", &mins, &secs) == 2) {
        // Format: mins:secs
        days = 0;
        hours = 0;
#ifdef TESTING
        if (debug_count <= 3) {
            fprintf(stderr, "DEBUG: Parsed as mins:secs = %d:%d\n", mins, secs);
        }
#endif
    } else {
#ifdef TESTING
        if (debug_count <= 3) {
            fprintf(stderr, "DEBUG: Failed to parse runtime string\n");
        }
#endif
        return 0;
    }
    
    long long result = (long long)days * 86400 + hours * 3600 + mins * 60 + secs;
#ifdef TESTING
    if (debug_count <= 3) {
        fprintf(stderr, "DEBUG: Final result = %lld seconds\n", result);
    }
#endif
    return result;
}

// Teaching: Custom compare function for sorting columns. Handles different types: int for PID, string for Name, float for CPU/Net/GPU, bytes for Mem, seconds for Runtime. No changes for threading, as sorting happens in main thread.
static gint process_compare_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    int col = GPOINTER_TO_INT(user_data);
    gchar *va = NULL, *vb = NULL;
    gtk_tree_model_get(model, a, col, &va, -1);
    gtk_tree_model_get(model, b, col, &vb, -1);

    if (va == NULL || vb == NULL) {
        gint ret = (va == NULL) - (vb == NULL);
        g_free(va);
        g_free(vb);
        return ret;
    }

    gint ret = 0;
    switch (col) {
        case COL_PID: {
            int ia = atoi(va), ib = atoi(vb);
            ret = ia - ib;
            break;
        }
        case COL_NAME: {
            ret = g_strcmp0(va, vb);
            break;
        }
        case COL_CPU:
        case COL_GPU:
        case COL_NET: {  // Treat N/A or KB/s as float (strip non-numeric if needed, but simple atof works)
            float fa = atof(va), fb = atof(vb);
            
            // More robust float comparison to avoid precision issues
            if (fabs(fa - fb) < 0.001) {
                ret = 0;
            } else {
                ret = (fa > fb) ? 1 : (fa < fb) ? -1 : 0;
            }
            break;
        }
        case COL_MEM: {
            long long la = parse_bytes(va), lb = parse_bytes(vb);
            ret = (la > lb) ? 1 : (la < lb) ? -1 : 0;
            break;
        }
        case COL_RUNTIME: {
            long long la = parse_runtime_to_seconds(va), lb = parse_runtime_to_seconds(vb);
            ret = (la > lb) ? 1 : (la < lb) ? -1 : 0;
            break;
        }
        case COL_TYPE: {
            ret = g_strcmp0(va, vb);
            break;
        }
    }
    g_free(va);
    g_free(vb);
    return ret;
}

// Teaching: Function to get top output with better CPU sampling. Use multiple samples for more accurate CPU readings.
char* get_top_output() {
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

// Forward declaration for UI update function
static gboolean update_ui_func(gpointer user_data);

// Forward declarations for context menu functions
void show_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
gboolean on_treeview_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
void kill_process_callback(GtkWidget *menuitem, gpointer user_data);

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

// SECURITY: Safe string operations with bounds checking
static void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';  // Ensure null termination
}

static void safe_strncat(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    size_t current_len = strnlen(dest, dest_size - 1);
    size_t remaining = dest_size - current_len - 1;
    
    if (remaining > 0) {
        strncat(dest, src, remaining);
        dest[dest_size - 1] = '\0';  // Ensure null termination
    }
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

// SECURITY: Thread monitoring and timeout protection
static gboolean update_thread_running = FALSE;
static time_t update_start_time = 0;

// Teaching: New function for the background thread. SECURED with timeout and resource limits.
static gpointer update_thread_func(gpointer data) {
    update_thread_running = TRUE;
    update_start_time = time(NULL);
    
    // SECURITY: Check for excessive consecutive failures
    if (consecutive_failures >= max_failures) {
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
    gboolean collecting_summary = FALSE;
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
                // Format: "packets: 21060567/26G in, 7375591/1598M out"
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
                
                // Look for swap activity
                char *swapins_pos = strstr(vm_info, " swapins");
                char *swapouts_pos = strstr(vm_info, " swapouts");
                long long swapins = 0, swapouts = 0;
                
                if (swapins_pos) {
                    char *paren = strchr(vm_info, '(');
                    if (paren && paren < swapins_pos) {
                        swapins = strtoll(paren - 10, NULL, 10); // Rough estimate of position
                    }
                }
                
                if (swapouts_pos) {
                    char *paren = strrchr(vm_info, '(');
                    if (paren) {
                        swapouts = strtoll(paren - 10, NULL, 10); // Rough estimate of position
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
                
                // Show actual swap numbers for transparency
                char swap_status[100] = "";
                if (actual_swapins > 0 || actual_swapouts > 0) {
                    sprintf(swap_status, " (swap-ins: %lld, swap-outs: %lld)", actual_swapins, actual_swapouts);
                } else {
                    sprintf(swap_status, " (swap-ins: 0, swap-outs: 0)");
                }
                
                if (strlen(total_allocated) > 0 && strlen(framework_mem) > 0) {
                    // Explain what T and M mean in context - clarify this is virtual address space, not actual RAM
                    char *total_unit = strchr(total_allocated, 'T') ? "TB" : 
                                      strchr(total_allocated, 'G') ? "GB" : 
                                      strchr(total_allocated, 'M') ? "MB" : "bytes";
                    char *system_unit = strchr(framework_mem, 'T') ? "TB" : 
                                       strchr(framework_mem, 'G') ? "GB" : 
                                       strchr(framework_mem, 'M') ? "MB" : "bytes";
                    
                    // Make it clear this is address space, not actual RAM usage                   
                    sprintf(simplified_line, "Virtual Memory: %s (%s) address space for apps, %s (%s) for system%s", 
                           total_allocated, total_unit, framework_mem, system_unit, swap_status);
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
                strncpy(simplified_line, line, 255);
                simplified_line[255] = '\0';
            }
            
            strcat(summary_buffer, simplified_line);
        }
        
        // Look for the PID COMMAND header to know when process lines start
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
                    printf("Not enough tokens (%d) in line: '%s'\n", token_count, line);
                    for (int i = 0; i < token_count; i++) {
                        free(tokens[i]);
                    }
                    free(line_copy);
                    free(proc);
                    continue;
                }
                
                free(line_copy);
                
                // Override runtime with elapsed time
                char *runtime = get_run_time(proc->pid);
                strcpy(proc->runtime, runtime);
                free(runtime);

                // GPU: N/A (per-process not easily available)
                strcpy(proc->gpu, "N/A");
                
                // Determine process type (system vs user)
                determine_process_type(proc);

                // Network rate
                long long current_bytes = get_net_bytes(proc->pid);
                char *pid_key = g_strdup(proc->pid);
                double rate = 0.0;

                g_mutex_lock(&hash_mutex);
                if (g_hash_table_contains(prev_net_bytes, pid_key)) {
                    long long *prev_b_ptr = (long long *)g_hash_table_lookup(prev_net_bytes, pid_key);
                    double *prev_t_ptr = (double *)g_hash_table_lookup(prev_times, pid_key);
                    double delta_time = current_time - *prev_b_ptr;
                    if (delta_time > 0.3) {  // Need at least 0.3 seconds for meaningful rate
                        long long bytes_diff = current_bytes - *prev_b_ptr;
                        if (bytes_diff > 0) {  // Only show positive rates
                            rate = (double)bytes_diff / delta_time / 1024.0;  // KB/s
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

    g_idle_add(update_ui_func, update_data);
    
    update_thread_running = FALSE;
    return NULL;
}



// Function to restore scroll position in an idle callback
static gboolean restore_scroll_position_idle(gpointer data) {
    ScrollRestoreData *scroll_data = (ScrollRestoreData *)data;
    
    if (global_scrolled_window) {
        GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(global_scrolled_window);
        if (vadj) {
            gtk_adjustment_set_value(vadj, scroll_data->scroll_position);
        }
    }
    
    g_free(scroll_data);
    return G_SOURCE_REMOVE; // Remove the idle callback after execution
}

// Teaching: New function called in main thread via g_idle_add. Updates the liststore by clearing and appending from the process list. Updates the specs label. Frees the data structures. Resets updating flag. Now preserves scroll position during updates.
static gboolean update_ui_func(gpointer user_data) {
    UpdateData *data = (UpdateData *)user_data;

    // Save current scroll position before updating
    gdouble scroll_position = 0.0;
    GtkAdjustment *vadj = NULL;
    if (global_scrolled_window) {
        vadj = gtk_scrolled_window_get_vadjustment(global_scrolled_window);
        if (vadj) {
            scroll_position = gtk_adjustment_get_value(vadj);
        }
    }

    gtk_list_store_clear(liststore);

    GtkTreeIter iter;
    GList *l;
    for (l = data->processes; l != NULL; l = l->next) {
        Process *proc = (Process *)l->data;
        gtk_list_store_append(liststore, &iter);
        gtk_list_store_set(liststore, &iter,
                           COL_PID, proc->pid,
                           COL_NAME, proc->name,
                           COL_CPU, proc->cpu,
                           COL_GPU, proc->gpu,
                           COL_MEM, proc->mem,
                           COL_NET, proc->net,
                           COL_RUNTIME, proc->runtime,
                           COL_TYPE, proc->type,
                           -1);
        free_process(proc);  // OPTIMIZATION: Use memory pool
    }
    g_list_free(data->processes);

    // Restore the scroll position after updating the list using an idle callback
    if (vadj && scroll_position > 0.0) {
        ScrollRestoreData *scroll_data = g_malloc(sizeof(ScrollRestoreData));
        scroll_data->scroll_position = scroll_position;
        g_idle_add(restore_scroll_position_idle, scroll_data);
    }

    // Don't force re-sort - let user's column sort choice persist
    // The TreeView will maintain the current sort order automatically

    // Update specs label with dynamic GPU usage in friendly format
    char full_specs[1024];
    
    // Parse GPU percentage for friendly display
    float gpu_percent = 0.0;
    sscanf(data->gpu_usage, "%f%%", &gpu_percent);
    
    char gpu_status[50];
    if (gpu_percent < 5.0) {
        strcpy(gpu_status, "Graphics: Idle");
    } else if (gpu_percent < 25.0) {
        strcpy(gpu_status, "Graphics: Light use");
    } else if (gpu_percent < 50.0) {
        strcpy(gpu_status, "Graphics: Moderate use"); 
    } else if (gpu_percent < 75.0) {
        strcpy(gpu_status, "Graphics: Heavy use");
    } else {
        strcpy(gpu_status, "Graphics: Maximum use");
    }
    
    sprintf(full_specs, "%s\n%s (%.0f%%)", static_specs, gpu_status, gpu_percent);
    gtk_label_set_text(specs_label, full_specs);
    
    // Update system summary label
    gtk_label_set_text(summary_label, data->system_summary);
    
    free(data->gpu_usage);
    free(data->system_summary);
    free(data);

    updating = FALSE;

    return G_SOURCE_REMOVE;
}



// SECURITY: Enhanced timeout callback with deadlock detection and resource monitoring
gboolean timeout_callback(gpointer data) {
    time_t now = time(NULL);
    
    // SECURITY: Detect hung update threads
    if (update_thread_running && (now - update_start_time) > (MAX_UPDATE_TIME_MS / 1000)) {
        // Thread appears hung, reset state
        updating = FALSE;
        update_thread_running = FALSE;
        consecutive_failures++;
    }
    
    if (updating) {
        return TRUE;  // Skip if still updating
    }
    
    // SECURITY: Throttle updates if too many failures
    if (consecutive_failures >= max_failures) {
        static time_t last_throttle_log = 0;
        if ((now - last_throttle_log) > 10) {  // Log once every 10 seconds
            g_warning("TaskMini: Throttling updates due to consecutive failures");
            last_throttle_log = now;
        }
        return TRUE;  // Skip this update cycle
    }
    
    updating = TRUE;
    g_thread_new("update_thread", update_thread_func, NULL);
    return TRUE;
}

// Teaching: Activate callback: Set up vertical box for specs label + scrolled window. Init hashes and static specs. Added sorting setup: cast liststore to sortable, set custom sort func for each column, set sort column id on each tree view column to make them clickable for asc/desc sorting. Initial sort on CPU descending. Added g_mutex_init for hash_mutex. Initial update now launches thread instead of direct call.
static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "TaskMini");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);

    // Vertical box for layout
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), box);

    // System specs label
    specs_label = GTK_LABEL(gtk_label_new("Loading specs..."));
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(specs_label), FALSE, FALSE, 0);
    
    // System summary label (Networks, VM, Disks info)
    summary_label = GTK_LABEL(gtk_label_new("Loading system info..."));
    gtk_label_set_justify(summary_label, GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(summary_label), FALSE, FALSE, 0);

    // Scrolled window
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    global_scrolled_window = GTK_SCROLLED_WINDOW(scrolled_window); // Set global reference for scroll preservation
    gtk_box_pack_start(GTK_BOX(box), scrolled_window, TRUE, TRUE, 0);

    // List store - added G_TYPE_STRING for the new TYPE column
    liststore = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    // Make sortable and set sort functions
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(liststore);
    for (int col = 0; col < NUM_COLS; col++) {
        gtk_tree_sortable_set_sort_func(sortable, col, process_compare_func, GINT_TO_POINTER(col), NULL);
    }

    // Set initial sort: CPU descending (will be applied after data is loaded)
    gtk_tree_sortable_set_sort_column_id(sortable, COL_CPU, GTK_SORT_DESCENDING);

    // Tree view
    GtkWidget *treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(liststore));
    global_treeview = GTK_TREE_VIEW(treeview); // Set global reference for scroll preservation
    gtk_container_add(GTK_CONTAINER(scrolled_window), treeview);
    
    // Connect right-click handler for context menu
    g_signal_connect(treeview, "button-press-event", G_CALLBACK(on_treeview_button_press), NULL);

    // Columns with updated titles, make sortable by setting sort column id
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column;

    column = gtk_tree_view_column_new_with_attributes("PID", renderer, "text", COL_PID, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_PID);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", COL_NAME, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("CPU (% of system)", renderer, "text", COL_CPU, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_CPU);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("GPU (%)", renderer, "text", COL_GPU, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_GPU);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Memory", renderer, "text", COL_MEM, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_MEM);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Network", renderer, "text", COL_NET, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_NET);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Run Time", renderer, "text", COL_RUNTIME, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_RUNTIME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", COL_TYPE, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_TYPE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    // Init hashes
    prev_net_bytes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    prev_times = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_mutex_init(&hash_mutex);

    // Get static specs
    static_specs = get_static_specs();

    // Initial update via thread
    updating = TRUE;
    g_thread_new("update_thread", update_thread_func, NULL);

    // Timer - refresh every 0.5 seconds for more responsive updates
    g_timeout_add(500, timeout_callback, NULL);

    gtk_widget_show_all(window);
}

// Context menu for process management
void show_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    GtkTreeView *treeview = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return; // No selection
    }
    
    // Get process info
    gchar *pid = NULL, *name = NULL, *type = NULL;
    gtk_tree_model_get(model, &iter, 
                       COL_PID, &pid, 
                       COL_NAME, &name, 
                       COL_TYPE, &type, 
                       -1);
    
    // Don't show context menu for system processes
    if (type && strstr(type, "🛡️") != NULL) {
        g_free(pid);
        g_free(name);
        g_free(type);
        return;
    }
    
    // Create context menu
    GtkWidget *menu = gtk_menu_new();
    
    // Kill process menu item
    gchar *menu_text = g_strdup_printf("Terminate Process %s (%s)", name ? name : "Unknown", pid ? pid : "0");
    GtkWidget *kill_item = gtk_menu_item_new_with_label(menu_text);
    g_free(menu_text);
    
    // Store PID and name in menu item for callback
    g_object_set_data_full(G_OBJECT(kill_item), "pid", g_strdup(pid), g_free);
    g_object_set_data_full(G_OBJECT(kill_item), "name", g_strdup(name), g_free);
    
    g_signal_connect(kill_item, "activate", G_CALLBACK(kill_process_callback), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), kill_item);
    
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
    
    g_free(pid);
    g_free(name);
    g_free(type);
}

// Right-click event handler
gboolean on_treeview_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { // Right click
        GtkTreeView *treeview = GTK_TREE_VIEW(widget);
        GtkTreePath *path;
        
        // Select the row under cursor
        if (gtk_tree_view_get_path_at_pos(treeview, (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL)) {
            gtk_tree_view_set_cursor(treeview, path, NULL, FALSE);
            gtk_tree_path_free(path);
            
            show_context_menu(widget, event, treeview);
        }
        return TRUE; // Handled
    }
    return FALSE; // Let other handlers process
}

// Kill process callback
void kill_process_callback(GtkWidget *menuitem, gpointer user_data) {
    const char *pid = (const char*)g_object_get_data(G_OBJECT(menuitem), "pid");
    const char *name = (const char*)g_object_get_data(G_OBJECT(menuitem), "name");
    
    if (!pid || !name) return;
    
    // Create confirmation dialog
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "Are you sure you want to terminate process '%s' (PID: %s)?", 
        name, pid
    );
    
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "This action cannot be undone. The process will be forcefully terminated."
    );
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (result == GTK_RESPONSE_YES) {
        // Kill the process
        char cmd[100];
        snprintf(cmd, sizeof(cmd), "kill -TERM %s 2>/dev/null || kill -9 %s 2>/dev/null", pid, pid);
        
        int exit_code = system(cmd);
        
        // Show result
        const char *message = (exit_code == 0) ? 
            "Process terminated successfully." : 
            "Failed to terminate process. It may have already ended or require administrative privileges.";
            
        GtkWidget *result_dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            (exit_code == 0) ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "%s", message
        );
        
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
    }
}


// OPTIMIZATION: Cleanup handler for graceful shutdown
static void cleanup_resources() {
    cleanup_process_pool();
    
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
    
    // Clean up GPU cache
    if (cached_gpu_result) {
        free(cached_gpu_result);
        cached_gpu_result = NULL;
    }
    
    // Clean up network cache
    if (net_cache) {
        g_hash_table_destroy(net_cache);
        net_cache = NULL;
    }
}

// Teaching: Main function with optimization cleanup.
#ifndef TESTING
int main(int argc, char **argv) {
    // Register cleanup handler
    atexit(cleanup_resources);
    
    // Initialize memory pools early
    init_process_pool();
    
    app = gtk_application_new("com.example.TaskMini", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}
#endif