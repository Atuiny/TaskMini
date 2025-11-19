#!/bin/bash
# TaskMini Quick Build Script

echo "TaskMini - Quick Build Script"
echo "============================"

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "ERROR: This script is designed for macOS only."
    exit 1
fi

# Check dependencies
echo "Checking dependencies..."
if ! command -v pkg-config &> /dev/null; then
    echo "ERROR: pkg-config not found. Install with: brew install pkg-config"
    exit 1
fi

if ! pkg-config --exists gtk+-3.0; then
    echo "ERROR: GTK+3 not found. Install with: brew install gtk+3"
    exit 1
fi

echo "Dependencies OK!"

# Build TaskMini
echo "Building TaskMini..."
gcc `pkg-config --cflags gtk+-3.0` -o TaskMini TaskMini.c `pkg-config --libs gtk+-3.0` -lpthread

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Build successful!"
    echo "Run TaskMini with: ./TaskMini"
    
    # Make executable if it exists
    if [ -f "TaskMini" ]; then
        chmod +x TaskMini
        echo "TaskMini is ready to run!"
    fi
else
    echo ""
    echo "❌ Build failed!"
    echo "Check the error messages above for details."
    exit 1
fi
