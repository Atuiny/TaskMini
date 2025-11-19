#include "../TaskMini.c"
#include "taskmini_tests.h"

// Integration tests - test full application workflows
// These tests verify end-to-end functionality

// Utility function for timing
double get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// Mock GTK functions for testing (simplified)
typedef struct _MockGtkListStore {
    int dummy; // Placeholder member
} MockGtkListStore;
typedef struct _MockGtkTreeIter {
    int dummy; // Placeholder member 
} MockGtkTreeIter;

static MockGtkListStore *mock_liststore = NULL;
static int mock_list_items = 0;

// Mock GTK functions
MockGtkListStore* mock_gtk_list_store_new(int n_columns, ...) {
    mock_liststore = malloc(sizeof(MockGtkListStore));
    mock_list_items = 0;
    return mock_liststore;
}

void mock_gtk_list_store_clear(MockGtkListStore *store) {
    mock_list_items = 0;
}

void mock_gtk_list_store_append(MockGtkListStore *store, MockGtkTreeIter *iter) {
    mock_list_items++;
}

// Test full update cycle
int test_full_update_cycle() {
    TEST_CASE("Full Update Cycle Integration Test");
    
    // Initialize all systems
    init_process_pool();
    
    // Mock the update data collection (normally done in background thread)
    UpdateData *update_data = malloc(sizeof(UpdateData));
    ASSERT_NOT_NULL(update_data, "Update data allocation should succeed");
    
    update_data->processes = NULL;
    update_data->gpu_usage = strdup("25.5%");
    update_data->system_summary = strdup("Network: 1GB downloaded, 500MB uploaded");
    
    // Create some test processes
    for (int i = 0; i < 5; i++) {
        Process *proc = alloc_process();
        snprintf(proc->pid, sizeof(proc->pid), "%d", 1000 + i);
        snprintf(proc->name, sizeof(proc->name), "TestProc%d", i);
        snprintf(proc->cpu, sizeof(proc->cpu), "%.1f", 5.0 + i);
        snprintf(proc->mem, sizeof(proc->mem), "%dMB", 100 + i * 50);
        snprintf(proc->gpu, sizeof(proc->gpu), "N/A");
        snprintf(proc->net, sizeof(proc->net), "%.1f KB/s", 1.0 + i);
        snprintf(proc->runtime, sizeof(proc->runtime), "00:%02d:30", i + 10);
        
        determine_process_type(proc);
        
        update_data->processes = g_list_append(update_data->processes, proc);
    }
    
    // Verify processes were created correctly
    int process_count = g_list_length(update_data->processes);
    ASSERT_EQUAL(5, process_count, "Should have created 5 test processes");
    
    // Verify GPU usage
    ASSERT_STR_EQUAL("25.5%", update_data->gpu_usage, "GPU usage should match");
    
    // Verify system summary
    ASSERT_TRUE(strstr(update_data->system_summary, "Network:") != NULL, 
                "System summary should contain network info");
    
    // Cleanup (simulate UI update function cleanup)
    GList *l;
    for (l = update_data->processes; l != NULL; l = l->next) {
        Process *proc = (Process *)l->data;
        free_process(proc);
    }
    g_list_free(update_data->processes);
    
    free(update_data->gpu_usage);
    free(update_data->system_summary);
    free(update_data);
    
    cleanup_process_pool();
    
    TEST_PASS();
}

// Test process filtering and type detection
int test_process_filtering() {
    TEST_CASE("Process Filtering and Type Detection");
    
    init_process_pool();
    
    // Create test processes with different types
    struct {
        const char *name;
        const char *pid;
        gboolean expected_system;
    } test_processes[] = {
        {"kernel_task", "0", TRUE},
        {"launchd", "1", TRUE},
        {"WindowServer", "100", TRUE},
        {"Safari", "1000", FALSE},
        {"Terminal", "2000", FALSE},
        {"systemstats", "50", TRUE},
        {"Google Chrome", "3000", FALSE}
    };
    
    int num_tests = sizeof(test_processes) / sizeof(test_processes[0]);
    
    for (int i = 0; i < num_tests; i++) {
        Process *proc = alloc_process();
        strcpy(proc->name, test_processes[i].name);
        strcpy(proc->pid, test_processes[i].pid);
        
        determine_process_type(proc);
        
        ASSERT_EQUAL(test_processes[i].expected_system, proc->is_system, 
                    "Process type detection should be correct");
        
        if (proc->is_system) {
            ASSERT_TRUE(strstr(proc->type, "System") != NULL, 
                       "System processes should have 'System' in type");
        } else {
            ASSERT_STR_EQUAL("User", proc->type, "User processes should show 'User'");
        }
        
        free_process(proc);
    }
    
    cleanup_process_pool();
    
    TEST_PASS();
}

// Test network data collection workflow
int test_network_workflow() {
    TEST_CASE("Network Data Collection Workflow");
    
    // Initialize network cache
    if (net_cache) {
        g_hash_table_destroy(net_cache);
    }
    net_cache = NULL;
    
    // Test cache initialization
    collect_all_network_data();
    ASSERT_NOT_NULL(net_cache, "Network cache should be initialized");
    
    // Test cache usage
    long long bytes1 = get_net_bytes("123");  // This should use cache
    long long bytes2 = get_net_bytes("456");  // This should also use cache
    
    ASSERT_TRUE(bytes1 >= 0, "Network bytes should be non-negative");
    ASSERT_TRUE(bytes2 >= 0, "Network bytes should be non-negative");
    
    // Test cache expiration by advancing time
    last_net_collection = time(NULL) - 2;  // Force cache expiration
    
    // This should trigger a new collection
    long long bytes3 = get_net_bytes("789");
    ASSERT_TRUE(bytes3 >= 0, "Network bytes after cache refresh should be non-negative");
    
    // Cleanup
    if (net_cache) {
        g_hash_table_destroy(net_cache);
        net_cache = NULL;
    }
    
    TEST_PASS();
}

// Test GPU detection workflow
int test_gpu_workflow() {
    TEST_CASE("GPU Detection Workflow");
    
    // Reset GPU detection state
    powermetrics_unavailable = FALSE;
    if (cached_gpu_result) {
        free(cached_gpu_result);
        cached_gpu_result = NULL;
    }
    
    // Test initial GPU detection (should try powermetrics first)
    char *gpu1 = get_gpu_usage();
    ASSERT_NOT_NULL(gpu1, "GPU detection should return a result");
    ASSERT_TRUE(strlen(gpu1) > 0, "GPU result should not be empty");
    
    // Test cached result (should be faster)
    double start_time = get_current_time();
    char *gpu2 = get_gpu_usage();
    double end_time = get_current_time();
    
    ASSERT_NOT_NULL(gpu2, "Cached GPU detection should return result");
    ASSERT_TRUE((end_time - start_time) < 0.1, "Cached GPU detection should be fast");
    
    // Test fallback system
    powermetrics_unavailable = TRUE;  // Force fallback
    if (cached_gpu_result) {
        free(cached_gpu_result);
        cached_gpu_result = NULL;
    }
    
    char *gpu3 = get_gpu_usage();
    ASSERT_NOT_NULL(gpu3, "Fallback GPU detection should work");
    
    // Verify fallback provides reasonable results
    ASSERT_TRUE(strstr(gpu3, "Idle") || strstr(gpu3, "Light") || 
                strstr(gpu3, "Active") || strstr(gpu3, "Busy") || 
                strstr(gpu3, "Heavy") || strstr(gpu3, "%") || 
                strstr(gpu3, "N/A"), "GPU fallback should provide status or percentage");
    
    free(gpu1);
    free(gpu2);
    free(gpu3);
    
    TEST_PASS();
}

// Test system information collection
int test_system_info_workflow() {
    TEST_CASE("System Information Collection Workflow");
    
    // Test static specs generation
    char *specs = get_static_specs();
    ASSERT_NOT_NULL(specs, "System specs should be generated");
    
    // Verify required information is present
    ASSERT_TRUE(strstr(specs, "Machine:") != NULL, "Should contain machine info");
    ASSERT_TRUE(strstr(specs, "Processor:") != NULL, "Should contain processor info");
    ASSERT_TRUE(strstr(specs, "Memory:") != NULL, "Should contain memory info");
    ASSERT_TRUE(strstr(specs, "Storage:") != NULL, "Should contain storage info");
    ASSERT_TRUE(strstr(specs, "Graphics:") != NULL, "Should contain graphics info");
    ASSERT_TRUE(strstr(specs, "System:") != NULL, "Should contain system info");
    ASSERT_TRUE(strstr(specs, "Serial:") != NULL, "Should contain serial info");
    
    // Verify no N/A values in critical fields (after our fixes)
    char *machine_line = strstr(specs, "Machine:");
    if (machine_line) {
        char *line_end = strchr(machine_line, '\n');
        if (line_end) {
            *line_end = '\0';  // Temporarily terminate
            ASSERT_FALSE(strstr(machine_line, "N/A (") != NULL, 
                        "Machine name should not be N/A");
            *line_end = '\n';  // Restore
        }
    }
    
    free(specs);
    
    TEST_PASS();
}

// Test error recovery workflow
int test_error_recovery_workflow() {
    TEST_CASE("Error Recovery Workflow");
    
    // Test command failure handling
    char *result = run_command("nonexistent_command_xyz_123");
    ASSERT_STR_EQUAL("N/A", result, "Failed commands should return N/A");
    free(result);
    
    // Test security blocking
    result = run_command("rm -rf /");
    ASSERT_STR_EQUAL("N/A", result, "Blocked commands should return N/A");
    free(result);
    
    // Test memory allocation failure simulation
    // (This is harder to test without actually exhausting memory)
    
    // Test invalid input handling
    ASSERT_FALSE(is_system_process(NULL, "123"), "NULL process name should return FALSE");
    ASSERT_FALSE(is_system_process("test", NULL), "NULL PID should return FALSE");
    
    // Test buffer safety
    char small_buf[5];
    safe_strncpy(small_buf, "This is way too long", sizeof(small_buf));
    ASSERT_EQUAL(4, strlen(small_buf), "Should truncate to fit buffer");
    ASSERT_EQUAL('\0', small_buf[4], "Should null terminate");
    
    TEST_PASS();
}

// Test performance under load
int test_performance_workflow() {
    TEST_CASE("Performance Under Load Workflow");
    
    init_process_pool();
    
    double start_time = get_current_time();
    
    // Simulate a heavy update cycle
    GList *processes = NULL;
    
    for (int i = 0; i < 100; i++) {
        Process *proc = alloc_process();
        
        snprintf(proc->pid, sizeof(proc->pid), "%d", i + 1000);
        snprintf(proc->name, sizeof(proc->name), "Process%d", i);
        snprintf(proc->cpu, sizeof(proc->cpu), "%.1f", (double)(i % 50));
        
        char *formatted = format_memory_human_readable("512M");
        safe_strncpy(proc->mem, formatted, sizeof(proc->mem));
        free(formatted);
        
        determine_process_type(proc);
        
        processes = g_list_append(processes, proc);
    }
    
    double end_time = get_current_time();
    double duration = end_time - start_time;
    
    printf("(%.3fs for 100 processes) ", duration);
    ASSERT_TRUE(duration < 1.0, "Should process 100 items in under 1 second");
    
    // Cleanup
    GList *l;
    for (l = processes; l != NULL; l = l->next) {
        free_process((Process*)l->data);
    }
    g_list_free(processes);
    
    cleanup_process_pool();
    
    TEST_PASS();
}

// Main integration test runner
int main() {
    printf("TaskMini Integration Test Suite\n");
    printf("===============================\n");
    
    TEST_SUITE("Integration and Workflow Tests");
    
    // Run integration tests
    test_full_update_cycle();
    test_process_filtering();
    test_network_workflow();
    test_gpu_workflow();
    test_system_info_workflow();
    test_error_recovery_workflow();
    test_performance_workflow();
    
    TEST_SUMMARY();
}
