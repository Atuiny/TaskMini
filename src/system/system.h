#ifndef SYSTEM_H
#define SYSTEM_H

#include <glib.h>
#include "../common/types.h"

// System information functions
char* get_static_specs(void);

// Process monitoring functions
char* get_top_output(void);
gpointer update_thread_func(gpointer data);
gboolean is_system_process(const char *name, const char *pid);
void determine_process_type(Process *proc);
char* get_run_time(const char *pid);

// GPU monitoring functions
char* get_gpu_usage(void);
char* get_gpu_usage_fallback(void);

// Network monitoring functions
long long get_net_bytes(const char *pid);
void collect_all_network_data(void);

// Global variables (declared in system/process.c)
extern int cpu_cores;
extern gboolean powermetrics_unavailable;
extern char *cached_gpu_result;
extern time_t last_gpu_check;
extern int gpu_check_interval;

// Network cache globals
extern time_t last_net_collection;
extern GHashTable *net_cache;

// System-wide network tracking
extern long long prev_system_bytes_in;
extern long long prev_system_bytes_out; 
extern double prev_system_time;

#endif // SYSTEM_H
