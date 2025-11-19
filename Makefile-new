# TaskMini - Modular Makefile
# Builds a GTK+ task manager with proper separation of concerns

CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Wall -Wextra -std=c99 -O2
LIBS = `pkg-config --libs gtk+-3.0` -lpthread

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = .

# Target executable
TARGET = TaskMini

# Source files organized by module
MAIN_SRC = $(SRCDIR)/main.c

UI_SRC = $(SRCDIR)/ui/ui.c \
         $(SRCDIR)/ui/context_menu.c \
         $(SRCDIR)/ui/sorting.c

SYSTEM_SRC = $(SRCDIR)/system/system_info.c \
             $(SRCDIR)/system/process.c \
             $(SRCDIR)/system/gpu.c \
             $(SRCDIR)/system/network.c

UTILS_SRC = $(SRCDIR)/utils/memory.c \
            $(SRCDIR)/utils/security.c \
            $(SRCDIR)/utils/parsing.c

# All source files
SOURCES = $(MAIN_SRC) $(UI_SRC) $(SYSTEM_SRC) $(UTILS_SRC)

# Object files (replace .c with .o and place in obj directory)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Default target
all: $(TARGET)

# Create directories if they don't exist
$(OBJDIR):
	@mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/ui
	@mkdir -p $(OBJDIR)/system
	@mkdir -p $(OBJDIR)/utils

# Link the target executable
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking TaskMini..."
	@$(CC) $(OBJECTS) -o $(BINDIR)/$(TARGET) $(LIBS)
	@echo "✅ Build complete! Run with: ./$(TARGET)"

# Compile source files to object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@echo "🔨 Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	@rm -rf $(OBJDIR)
	@rm -f $(BINDIR)/$(TARGET)
	@echo "✅ Clean complete!"

# Install dependencies (for development)
deps:
	@echo "📦 Installing GTK+ development dependencies..."
	@echo "Please install GTK+ 3 development packages for your system:"
	@echo "  macOS: brew install gtk+3"
	@echo "  Ubuntu/Debian: sudo apt install libgtk-3-dev"
	@echo "  CentOS/RHEL: sudo yum install gtk3-devel"

# Development build with debug symbols
debug: CFLAGS += -g -DDEBUG -O0
debug: clean $(TARGET)

# Production build with optimizations
release: CFLAGS += -DNDEBUG -O3 -march=native
release: clean $(TARGET)

# Run the application
run: $(TARGET)
	@echo "🚀 Starting TaskMini..."
	@./$(TARGET)

# Show build information
info:
	@echo "📋 TaskMini Build Information"
	@echo "=============================="
	@echo "Target: $(TARGET)"
	@echo "Source files: $(words $(SOURCES))"
	@echo "  - Main: 1 file"
	@echo "  - UI: $(words $(UI_SRC)) files"
	@echo "  - System: $(words $(SYSTEM_SRC)) files"
	@echo "  - Utils: $(words $(UTILS_SRC)) files"
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"

# Help target
help:
	@echo "📚 TaskMini Build System"
	@echo "========================"
	@echo "Available targets:"
	@echo "  all       - Build TaskMini (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build optimized version"
	@echo "  run       - Build and run TaskMini"
	@echo "  deps      - Show dependency information"
	@echo "  info      - Show build information"
	@echo "  help      - Show this help"

# Declare phony targets
.PHONY: all clean deps debug release run info help

# Include dependency files if they exist
-include $(OBJECTS:.o=.d)
