#!/bin/bash

# Quick test runner with timeouts to prevent hanging tests

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$TEST_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Function to run test with timeout (macOS compatible)
run_test_with_timeout() {
    local test_name="$1"
    local timeout="$2"
    
    echo -e "${BLUE}Running $test_name (max ${timeout}s)...${NC}"
    
    # Run the test in background and get its PID
    make "$test_name" &
    local test_pid=$!
    
    # Wait for completion or timeout
    local count=0
    while kill -0 "$test_pid" 2>/dev/null; do
        if [ $count -ge $timeout ]; then
            echo -e "${YELLOW}⏰ $test_name taking too long, killing...${NC}"
            kill -9 "$test_pid" 2>/dev/null
            wait "$test_pid" 2>/dev/null
            return 124
        fi
        sleep 1
        ((count++))
    done
    
    # Get the exit code
    wait "$test_pid"
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}✅ $test_name completed successfully${NC}"
        return 0
    else
        echo -e "${RED}❌ $test_name failed with exit code $exit_code${NC}"
        return $exit_code
    fi
}

main() {
    echo "TaskMini Quick Test Runner"
    echo "========================="
    echo ""
    
    cd "$PROJECT_DIR"
    
    # Clean and build test executables
    echo -e "${BLUE}Building test executables...${NC}"
    make clean-tests >/dev/null 2>&1
    
    local failed_tests=0
    local total_tests=0
    
    # Run tests with appropriate timeouts
    ((total_tests++))
    if ! run_test_with_timeout "unit-tests" 60; then
        ((failed_tests++))
    fi
    
    ((total_tests++))
    if ! run_test_with_timeout "stress-tests" 120; then
        ((failed_tests++))
    fi
    
    ((total_tests++)) 
    if ! run_test_with_timeout "integration-tests" 60; then
        ((failed_tests++))
    fi
    
    ((total_tests++))
    if ! run_test_with_timeout "memory-tests" 60; then
        ((failed_tests++))
    fi
    
    ((total_tests++))
    if ! run_test_with_timeout "performance-tests" 90; then
        ((failed_tests++))
    fi
    
    # Summary
    echo ""
    echo "============================================"
    echo "Test Summary:"
    echo "  Total: $total_tests"
    echo "  Passed: $((total_tests - failed_tests))"
    echo "  Failed: $failed_tests"
    
    if [ $failed_tests -eq 0 ]; then
        echo -e "${GREEN}🎉 All tests completed successfully!${NC}"
        exit 0
    else
        echo -e "${RED}⚠️  Some tests failed or timed out${NC}"
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
        echo "  --quick        Run only unit tests (fastest)"
        echo ""
        echo "This script runs TaskMini tests with timeouts to prevent hanging."
        exit 0
        ;;
    --quick)
        cd "$PROJECT_DIR"
        run_test_with_timeout "unit-tests" 30
        exit $?
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
