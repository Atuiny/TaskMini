# TaskMini Test Suite

This directory contains comprehensive tests for the TaskMini process monitor application.

## Test Categories

### 1. Unit Tests (`test_runner.c`)
- **Memory Pool Operations** - Tests the process memory pool allocation and deallocation
- **String Buffer Cache** - Tests the string caching system for performance
- **Command Security Validation** - Tests command injection prevention
- **Process Data Parsing** - Tests parsing of memory strings, runtime formats
- **GPU Usage Detection** - Tests GPU monitoring with fallback mechanisms
- **Network Data Parsing** - Tests network statistics parsing
- **System Information Parsing** - Tests system profiler output parsing
- **Memory Format Functions** - Tests memory formatting utilities
- **Process Type Detection** - Tests system vs user process classification
- **Resource Limits and Safety** - Tests security limits and safe string operations
- **Error Handling** - Tests graceful error handling throughout the system

### 2. Stress Tests (`stress_tests.c`)
- **Memory Pool Stress Test** - Allocates/deallocates many processes rapidly
- **String Cache Stress Test** - Tests cache under heavy load
- **Security Validation Stress Test** - Tests command validation performance
- **Concurrent Access Safety Test** - Tests thread safety (simulated)
- **Memory Leak Detection** - Monitors memory usage during stress testing
- **Performance Regression Test** - Ensures performance doesn't degrade

### 3. Integration Tests (`integration_tests.c`)
- **Full Update Cycle Integration Test** - Tests complete data collection workflow
- **UI Update Integration Test** - Tests UI data population and updates
- **Background Thread Safety Test** - Tests thread safety in data updates
- **System Command Integration Test** - Tests real system command execution
- **Error Recovery Integration Test** - Tests recovery from system failures

### 4. Memory Safety Tests (`memory_safety_tests.c`)
- **Buffer Overflow Protection** - Tests against buffer overflow attacks
- **Use-After-Free Protection** - Tests memory reuse safety
- **Double-Free Protection** - Tests against double-free errors
- **Null Pointer Protection** - Tests null pointer handling
- **Memory Alignment** - Tests proper memory alignment and corruption detection

### 5. Performance Regression Tests (`performance_regression_tests.c`)
- **Memory Pool Performance Benchmark** - Measures allocation performance
- **String Cache Performance Benchmark** - Measures cache efficiency
- **Process Parsing Performance Benchmark** - Measures parsing speed
- **Process Type Detection Performance** - Measures classification speed
- **Memory Usage Stability Test** - Detects memory leaks over time
- **Concurrent Access Performance Test** - Measures concurrent operation efficiency

## Running Tests

### Quick Test (Unit Tests Only)
```bash
./tests/quick_test.sh --quick
```

### Individual Test Suites
```bash
make unit-tests           # Basic functionality tests
make stress-tests         # Performance and stress tests  
make integration-tests    # End-to-end workflow tests
make memory-tests         # Memory safety tests
make performance-tests    # Performance regression tests
```

### All Tests
```bash
make test                 # Run all test suites
```

### With Analysis
```bash
./tests/run_analysis.sh   # Comprehensive test analysis with reporting
```

### With Memory Checking (if Valgrind available)
```bash
make regression-test      # Run tests with memory leak detection
```

## Test Configuration

Test parameters are defined in `test_config.conf`:
- Performance baselines and thresholds
- Memory leak detection limits
- Stress test iteration counts
- Timeout values
- Expected error conditions

## Test Results

### Expected Performance Baselines
- Memory Pool: >2,500 ops/sec
- String Cache: >5,000 ops/sec  
- Process Parsing: >50 ops/sec
- Process Type Detection: >1,250 ops/sec
- Memory Leak Threshold: <1MB over test duration

### Success Criteria
- All unit tests pass
- No memory leaks detected
- Performance within acceptable ranges
- No crashes or hangs
- Graceful error handling

## Debugging Tests

### Enable Debug Output
Tests automatically include debug output when built with `-DTESTING` flag (done by Makefile).

### Common Issues
1. **Test Timeouts**: Reduce iteration counts in test files
2. **Memory Leaks**: Check process pool cleanup
3. **Performance Issues**: Verify system not under heavy load
4. **Command Failures**: Ensure required system commands are available

### Test-Specific Debug
- Runtime parsing shows detailed parsing steps
- Process type detection shows classification logic
- Memory operations show allocation/deallocation patterns

## Adding New Tests

### Test Structure
```c
int test_new_feature() {
    TEST_CASE("New Feature Test Description");
    
    // Setup
    init_process_pool();
    
    // Test operations
    ASSERT_TRUE(condition, "Error message");
    ASSERT_EQUAL(expected, actual, "Error message");
    ASSERT_NOT_NULL(pointer, "Error message");
    
    // Cleanup
    cleanup_process_pool();
    
    TEST_PASS();
}
```

### Test Macros Available
- `TEST_CASE(name)` - Start a test case
- `ASSERT_TRUE(condition, msg)` - Assert condition is true
- `ASSERT_FALSE(condition, msg)` - Assert condition is false
- `ASSERT_EQUAL(expected, actual, msg)` - Assert values are equal
- `ASSERT_STR_EQUAL(expected, actual, msg)` - Assert strings are equal
- `ASSERT_NOT_NULL(ptr, msg)` - Assert pointer is not null
- `ASSERT_NULL(ptr, msg)` - Assert pointer is null
- `ASSERT_RANGE(value, min, max, msg)` - Assert value is in range
- `ASSERT_STR_CONTAINS(haystack, needle, msg)` - Assert string contains substring
- `ASSERT_MEMORY_LEAK_FREE(start, end, threshold, msg)` - Assert no memory leaks
- `ASSERT_PERFORMANCE(ops_per_sec, min_threshold, msg)` - Assert performance level
- `TEST_PASS()` - Mark test as passed

### Integration with Build System
1. Add new test file to `tests/` directory
2. Update Makefile with new test target
3. Include in appropriate test suite or create new suite
4. Update this README with test description

## Continuous Integration

The test suite is designed to be CI-friendly:
- Configurable timeouts prevent hanging
- Clear exit codes (0 = success, non-zero = failure)
- Structured output for parsing
- Performance baselines for regression detection
- Memory leak detection integration

## Test Coverage

The test suite aims for comprehensive coverage:
- **Code Coverage**: Tests exercise all major code paths
- **Error Conditions**: Tests handle failure scenarios
- **Edge Cases**: Tests boundary conditions and invalid inputs
- **Performance**: Tests ensure performance doesn't regress
- **Security**: Tests validate security measures
- **Compatibility**: Tests work across different Mac hardware/OS versions

For detailed test results and analysis, run `./tests/run_analysis.sh` which generates comprehensive reports in `tests/regression_report.txt`.
