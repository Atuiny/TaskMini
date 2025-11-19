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

# Show help
help:
	@echo "TaskMini Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all      - Build TaskMini (default)"
	@echo "  install  - Install to $(PREFIX)/bin"
	@echo "  clean    - Remove build artifacts"  
	@echo "  uninstall- Remove from system"
	@echo "  debug    - Build with debug symbols"
	@echo "  release  - Build optimized release"
	@echo "  deps     - Check dependencies"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  make           # Build TaskMini"
	@echo "  make install   # Build and install"
	@echo "  make clean     # Clean up"

.PHONY: all install clean uninstall debug release deps help
