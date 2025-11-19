#ifndef TYPES_H
#define TYPES_H

#include <gtk/gtk.h>
#include <glib.h>

// Column enumeration for TreeView
enum {
    COL_PID,
    COL_NAME,
    COL_CPU,
    COL_GPU,
    COL_MEM,
    COL_NET,
    COL_RUNTIME,
    COL_TYPE,
    NUM_COLS
};

// Process data structure
typedef struct {
    char pid[10];           // Max PID on macOS + safety margin 
    char name[50];          // Process name/command
    char cpu[10];           // CPU usage percentage
    char mem[20];           // Memory usage (human readable)
    char gpu[20];           // GPU usage
    char net[20];           // Network usage rate
    char runtime[20];       // Process runtime
    char type[20];          // Process type (System/User)
    gboolean is_system;     // TRUE if system process
} Process;

// Data structure for passing between threads
typedef struct {
    GList *processes;       // List of Process structs
    char *gpu_usage;        // GPU usage string
    char *system_summary;   // System summary info
    float system_cpu_usage; // System-wide CPU usage percentage
    float system_memory_usage; // System-wide memory usage percentage
} UpdateData;

// Process cache entry for incremental updates
typedef struct {
    Process *process;           // Process data
    GtkTreeRowReference *row_ref;  // Stable reference to TreeView row
    gboolean valid;             // Whether the row reference is valid
} ProcessCacheEntry;

// Filter criteria structure
typedef struct {
    char pid_filter[20];        // PID filter (e.g., "100+", "50-", "123")
    char name_filter[100];      // Name filter (substring search)
    char cpu_filter[20];        // CPU filter (e.g., "15%+", "5%-")
    char gpu_filter[20];        // GPU filter (e.g., "10%+", "0%-")
    char memory_filter[20];     // Memory filter (e.g., "100MB+", "1GB-")
    char network_filter[20];    // Network filter (e.g., "1KB/s+")
    char type_filter[20];       // Type filter ("System", "User", "All")
    gboolean active;            // Whether filtering is enabled
} FilterCriteria;

// Scroll position is now preserved using incremental updates

#endif // TYPES_H
