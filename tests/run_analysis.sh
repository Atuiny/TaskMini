#!/bin/bash

# TaskMini Test Result Analysis Script
# This script analyzes test results to identify regressions and performance issues

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$TEST_DIR")"
LOG_FILE="$TEST_DIR/test_results.log"
BASELINE_FILE="$TEST_DIR/performance_baseline.txt"
REPORT_FILE="$TEST_DIR/regression_report.txt"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging function
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

# Create baseline if it doesn't exist
create_baseline() {
    if [ ! -f "$BASELINE_FILE" ]; then
        log "Creating performance baseline..."
        echo "# TaskMini Performance Baseline - $(date)" > "$BASELINE_FILE"
        echo "# Format: test_name:ops_per_sec:memory_usage:duration" >> "$BASELINE_FILE"
    fi
}

# Run all tests and capture results
run_comprehensive_tests() {
    log "Starting comprehensive test suite..."
    
    cd "$PROJECT_DIR"
    
    # Clean previous test artifacts
    make clean-tests >/dev/null 2>&1
    
    echo -e "${BLUE}Building test executables...${NC}"
    if ! make tests/test_runner tests/stress_tests tests/integration_tests tests/memory_safety_tests tests/performance_regression_tests >/dev/null 2>&1; then
        echo -e "${RED}ERROR: Failed to build test executables${NC}"
        return 1
    fi
    
    # Initialize report
    echo "TaskMini Regression Analysis Report" > "$REPORT_FILE"
    echo "Generated: $(date)" >> "$REPORT_FILE"
    echo "=====================================" >> "$REPORT_FILE"
    echo "" >> "$REPORT_FILE"
    
    # Run each test suite
    local test_suites=("unit-tests" "stress-tests" "integration-tests" "memory-tests" "performance-tests")
    local failed_tests=0
    local total_tests=0
    
    for suite in "${test_suites[@]}"; do
        echo -e "${BLUE}Running $suite...${NC}"
        ((total_tests++))
        
        local start_time=$(date +%s.%N)
        local test_output
        
        if test_output=$(make "$suite" 2>&1); then
            local end_time=$(date +%s.%N)
            local duration=$(echo "$end_time - $start_time" | bc)
            
            echo -e "${GREEN}✅ $suite PASSED (${duration}s)${NC}"
            log "$suite: PASSED in ${duration}s"
            
            # Extract performance metrics if available
            extract_performance_metrics "$suite" "$test_output" "$duration"
            
        else
            local end_time=$(date +%s.%N)
            local duration=$(echo "$end_time - $start_time" | bc)
            
            echo -e "${RED}❌ $suite FAILED (${duration}s)${NC}"
            log "$suite: FAILED in ${duration}s"
            ((failed_tests++))
            
            # Add failure details to report
            echo "FAILED TEST: $suite" >> "$REPORT_FILE"
            echo "Duration: ${duration}s" >> "$REPORT_FILE"
            echo "Output:" >> "$REPORT_FILE"
            echo "$test_output" | sed 's/^/  /' >> "$REPORT_FILE"
            echo "" >> "$REPORT_FILE"
        fi
    done
    
    # Generate summary
    echo "" >> "$REPORT_FILE"
    echo "TEST SUMMARY:" >> "$REPORT_FILE"
    echo "Total test suites: $total_tests" >> "$REPORT_FILE"
    echo "Passed: $((total_tests - failed_tests))" >> "$REPORT_FILE"
    echo "Failed: $failed_tests" >> "$REPORT_FILE"
    
    if [ $failed_tests -eq 0 ]; then
        echo -e "${GREEN}🎉 All tests passed!${NC}"
        return 0
    else
        echo -e "${RED}⚠️  $failed_tests test suite(s) failed${NC}"
        return 1
    fi
}

# Extract performance metrics from test output
extract_performance_metrics() {
    local suite="$1"
    local output="$2"
    local duration="$3"
    
    # Look for performance data in the output
    local metrics=$(echo "$output" | grep -E "([0-9]+\.[0-9]+) ops/sec|Memory change: [+-]?[0-9]+ bytes")
    
    if [ -n "$metrics" ]; then
        echo "Performance data for $suite:" >> "$REPORT_FILE"
        echo "$metrics" | sed 's/^/  /' >> "$REPORT_FILE"
        echo "" >> "$REPORT_FILE"
    fi
}

# Check for regressions by comparing with baseline
check_regressions() {
    if [ ! -f "$BASELINE_FILE" ]; then
        log "No baseline file found. Current run will be used as baseline."
        return 0
    fi
    
    echo -e "${BLUE}Checking for performance regressions...${NC}"
    
    # This is a simplified regression check
    # In a real implementation, you would parse the performance data
    # and compare against historical baselines
    
    local regressions_found=false
    
    # Check if any test output indicates performance issues
    if grep -q "performance regression" "$LOG_FILE"; then
        echo -e "${YELLOW}⚠️  Performance regressions detected${NC}"
        regressions_found=true
    fi
    
    if grep -q "Memory leak" "$LOG_FILE"; then
        echo -e "${YELLOW}⚠️  Memory leaks detected${NC}"
        regressions_found=true
    fi
    
    if [ "$regressions_found" = true ]; then
        echo "REGRESSIONS DETECTED:" >> "$REPORT_FILE"
        grep -i "regression\|leak\|failed" "$LOG_FILE" >> "$REPORT_FILE"
        return 1
    else
        echo -e "${GREEN}✅ No regressions detected${NC}"
        return 0
    fi
}

# Generate detailed report
generate_report() {
    echo -e "${BLUE}Generating detailed report...${NC}"
    
    # Add system information
    echo "" >> "$REPORT_FILE"
    echo "SYSTEM INFORMATION:" >> "$REPORT_FILE"
    echo "OS: $(uname -s) $(uname -r)" >> "$REPORT_FILE"
    echo "Architecture: $(uname -m)" >> "$REPORT_FILE"
    echo "Date: $(date)" >> "$REPORT_FILE"
    
    # Add git information if available
    if git rev-parse --git-dir > /dev/null 2>&1; then
        echo "Git commit: $(git rev-parse HEAD)" >> "$REPORT_FILE"
        echo "Git branch: $(git rev-parse --abbrev-ref HEAD)" >> "$REPORT_FILE"
    fi
    
    echo "" >> "$REPORT_FILE"
    echo "Log file: $LOG_FILE" >> "$REPORT_FILE"
    echo "Report generated by: $0" >> "$REPORT_FILE"
    
    echo -e "${GREEN}Report saved to: $REPORT_FILE${NC}"
}

# Main execution
main() {
    echo "TaskMini Test Analysis Tool"
    echo "=========================="
    echo ""
    
    # Ensure test directory exists
    mkdir -p "$TEST_DIR"
    
    # Initialize baseline
    create_baseline
    
    # Clear previous log
    > "$LOG_FILE"
    
    log "Starting test analysis..."
    
    # Run tests
    if run_comprehensive_tests; then
        local test_result=0
    else
        local test_result=1
    fi
    
    # Check for regressions
    if check_regressions; then
        local regression_result=0
    else
        local regression_result=1
    fi
    
    # Generate report
    generate_report
    
    # Final result
    if [ $test_result -eq 0 ] && [ $regression_result -eq 0 ]; then
        echo -e "${GREEN}🎉 All tests passed with no regressions!${NC}"
        exit 0
    else
        echo -e "${RED}❌ Tests failed or regressions detected${NC}"
        echo -e "Check the report: $REPORT_FILE"
        exit 1
    fi
}

# Handle command line arguments
case "${1:-}" in
    --help|-h)
        echo "Usage: $0 [OPTIONS]"
        echo ""
        echo "Options:"
        echo "  --help, -h     Show this help message"
        echo "  --baseline     Update performance baseline"
        echo "  --clean        Clean test artifacts and logs"
        echo ""
        echo "This script runs the complete TaskMini test suite and analyzes"
        echo "results for regressions and performance issues."
        exit 0
        ;;
    --baseline)
        rm -f "$BASELINE_FILE"
        echo "Performance baseline will be regenerated on next run."
        exit 0
        ;;
    --clean)
        rm -f "$LOG_FILE" "$REPORT_FILE" "$BASELINE_FILE"
        cd "$PROJECT_DIR" && make clean-tests
        echo "Test artifacts cleaned."
        exit 0
        ;;
    "")
        main
        ;;
    *)
        echo "Unknown option: $1"
        echo "Use --help for usage information."
        exit 1
        ;;
esac
