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
} UpdateData;

// Scroll position is now preserved using GTK model detachment technique

#endif // TYPES_H
