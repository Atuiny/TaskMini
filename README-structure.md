# TaskMini - Modular Structure

This document describes the refactored, modular structure of TaskMini following C best practices.

## 🏗️ Project Structure

```
TaskMini-Project/
├── src/                    # Source code
│   ├── main.c             # Application entry point
│   ├── common/            # Shared definitions
│   │   ├── types.h        # Data structures and enums  
│   │   └── config.h       # Configuration constants
│   ├── ui/                # User interface components
│   │   ├── ui.h           # UI function declarations
│   │   ├── ui.c           # Main UI setup and callbacks
│   │   ├── context_menu.c # Right-click context menus
│   │   └── sorting.c      # TreeView column sorting
│   ├── system/            # System monitoring
│   │   ├── system.h       # System function declarations
│   │   ├── system_info.c  # Hardware specs and static info
│   │   ├── process.c      # Process monitoring and threading
│   │   ├── gpu.c          # GPU usage detection
│   │   └── network.c      # Network monitoring
│   └── utils/             # Utility functions
│       ├── utils.h        # Utility function declarations
│       ├── memory.c       # Memory pools and caching
│       ├── security.c     # Input validation and safe functions
│       └── parsing.c      # String parsing and formatting
├── tests/                 # Test suite (existing)
├── Makefile-new          # New modular build system
└── README-structure.md   # This file
```

## 📋 Module Responsibilities

### 🎯 Main (`src/main.c`)
- Application entry point
- Cleanup registration
- Memory pool initialization

### 🏠 Common (`src/common/`)
- **types.h**: Shared data structures (Process, UpdateData, etc.)
- **config.h**: Compile-time constants and limits

### 🖥️ UI (`src/ui/`)
- **ui.c**: Main window setup, GTK initialization, update callbacks
- **context_menu.c**: Process termination via right-click menus
- **sorting.c**: Column sorting logic for different data types

### 🔧 System (`src/system/`)
- **system_info.c**: Hardware detection (CPU, RAM, storage, etc.)
- **process.c**: Background thread for process monitoring
- **gpu.c**: GPU usage detection with fallback methods
- **network.c**: Network activity monitoring with caching

### 🛠️ Utils (`src/utils/`)
- **memory.c**: Memory pools for Process structs, string caching
- **security.c**: Input validation, safe string operations
- **parsing.c**: String parsing (bytes, runtime, memory formatting)

## 🔨 Building the Modular Version

### Quick Build
```bash
# Replace old Makefile and build
mv Makefile Makefile-old
mv Makefile-new Makefile
make
```

### Development Commands
```bash
make debug      # Build with debug symbols
make release    # Optimized production build  
make clean      # Remove build artifacts
make run        # Build and run
make info       # Show build information
make help       # Show available targets
```

## 🚀 Benefits of Modular Structure

### ✅ **Maintainability**
- Clear separation of concerns
- Each module has a single responsibility
- Easy to locate and modify specific functionality

### ✅ **Readability**
- No more 1900+ line files
- Logical grouping of related functions
- Clear dependencies between modules

### ✅ **Testability** 
- Individual modules can be unit tested
- Mock interfaces for isolated testing
- Existing test suite continues to work

### ✅ **Scalability**
- Easy to add new monitoring features
- Simple to extend UI components
- Clear patterns for new functionality

### ✅ **Collaboration**
- Multiple developers can work on different modules
- Reduced merge conflicts
- Clear ownership of components

## 📊 Module Dependencies

```
main.c
├── ui/ (GTK interface)
│   ├── system/ (data source)
│   └── utils/ (helper functions)
├── system/ (monitoring)
│   └── utils/ (parsing, security)
└── utils/ (foundation)
    └── common/ (types, config)
```

## 🔄 Migration from Single File

The refactoring preserves all existing functionality while improving structure:

- **Zero functionality loss** - All features maintained
- **Same performance** - Optimizations preserved  
- **Identical UI** - No user-facing changes
- **Backward compatibility** - Same command-line interface

## 🎯 Future Enhancements

With the modular structure, these improvements become easier:

1. **Plugin System**: Add new monitoring modules
2. **Configuration Files**: User-customizable settings
3. **Multiple Backends**: Support different monitoring APIs
4. **Extended UI**: Additional views and panels
5. **Cross-platform**: Easier OS-specific implementations

## 🛡️ Security and Robustness

All existing security features are preserved:
- Input validation in `utils/security.c`
- Memory safety with bounds checking
- Resource limits and timeout protection
- Safe string operations throughout

The modular structure actually improves security by:
- Isolating validation logic
- Making security reviews easier
- Reducing complexity in critical paths
