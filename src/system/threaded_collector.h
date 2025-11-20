#ifndef THREADED_COLLECTOR_H
#define THREADED_COLLECTOR_H

#include <glib.h>
#include <time.h>
#include "../common/types.h"

// Threading states
typedef enum {
    THREAD_STATE_IDLE,
    THREAD_STATE_RUNNING,
    THREAD_STATE_COMPLETED,
    THREAD_STATE_FAILED
} ThreadState;

// Data collection results for different components
typedef struct {
    GList *processes;           // Basic process list (PID, name, type)
    char *system_summary;       // System summary info (Networks, VM, Disks)
    ThreadState state;
    time_t timestamp;
    GMutex mutex;
} ProcessListResult;

typedef struct {
    float cpu_usage;            // System-wide CPU usage
    GHashTable *process_cpu;    // Per-process CPU usage (PID -> CPU%)
    ThreadState state;
    time_t timestamp;
    GMutex mutex;
} CPUDataResult;

typedef struct {
    float memory_usage;         // System-wide memory usage
    GHashTable *process_memory; // Per-process memory usage (PID -> bytes)
    ThreadState state;
    time_t timestamp;
    GMutex mutex;
} MemoryDataResult;

typedef struct {
    char gpu_status[50];        // GPU status string
    float gpu_percentage;       // GPU usage percentage
    ThreadState state;
    time_t timestamp;
    GMutex mutex;
} GPUDataResult;

typedef struct {
    GHashTable *process_network; // Per-process network rates (PID -> KB/s)
    GHashTable *prev_net_bytes;  // Previous network bytes per process
    GHashTable *prev_times;      // Previous collection times per process
    ThreadState state;
    time_t timestamp;
    GMutex mutex;
} NetworkDataResult;

// Main collector structure
typedef struct {
    ProcessListResult *process_list;
    CPUDataResult *cpu_data;
    MemoryDataResult *memory_data;
    GPUDataResult *gpu_data;
    NetworkDataResult *network_data;
    
    // Thread handles
    GThread *process_thread;
    GThread *cpu_thread;
    GThread *memory_thread;
    GThread *gpu_thread;
    GThread *network_thread;
    
    // Coordination
    gboolean shutdown_requested;
    GMutex coordinator_mutex;
    time_t collection_start_time;
    
    // Collector/Bin system - pre-built complete data ready for fast UI access
    UpdateData *data_bin;           // Latest complete dataset
    GMutex bin_mutex;              // Protects the data bin
    GThread *collector_thread;     // Single background collector thread
    gboolean continuous_mode;      // Whether collector runs continuously
    
} ThreadedCollector;

// Function declarations
ThreadedCollector* threaded_collector_create(void);
void threaded_collector_destroy(ThreadedCollector *collector);

// Original threading functions (kept for compatibility)
void threaded_collector_start_collection(ThreadedCollector *collector);
gboolean threaded_collector_has_basic_data(ThreadedCollector *collector);
gboolean threaded_collector_has_complete_data(ThreadedCollector *collector);
UpdateData* threaded_collector_get_available_data(ThreadedCollector *collector);

// New collector/bin functions (efficient approach)
void threaded_collector_start_continuous_collection(ThreadedCollector *collector);
UpdateData* threaded_collector_get_latest_complete_data(ThreadedCollector *collector);

// Individual collection threads
gpointer collect_process_list_thread(gpointer data);
gpointer collect_cpu_data_thread(gpointer data);
gpointer collect_memory_data_thread(gpointer data);
gpointer collect_gpu_data_thread(gpointer data);
gpointer collect_network_data_thread(gpointer data);

// Helper functions
void merge_process_data(GList *processes, CPUDataResult *cpu, MemoryDataResult *memory, 
                       GPUDataResult *gpu, NetworkDataResult *network);
void cleanup_process_list_result(ProcessListResult *result);
void cleanup_cpu_data_result(CPUDataResult *result);
void cleanup_memory_data_result(MemoryDataResult *result);
void cleanup_gpu_data_result(GPUDataResult *result);
void cleanup_network_data_result(NetworkDataResult *result);

#endif // THREADED_COLLECTOR_H
