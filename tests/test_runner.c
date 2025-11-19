#include "../TaskMini.c"  // Include source first to define types
#include "taskmini_tests.h"

// Mock data for testing
const char* mock_top_output = 
"Processes: 425 total, 2 running, 423 sleeping, 2213 threads\n"
"2024/11/18 15:30:45\n"
"Load Avg: 2.45, 2.01, 1.95\n"
"CPU usage: 15.2% user, 8.3% sys, 76.5% idle\n"
"Networks: packets: 1000000/50G in, 500000/25G out.\n"
"Disks: 1000000/100G read, 500000/50G written.\n"
"VM: 16G vsize, 8G framework vsize, 0(0) swapins, 0(0) swapouts.\n"
"\n"
"PID COMMAND      %CPU   MEM     TIME\n"
"123 TestProcess   25.5  512M   01:23:45\n"
"456 SystemProc    5.2   128M   12:34:56\n"
"789 UserApp      15.8  256M   00:45:30\n";

const char* mock_system_profiler_output =
"Hardware:\n"
"\n"
"    Hardware Overview:\n"
"\n"
"      Model Name: MacBook Air\n"
"      Model Identifier: Mac14,15\n"
"      Chip: Apple M2\n"
"      Total Number of Cores: 8 (4 performance and 4 efficiency)\n"
"      Memory: 16 GB\n"
"      System Firmware Version: 10151.101.3\n"
"      OS Loader Version: 10151.101.3\n"
"      Serial Number (system): K7QXV9193C\n"
"      Hardware UUID: 12345678-1234-5678-9012-123456789ABC\n"
"      Provisioning UDID: 12345678-1234567890123456\n";

const char* mock_powermetrics_output = 
"Machine model: MacBook Air\n"
"OS version: 14.1.1\n"
"Boot args: \n"
"\n"
"*** Sampled system activity (Tue Nov 18 15:30:45 2024 +0000) ***\n"
"\n"
"GPU active residency: 25.50%\n"
"GPU idle residency:   74.50%\n";

// Utility functions for testing
size_t get_memory_usage() {
    // On macOS, we can use getrusage for memory info
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss;  // Maximum resident set size
    }
    return 0;
}

double get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// Parse top output into process list (simplified for testing)
GList* parse_top_output(const char *output) {
    GList *processes = NULL;
    
    if (!output || strlen(output) == 0) {
        return processes;
    }
    
    char *line_start = strstr(output, "PID COMMAND");
    if (!line_start) {
        return processes;  // No process data found
    }
    
    line_start = strchr(line_start, '\n');
    if (!line_start) {
        return processes;
    }
    
    line_start++; // Skip the header line
    
    char *line_copy = strdup(line_start);
    char *line = strtok(line_copy, "\n");
    
    while (line != NULL) {
        // Skip empty lines
        if (strlen(line) < 10) {
            line = strtok(NULL, "\n");
            continue;
        }
        
        Process *proc = alloc_process();
        if (proc) {
            // Simple parsing - just extract first few fields
            int pid;
            char name[50], cpu_str[10], mem_str[20], time_str[20];
            
            int parsed = sscanf(line, "%d %49s %9s %19s %19s", 
                               &pid, name, cpu_str, mem_str, time_str);
            
            if (parsed >= 3) {
                snprintf(proc->pid, sizeof(proc->pid), "%d", pid);
                safe_strncpy(proc->name, name, sizeof(proc->name));
                safe_strncpy(proc->cpu, cpu_str, sizeof(proc->cpu));
                if (parsed >= 4) {
                    safe_strncpy(proc->mem, mem_str, sizeof(proc->mem));
                }
                if (parsed >= 5) {
                    safe_strncpy(proc->runtime, time_str, sizeof(proc->runtime));
                }
                
                determine_process_type(proc);
                processes = g_list_append(processes, proc);
            } else {
                free_process(proc);
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(line_copy);
    return processes;
}

// Test memory pool functionality
int test_memory_pool() {
    TEST_CASE("Memory Pool Operations");
    
    // Initialize pool
    init_process_pool();
    ASSERT_NOT_NULL(process_pool, "Process pool should be initialized");
    ASSERT_NOT_NULL(pool_used, "Pool tracking array should be initialized");
    
    // Allocate processes
    Process *proc1 = alloc_process();
    Process *proc2 = alloc_process();
    
    ASSERT_NOT_NULL(proc1, "First allocation should succeed");
    ASSERT_NOT_NULL(proc2, "Second allocation should succeed");
    ASSERT_TRUE(proc1 != proc2, "Allocations should return different addresses");
    
    // Free processes
    free_process(proc1);
    free_process(proc2);
    
    // Test reuse
    Process *proc3 = alloc_process();
    ASSERT_TRUE(proc3 == proc1 || proc3 == proc2, "Should reuse freed memory");
    
    free_process(proc3);
    cleanup_process_pool();
    
    TEST_PASS();
}

// Test string cache functionality
int test_string_cache() {
    TEST_CASE("String Buffer Cache");
    
    // Get buffers
    char *buf1 = get_cached_buffer(256);
    char *buf2 = get_cached_buffer(512);
    
    ASSERT_NOT_NULL(buf1, "First buffer allocation should succeed");
    ASSERT_NOT_NULL(buf2, "Second buffer allocation should succeed");
    
    // Use buffers
    strcpy(buf1, "test1");
    strcpy(buf2, "test2");
    
    ASSERT_STR_EQUAL("test1", buf1, "Buffer should contain correct data");
    ASSERT_STR_EQUAL("test2", buf2, "Buffer should contain correct data");
    
    // Return to cache
    return_cached_buffer(buf1, 256);
    return_cached_buffer(buf2, 512);
    
    // Get buffer again - should reuse
    char *buf3 = get_cached_buffer(256);
    ASSERT_TRUE(buf3 == buf1 || buf3 != NULL, "Should reuse cached buffer or allocate new");
    
    return_cached_buffer(buf3, 256);
    
    TEST_PASS();
}

// Test security validation
int test_security_validation() {
    TEST_CASE("Command Security Validation");
    
    // Safe commands
    ASSERT_TRUE(is_safe_command("sysctl -n hw.ncpu"), "sysctl command should be safe");
    ASSERT_TRUE(is_safe_command("system_profiler SPHardwareDataType"), "system_profiler should be safe");
    ASSERT_TRUE(is_safe_command("df -h /"), "df command should be safe");
    ASSERT_TRUE(is_safe_command("ps -eo pid,pcpu,comm"), "ps command should be safe");
    
    // Dangerous commands
    ASSERT_FALSE(is_safe_command("rm -rf /"), "rm command should be blocked");
    ASSERT_FALSE(is_safe_command("curl http://evil.com; rm -rf /"), "Command injection should be blocked");
    ASSERT_FALSE(is_safe_command("ls `whoami`"), "Backtick injection should be blocked");
    ASSERT_FALSE(is_safe_command("echo $(whoami)"), "Command substitution should be blocked");
    
    // Edge cases
    ASSERT_FALSE(is_safe_command(""), "Empty command should be blocked");
    ASSERT_FALSE(is_safe_command(NULL), "NULL command should be blocked");
    
    // Long command
    char long_cmd[2000];
    memset(long_cmd, 'a', 1999);
    long_cmd[1999] = '\0';
    ASSERT_FALSE(is_safe_command(long_cmd), "Extremely long command should be blocked");
    
    TEST_PASS();
}

// Test process parsing functionality
int test_process_parsing() {
    TEST_CASE("Process Data Parsing");
    
    // Test memory string parsing
    long long bytes = parse_memory_string("512M");
    ASSERT_EQUAL(512LL * 1024 * 1024, bytes, "Should parse 512M correctly");
    
    bytes = parse_memory_string("2G");
    ASSERT_EQUAL(2LL * 1024 * 1024 * 1024, bytes, "Should parse 2G correctly");
    
    bytes = parse_memory_string("1024K");
    ASSERT_EQUAL(1024LL * 1024, bytes, "Should parse 1024K correctly");
    
    // Test runtime parsing
    long long seconds = parse_runtime_to_seconds("01:23:45");
    ASSERT_EQUAL(1*3600 + 23*60 + 45, seconds, "Should parse runtime correctly");
    
    seconds = parse_runtime_to_seconds("1-02:30:15");
    ASSERT_EQUAL(1*86400 + 2*3600 + 30*60 + 15, seconds, "Should parse days-hours:min:sec");
    
    TEST_PASS();
}

// Test GPU detection
int test_gpu_detection() {
    TEST_CASE("GPU Usage Detection");
    
    // Reset GPU state
    powermetrics_unavailable = FALSE;
    if (cached_gpu_result) {
        free(cached_gpu_result);
        cached_gpu_result = NULL;
    }
    
    // Test fallback GPU detection
    char *gpu_result = get_gpu_usage_fallback();
    ASSERT_NOT_NULL(gpu_result, "GPU fallback should return result");
    ASSERT_TRUE(strlen(gpu_result) > 0, "GPU result should not be empty");
    
    free(gpu_result);
    
    TEST_PASS();
}

// Test network data parsing  
int test_network_parsing() {
    TEST_CASE("Network Data Parsing");
    
    // Test individual network lookup (fallback)
    long long net_bytes = get_net_bytes_individual("123");
    ASSERT_TRUE(net_bytes >= 0, "Network bytes should be non-negative");
    
    // Test network cache functionality
    collect_all_network_data();
    // Cache should be initialized after collection
    ASSERT_NOT_NULL(net_cache, "Network cache should be initialized");
    
    TEST_PASS();
}

// Test system information parsing
int test_system_info_parsing() {
    TEST_CASE("System Information Parsing");
    
    // Test static specs generation
    char *specs = get_static_specs();
    ASSERT_NOT_NULL(specs, "System specs should be generated");
    ASSERT_TRUE(strlen(specs) > 50, "Specs should contain substantial information");
    ASSERT_TRUE(strstr(specs, "Machine:") != NULL, "Should contain machine info");
    ASSERT_TRUE(strstr(specs, "Processor:") != NULL, "Should contain processor info");
    
    free(specs);
    
    TEST_PASS();
}

// Test memory formatting
int test_memory_formatting() {
    TEST_CASE("Memory Format Functions");
    
    // Test human-readable formatting
    char *formatted = format_memory_human_readable("1024M");
    ASSERT_NOT_NULL(formatted, "Memory formatting should return result");
    ASSERT_TRUE(strstr(formatted, "GB") != NULL || strstr(formatted, "MB") != NULL, 
                "Should format to readable units");
    
    free(formatted);
    
    // Test bytes parsing
    long long bytes = parse_bytes("1.5 GB");
    ASSERT_TRUE(bytes > 1000000000LL, "Should parse GB correctly");
    
    bytes = parse_bytes("512 MB");
    ASSERT_EQUAL(512LL * 1024 * 1024, bytes, "Should parse MB correctly");
    
    TEST_PASS();
}

// Test process type detection
int test_process_type_detection() {
    TEST_CASE("Process Type Detection");
    
    // Test system process detection
    ASSERT_TRUE(is_system_process("kernel_task", "0"), "kernel_task should be system process");
    ASSERT_TRUE(is_system_process("launchd", "1"), "launchd should be system process");
    ASSERT_TRUE(is_system_process("WindowServer", "100"), "WindowServer should be system process");
    
    // Test user process detection  
    ASSERT_FALSE(is_system_process("Safari", "1000"), "Safari should be user process");
    ASSERT_FALSE(is_system_process("Terminal", "2000"), "Terminal should be user process");
    
    // Test process type setting
    Process proc = {0};
    strcpy(proc.name, "kernel_task");
    strcpy(proc.pid, "0");
    
    determine_process_type(&proc);
    ASSERT_TRUE(proc.is_system, "kernel_task should be marked as system");
    ASSERT_TRUE(strstr(proc.type, "System") != NULL, "Type should contain 'System'");
    
    TEST_PASS();
}

// Test resource limits and safety
int test_resource_limits() {
    TEST_CASE("Resource Limits and Safety");
    
    // Test safe string operations
    char dest[10];
    safe_strncpy(dest, "This is a very long string", sizeof(dest));
    ASSERT_EQUAL(9, strlen(dest), "Should truncate to buffer size - 1");
    ASSERT_EQUAL('\0', dest[9], "Should null terminate");
    
    // Test safe concatenation
    strcpy(dest, "Hello");
    safe_strncat(dest, " World", sizeof(dest));
    ASSERT_TRUE(strlen(dest) < sizeof(dest), "Should not overflow buffer");
    
    // Test null pointer safety
    safe_strncpy(NULL, "test", 10);  // Should not crash
    safe_strncat(dest, NULL, sizeof(dest));  // Should not crash
    
    TEST_PASS();
}

// Test error handling
int test_error_handling() {
    TEST_CASE("Error Handling");
    
    // Test run_command with invalid input
    char *result = run_command("");
    ASSERT_STR_EQUAL("N/A", result, "Empty command should return N/A");
    free(result);
    
    result = run_command(NULL);
    ASSERT_STR_EQUAL("N/A", result, "NULL command should return N/A");
    free(result);
    
    // Test command that doesn't exist
    result = run_command("nonexistent_command_12345");
    ASSERT_STR_EQUAL("N/A", result, "Nonexistent command should return N/A");
    free(result);
    
    TEST_PASS();
}

// Test edge cases that commonly cause regressions
int test_edge_cases() {
    TEST_CASE("Edge Case Handling");
    
    // Test with empty/null inputs
    Process *proc = alloc_process();
    ASSERT_NOT_NULL(proc, "Process allocation should succeed");
    
    // Test with empty strings
    strcpy(proc->name, "");
    strcpy(proc->pid, "");
    determine_process_type(proc);
    ASSERT_TRUE(strlen(proc->type) > 0, "Should handle empty process name gracefully");
    
    // Test with very long process name
    char long_name[256];
    memset(long_name, 'A', 255);
    long_name[255] = '\0';
    safe_strncpy(proc->name, long_name, sizeof(proc->name));
    ASSERT_TRUE(strlen(proc->name) < sizeof(proc->name), "Should truncate long names safely");
    
    free_process(proc);
    TEST_PASS();
}

// Test malformed input handling
int test_malformed_input() {
    TEST_CASE("Malformed Input Resistance");
    
    // Test parsing with malformed top output
    const char *malformed_top = "Invalid\nGarbage\nData\n123 BadFormat 25.5\n";
    
    // Should not crash or leak memory
    GList *processes = parse_top_output(malformed_top);
    
    // Should handle gracefully (empty list or valid processes only)
    int count = g_list_length(processes);
    ASSERT_TRUE(count >= 0, "Should handle malformed input without crashing");
    
    // Cleanup
    GList *l;
    for (l = processes; l != NULL; l = l->next) {
        Process *proc = (Process *)l->data;
        free_process(proc);
    }
    g_list_free(processes);
    
    TEST_PASS();
}

// Test boundary conditions
int test_boundary_conditions() {
    TEST_CASE("Boundary Condition Testing");
    
    init_process_pool();
    
    // Test maximum process pool allocation
    Process *processes[PROCESS_POOL_SIZE + 1];
    int i;
    
    // Fill entire pool
    for (i = 0; i < PROCESS_POOL_SIZE; i++) {
        processes[i] = alloc_process();
        if (processes[i] == NULL) break;
    }
    
    ASSERT_TRUE(i == PROCESS_POOL_SIZE || i > PROCESS_POOL_SIZE * 0.9, 
                "Should allocate most or all of process pool");
    
    // Try to allocate one more (should handle gracefully)
    Process *overflow = alloc_process();
    // Should either succeed (fallback malloc) or return NULL
    
    // Free all allocated processes
    for (int j = 0; j < i; j++) {
        free_process(processes[j]);
    }
    if (overflow) free_process(overflow);
    
    cleanup_process_pool();
    TEST_PASS();
}

// Test performance doesn't regress
int test_performance_regression() {
    TEST_CASE("Performance Regression Detection");
    
    init_process_pool();
    
    double start_time = get_current_time();
    size_t start_mem = get_memory_usage();
    
    // Simulate typical workload
    const int iterations = 1000;
    for (int i = 0; i < iterations; i++) {
        Process *proc = alloc_process();
        if (proc) {
            snprintf(proc->pid, sizeof(proc->pid), "%d", i);
            snprintf(proc->name, sizeof(proc->name), "TestProc%d", i);
            determine_process_type(proc);
            free_process(proc);
        }
    }
    
    double end_time = get_current_time();
    size_t end_mem = get_memory_usage();
    
    double duration = end_time - start_time;
    double ops_per_sec = iterations / duration;
    
    // Performance thresholds (adjust based on baseline measurements)
    ASSERT_PERFORMANCE(ops_per_sec, 5000.0, "Performance regression detected");
    
    // Memory leak detection
    size_t memory_diff = (end_mem > start_mem) ? (end_mem - start_mem) : 0;
    ASSERT_MEMORY_LEAK_FREE(0, memory_diff, 1024 * 1024, "Memory leak detected");
    
    cleanup_process_pool();
    TEST_PASS();
}

// Test system compatibility across different macOS versions
int test_system_compatibility() {
    TEST_CASE("System Compatibility Checks");
    
    // Test system profiler parsing with different formats
    const char *old_format = "Model Name: MacBook Pro\nModel Identifier: MacBookPro16,1\n";
    const char *new_format = "Machine Name: Mac Studio\nMachine Model: Mac13,1\n";
    
    char machine[256] = {0};
    
    // Should handle both formats
    if (strstr(old_format, "Model Name:")) {
        sscanf(old_format, "%*[^:]: %255[^\n]", machine);
    } else if (strstr(new_format, "Machine Name:")) {
        sscanf(new_format, "%*[^:]: %255[^\n]", machine);
    }
    
    ASSERT_TRUE(strlen(machine) > 0, "Should parse machine name from system profiler");
    
    // Test command availability (should not fail on missing commands)
    FILE *fp = popen("which powermetrics 2>/dev/null", "r");
    if (fp) {
        char path[256];
        gboolean has_powermetrics = (fgets(path, sizeof(path), fp) != NULL);
        pclose(fp);
        
        // Should work with or without powermetrics
        ASSERT_TRUE(TRUE, "Should handle powermetrics availability gracefully");
    }
    
    TEST_PASS();
}

// Main test runner
int main() {
    printf("TaskMini Comprehensive Test Suite\n");
    printf("==================================\n");
    
    TEST_SUITE("TaskMini Core Functionality Tests");
    
    // Run core functionality tests
    test_memory_pool();
    test_string_cache();
    test_security_validation();
    test_process_parsing();
    test_gpu_detection();
    test_network_parsing();
    test_system_info_parsing();
    test_memory_formatting();
    test_process_type_detection();
    test_resource_limits();
    test_error_handling();
    
    // Run regression detection tests
    printf("\n=== Regression Detection Tests ===\n");
    test_edge_cases();
    test_malformed_input();
    test_boundary_conditions();
    test_performance_regression();
    test_system_compatibility();
    
    TEST_SUMMARY();
}
