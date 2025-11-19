# TaskMini Makefile
# Lightweight process monitor for macOS

CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0` -lpthread
TARGET = TaskMini
SOURCE = TaskMini.c
PREFIX = /usr/local

# Default target
all: $(TARGET)

# Build the main executable
$(TARGET): $(SOURCE)
	@echo "Building TaskMini..."
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)
	@echo "Build complete! Run with: ./$(TARGET)"

# Install to system (optional)
install: $(TARGET)
	@echo "Installing TaskMini to $(PREFIX)/bin..."
	cp $(TARGET) $(PREFIX)/bin/
	chmod +x $(PREFIX)/bin/$(TARGET)
	@echo "TaskMini installed! Run with: TaskMini"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TARGET)
	@echo "Clean complete!"

# Uninstall from system
uninstall:
	@echo "Removing TaskMini from $(PREFIX)/bin..."
	rm -f $(PREFIX)/bin/$(TARGET)
	@echo "TaskMini uninstalled!"

# Development build with debug symbols
debug: CFLAGS += -g -DDEBUG -Wall -Wextra
debug: $(TARGET)

# Release build with optimizations
release: CFLAGS += -O2 -DNDEBUG
release: $(TARGET)

# Check dependencies
deps:
	@echo "Checking dependencies..."
	@which pkg-config > /dev/null || (echo "ERROR: pkg-config not found. Install with: brew install pkg-config" && exit 1)
	@pkg-config --exists gtk+-3.0 || (echo "ERROR: GTK+3 not found. Install with: brew install gtk+3" && exit 1)
	@echo "All dependencies satisfied!"

# Test targets
test: unit-tests stress-tests integration-tests memory-tests performance-tests
	@echo "All tests completed successfully! ✅"

unit-tests: tests/test_runner
	@echo "Running unit tests..."
	@./tests/test_runner

stress-tests: tests/stress_tests  
	@echo "Running stress tests..."
	@./tests/stress_tests

integration-tests: tests/integration_tests
	@echo "Running integration tests..."
	@./tests/integration_tests

memory-tests: tests/memory_safety_tests
	@echo "Running memory safety tests..."
	@./tests/memory_safety_tests

performance-tests: tests/performance_regression_tests
	@echo "Running performance regression tests..."
	@./tests/performance_regression_tests

# Build test executables
tests/test_runner: tests/test_runner.c tests/taskmini_tests.h $(SOURCE)
	@mkdir -p tests/bin
	$(CC) $(CFLAGS) -DTESTING -o tests/test_runner tests/test_runner.c $(LIBS)

tests/stress_tests: tests/stress_tests.c tests/taskmini_tests.h $(SOURCE)
	@mkdir -p tests/bin  
	$(CC) $(CFLAGS) -DTESTING -o tests/stress_tests tests/stress_tests.c $(LIBS)

tests/integration_tests: tests/integration_tests.c tests/taskmini_tests.h $(SOURCE)
	@mkdir -p tests/bin
	$(CC) $(CFLAGS) -DTESTING -o tests/integration_tests tests/integration_tests.c $(LIBS)

tests/memory_safety_tests: tests/memory_safety_tests.c tests/taskmini_tests.h $(SOURCE)
	@mkdir -p tests/bin
	$(CC) $(CFLAGS) -DTESTING -o tests/memory_safety_tests tests/memory_safety_tests.c $(LIBS)

tests/performance_regression_tests: tests/performance_regression_tests.c tests/taskmini_tests.h $(SOURCE)
	@mkdir -p tests/bin
	$(CC) $(CFLAGS) -DTESTING -o tests/performance_regression_tests tests/performance_regression_tests.c $(LIBS)

# Clean test artifacts
clean-tests:
	@echo "Cleaning test artifacts..."
	rm -f tests/test_runner tests/stress_tests tests/integration_tests tests/memory_safety_tests tests/performance_regression_tests
	rm -rf tests/bin
	@echo "Test artifacts cleaned!"

# Regression test - runs all tests and checks for memory leaks
regression-test: test
	@echo "Running regression tests with memory checking..."
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Running with Valgrind..."; \
		valgrind --leak-check=full --error-exitcode=1 ./tests/test_runner; \
		valgrind --leak-check=full --error-exitcode=1 ./tests/stress_tests; \
		valgrind --leak-check=full --error-exitcode=1 ./tests/memory_safety_tests; \
	else \
		echo "Valgrind not available, running tests normally..."; \
	fi

# Code quality checks
lint:
	@echo "Running code quality checks..."
	@if command -v clang-tidy >/dev/null 2>&1; then \
		clang-tidy $(SOURCE) -- $(CFLAGS); \
	else \
		echo "clang-tidy not available, skipping lint check"; \
	fi

# Show help
help:
	@echo "TaskMini Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all            - Build TaskMini (default)"
	@echo "  install        - Install to $(PREFIX)/bin"
	@echo "  clean          - Remove build artifacts"  
	@echo "  uninstall      - Remove from system"
	@echo "  debug          - Build with debug symbols"
	@echo "  release        - Build optimized release"
	@echo "  deps           - Check dependencies"
	@echo "  test           - Run all tests"
	@echo "  unit-tests     - Run unit tests only"
	@echo "  stress-tests   - Run stress tests only"  
	@echo "  integration-tests - Run integration tests only"
	@echo "  memory-tests   - Run memory safety tests only"
	@echo "  performance-tests - Run performance regression tests only"
	@echo "  regression-test   - Run full regression suite"
	@echo "  clean-tests    - Clean test artifacts"
	@echo "  lint           - Run code quality checks"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  make           # Build TaskMini"
	@echo "  make test      # Run all tests"
	@echo "  make install   # Build and install"
	@echo "  make clean     # Clean up"

.PHONY: all install clean uninstall debug release deps help test unit-tests stress-tests integration-tests clean-tests regression-test lint
