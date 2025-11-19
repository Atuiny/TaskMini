#ifndef TASKMINI_TESTS_H
#define TASKMINI_TESTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>

// Global test counters - declared at file scope
static int test_count = 0;
static int test_passed = 0;
static int test_failed = 0;

// Test framework macros
#define TEST_SUITE(name) \
    test_count = 0; \
    test_passed = 0; \
    test_failed = 0; \
    printf("\n=== %s ===\n", name);

#define TEST_CASE(name) \
    printf("  Running: %s... ", name); \
    test_count++;

#define ASSERT_TRUE(condition, message) \
    if (!(condition)) { \
        printf("FAILED\n    Assertion failed: %s\n", message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_FALSE(condition, message) \
    if (condition) { \
        printf("FAILED\n    Assertion failed: %s\n", message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_EQUAL(expected, actual, message) \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: %ld, Got: %ld - %s\n", (long)(expected), (long)(actual), message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_STR_EQUAL(expected, actual, message) \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAILED\n    Expected: '%s', Got: '%s' - %s\n", expected, actual, message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_NOT_NULL(ptr, message) \
    if ((ptr) == NULL) { \
        printf("FAILED\n    Pointer is NULL: %s\n", message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_NULL(ptr, message) \
    if ((ptr) != NULL) { \
        printf("FAILED\n    Pointer should be NULL: %s\n", message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_RANGE(value, min, max, message) \
    if ((value) < (min) || (value) > (max)) { \
        printf("FAILED\n    Value %ld out of range [%ld, %ld]: %s\n", \
               (long)(value), (long)(min), (long)(max), message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_STR_CONTAINS(haystack, needle, message) \
    if (strstr((haystack), (needle)) == NULL) { \
        printf("FAILED\n    String '%s' does not contain '%s': %s\n", \
               haystack, needle, message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_MEMORY_LEAK_FREE(start_mem, end_mem, threshold, message) \
    if ((end_mem) - (start_mem) > (threshold)) { \
        printf("FAILED\n    Memory leak detected: %ld bytes: %s\n", \
               (long)((end_mem) - (start_mem)), message); \
        test_failed++; \
        return 0; \
    }

#define ASSERT_PERFORMANCE(ops_per_sec, min_threshold, message) \
    if ((ops_per_sec) < (min_threshold)) { \
        printf("FAILED\n    Performance too low: %.2f ops/sec (min: %.2f): %s\n", \
               (double)(ops_per_sec), (double)(min_threshold), message); \
        test_failed++; \
        return 0; \
    }

#define TEST_PASS() \
    printf("PASSED\n"); \
    test_passed++; \
    return 1;

#define TEST_SUMMARY() \
    printf("\n=== Test Summary ===\n"); \
    printf("Total Tests: %d\n", test_count); \
    printf("Passed: %d\n", test_passed); \
    printf("Failed: %d\n", test_failed); \
    if (test_failed == 0) { \
        printf("All tests PASSED! ✅\n"); \
        return 0; \
    } else { \
        printf("Some tests FAILED! ❌\n"); \
        return 1; \
    }

// Mock data for testing
extern const char* mock_top_output;
extern const char* mock_system_profiler_output;
extern const char* mock_powermetrics_output;

// Test function declarations
int test_memory_pool();
int test_string_cache();
int test_security_validation();
int test_process_parsing();
int test_gpu_detection();
int test_network_parsing();
int test_system_info_parsing();
int test_memory_formatting();
int test_runtime_parsing();
int test_process_type_detection();
int test_resource_limits();
int test_error_handling();

// Enhanced regression detection tests
int test_edge_cases();
int test_malformed_input();
int test_boundary_conditions();
int test_concurrent_safety();
int test_memory_corruption_detection();
int test_performance_regression();
int test_ui_stability();
int test_system_compatibility();

// Utility functions (declare after including TaskMini.c in test files)
size_t get_memory_usage();
double get_current_time();
void simulate_system_load();
int check_memory_bounds(void* ptr, size_t size);

#endif // TASKMINI_TESTS_H
