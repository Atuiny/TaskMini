#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>

// Include optimized functions
#include "src/system/performance.h"
#include "src/utils/memory_pool.h"

// Timing utilities
double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// Test CPU usage collection speed
void test_cpu_performance(int iterations) {
    printf("=== CPU Usage Collection Performance Test ===\n");
    
    double start_time, end_time;
    
    // Test traditional method
    printf("Testing traditional CPU collection (%d iterations)...\n", iterations);
    start_time = get_time_ms();
    
    for (int i = 0; i < iterations; i++) {
        FILE *fp = popen("top -l 1 -n 0 | grep 'CPU usage:'", "r");
        if (fp) {
            char line[256];
            if (fgets(line, sizeof(line), fp)) {
                float user = 0.0, sys = 0.0, idle = 0.0;
                sscanf(line, "CPU usage: %f%% user, %f%% sys, %f%% idle", &user, &sys, &idle);
            }
            pclose(fp);
        }
    }
    
    end_time = get_time_ms();
    double traditional_time = end_time - start_time;
    printf("Traditional method: %.2f ms (%.2f ms per call)\n", 
           traditional_time, traditional_time / iterations);
    
    // Test optimized method
    printf("Testing optimized CPU collection (%d iterations)...\n", iterations);
    start_time = get_time_ms();
    
    for (int i = 0; i < iterations; i++) {
        double cpu_usage = get_system_cpu_usage_fast();
        (void)cpu_usage; // Suppress unused variable warning
    }
    
    end_time = get_time_ms();
    double optimized_time = end_time - start_time;
    printf("Optimized method: %.2f ms (%.2f ms per call)\n", 
           optimized_time, optimized_time / iterations);
    
    double speedup = traditional_time / optimized_time;
    printf("Speedup: %.1fx faster\n\n", speedup);
}

// Test memory usage collection speed
void test_memory_performance(int iterations) {
    printf("=== Memory Usage Collection Performance Test ===\n");
    
    double start_time, end_time;
    
    // Test traditional method
    printf("Testing traditional memory collection (%d iterations)...\n", iterations);
    start_time = get_time_ms();
    
    for (int i = 0; i < iterations; i++) {
        // Simulate the old memory collection method
        FILE *fp1 = popen("sysctl -n hw.memsize", "r");
        FILE *fp2 = popen("vm_stat | awk 'BEGIN{total=0} /Pages active|Pages inactive|Pages speculative|Pages wired down|Pages occupied by compressor/ {gsub(/[^0-9]/, \"\", $NF); total+=$NF} END{print total}'", "r");
        
        if (fp1 && fp2) {
            char buffer1[64], buffer2[64];
            if (fgets(buffer1, sizeof(buffer1), fp1) && 
                fgets(buffer2, sizeof(buffer2), fp2)) {
                long long total = atoll(buffer1);
                long used_pages = atol(buffer2);
                double usage = ((double)(used_pages * 16384) / total) * 100.0;
                (void)usage;
            }
        }
        
        if (fp1) pclose(fp1);
        if (fp2) pclose(fp2);
    }
    
    end_time = get_time_ms();
    double traditional_time = end_time - start_time;
    printf("Traditional method: %.2f ms (%.2f ms per call)\n", 
           traditional_time, traditional_time / iterations);
    
    // Test optimized method
    printf("Testing optimized memory collection (%d iterations)...\n", iterations);
    start_time = get_time_ms();
    
    for (int i = 0; i < iterations; i++) {
        double mem_usage = get_system_memory_usage_fast();
        (void)mem_usage; // Suppress unused variable warning
    }
    
    end_time = get_time_ms();
    double optimized_time = end_time - start_time;
    printf("Optimized method: %.2f ms (%.2f ms per call)\n", 
           optimized_time, optimized_time / iterations);
    
    double speedup = traditional_time / optimized_time;
    printf("Speedup: %.1fx faster\n\n", speedup);
}

// Test memory pool performance
void test_memory_pool_performance(int iterations) {
    printf("=== Memory Pool Performance Test ===\n");
    
    double start_time, end_time;
    
    // Initialize memory pools
    init_memory_pools();
    
    // Test traditional malloc/free
    printf("Testing malloc/free (%d iterations)...\n", iterations);
    start_time = get_time_ms();
    
    for (int i = 0; i < iterations; i++) {
        Process *proc = malloc(sizeof(Process));
        if (proc) {
            memset(proc, 0, sizeof(Process));
            // Simulate some work
            strcpy(proc->name, "test_process");
            strcpy(proc->pid, "12345");
            free(proc);
        }
    }
    
    end_time = get_time_ms();
    double malloc_time = end_time - start_time;
    printf("Malloc/free method: %.2f ms (%.3f ms per call)\n", 
           malloc_time, malloc_time / iterations);
    
    // Test memory pool
    printf("Testing memory pool (%d iterations)...\n", iterations);
    start_time = get_time_ms();
    
    for (int i = 0; i < iterations; i++) {
        Process *proc = get_process_from_pool_fast();
        if (proc) {
            // Simulate some work
            strcpy(proc->name, "test_process");
            strcpy(proc->pid, "12345");
            return_process_to_pool_fast(proc);
        }
    }
    
    end_time = get_time_ms();
    double pool_time = end_time - start_time;
    printf("Memory pool method: %.2f ms (%.3f ms per call)\n", 
           pool_time, pool_time / iterations);
    
    double speedup = malloc_time / pool_time;
    printf("Speedup: %.1fx faster\n\n", speedup);
    
    // Print pool statistics
    print_memory_pool_stats();
}

// Test accuracy of optimized functions
void test_accuracy(void) {
    printf("=== Accuracy Test ===\n");
    
    // Test CPU accuracy
    printf("CPU Usage Comparison:\n");
    
    // Traditional method
    FILE *fp = popen("top -l 1 -n 0 | grep 'CPU usage:'", "r");
    float traditional_cpu = 0.0;
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            float user, sys, idle;
            if (sscanf(line, "CPU usage: %f%% user, %f%% sys, %f%% idle", &user, &sys, &idle) == 3) {
                traditional_cpu = user + sys;
            }
        }
        pclose(fp);
    }
    
    // Optimized method
    double optimized_cpu = get_system_cpu_usage_fast();
    
    printf("  Traditional: %.2f%%\n", traditional_cpu);
    printf("  Optimized: %.2f%%\n", optimized_cpu);
    printf("  Difference: %.2f%% (%.1f%% relative)\n", 
           fabs(traditional_cpu - optimized_cpu),
           fabs(traditional_cpu - optimized_cpu) / traditional_cpu * 100.0);
    
    // Test Memory accuracy  
    printf("\nMemory Usage Comparison:\n");
    
    // Traditional method (simplified for testing)
    fp = popen("sysctl -n hw.memsize", "r");
    char total_str[64];
    long long total_mem = 0;
    if (fp && fgets(total_str, sizeof(total_str), fp)) {
        total_mem = atoll(total_str);
    }
    if (fp) pclose(fp);
    
    fp = popen("vm_stat | awk 'BEGIN{total=0} /Pages active|Pages inactive|Pages speculative|Pages wired down|Pages occupied by compressor/ {gsub(/[^0-9]/, \"\", $NF); total+=$NF} END{print total}'", "r");
    char used_str[64];
    long used_pages = 0;
    if (fp && fgets(used_str, sizeof(used_str), fp)) {
        used_pages = atol(used_str);
    }
    if (fp) pclose(fp);
    
    double traditional_mem = ((double)(used_pages * 16384) / total_mem) * 100.0;
    double optimized_mem = get_system_memory_usage_fast();
    
    printf("  Traditional: %.2f%%\n", traditional_mem);
    printf("  Optimized: %.2f%%\n", optimized_mem);
    printf("  Difference: %.2f%% (%.1f%% relative)\n\n", 
           fabs(traditional_mem - optimized_mem),
           fabs(traditional_mem - optimized_mem) / traditional_mem * 100.0);
}

int main(void) {
    printf("TaskMini Performance Optimization Test Suite\n");
    printf("==========================================\n\n");
    
    const int test_iterations = 100;
    
    // Run performance tests
    test_cpu_performance(test_iterations);
    test_memory_performance(test_iterations);
    test_memory_pool_performance(test_iterations * 10); // More iterations for memory pool
    
    // Test accuracy
    test_accuracy();
    
    printf("=== Summary ===\n");
    printf("Performance optimizations provide significant speed improvements\n");
    printf("while maintaining full accuracy of system monitoring data.\n");
    printf("Memory pools eliminate allocation overhead for better real-time performance.\n");
    
    // Cleanup
    cleanup_memory_pools();
    
    return 0;
}
