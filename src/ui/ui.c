#include "ui.h"
#include "../system/system.h"
#include "../utils/utils.h"
#include "../common/config.h"
#include <time.h>

// Global UI variables
GtkApplication *app = NULL;
GtkListStore *liststore = NULL;
GtkLabel *specs_label = NULL;
GtkLabel *summary_label = NULL;
char *static_specs = NULL;
gboolean updating = FALSE;
GMutex hash_mutex;

// Global widgets for scroll position preservation
GtkTreeView *global_treeview = NULL;
GtkScrolledWindow *global_scrolled_window = NULL;
GtkAdjustment *vertical_adjustment = NULL;

// Hash tables for network tracking
GHashTable *prev_net_bytes = NULL;
GHashTable *prev_times = NULL;

// Process cache for incremental updates (key: PID, value: ProcessCacheEntry*)
GHashTable *process_cache = NULL;
GMutex cache_mutex;

// Security tracking
time_t last_update_time = 0;
int consecutive_failures = 0;

// Helper function to compare two processes for changes
gboolean process_data_changed(Process *old_proc, Process *new_proc) {
    if (!old_proc || !new_proc) return TRUE;
    
    return strcmp(old_proc->name, new_proc->name) != 0 ||
           strcmp(old_proc->cpu, new_proc->cpu) != 0 ||
           strcmp(old_proc->mem, new_proc->mem) != 0 ||
           strcmp(old_proc->gpu, new_proc->gpu) != 0 ||
           strcmp(old_proc->net, new_proc->net) != 0 ||
           strcmp(old_proc->runtime, new_proc->runtime) != 0 ||
           strcmp(old_proc->type, new_proc->type) != 0;
}

// Helper function to update a single row in the TreeView using row reference
void update_tree_row_by_ref(GtkTreeRowReference *row_ref, Process *proc) {
    if (!row_ref || !gtk_tree_row_reference_valid(row_ref)) return;
    
    GtkTreePath *path = gtk_tree_row_reference_get_path(row_ref);
    if (!path) return;
    
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(liststore), &iter, path)) {
        gtk_list_store_set(liststore, &iter,
                           COL_PID, proc->pid,
                           COL_NAME, proc->name,
                           COL_CPU, proc->cpu,
                           COL_GPU, proc->gpu,
                           COL_MEM, proc->mem,
                           COL_NET, proc->net,
                           COL_RUNTIME, proc->runtime,
                           COL_TYPE, proc->type,
                           -1);
    }
    
    gtk_tree_path_free(path);
}

// Helper function to free ProcessCacheEntry
void free_cache_entry(ProcessCacheEntry *entry) {
    if (entry) {
        if (entry->process) {
            free_process(entry->process);
        }
        if (entry->row_ref) {
            gtk_tree_row_reference_free(entry->row_ref);
        }
        g_free(entry);
    }
}

// Forward declaration
void cleanup_stale_cache_entries(void);

// Incremental UI update function that preserves scroll position by only updating changed rows
gboolean update_ui_func(gpointer user_data) {
    UpdateData *data = (UpdateData *)user_data;
    
    // Periodic cache maintenance
    cleanup_stale_cache_entries();
    
    g_mutex_lock(&cache_mutex);
    
    // Create a set of PIDs from new data to track which processes are still alive
    GHashTable *new_pids = g_hash_table_new(g_str_hash, g_str_equal);
    
    // Phase 1: Update existing processes and add new ones
    GList *l;
    for (l = data->processes; l != NULL; l = l->next) {
        Process *new_proc = (Process *)l->data;
        g_hash_table_add(new_pids, new_proc->pid);
        
        ProcessCacheEntry *cache_entry = g_hash_table_lookup(process_cache, new_proc->pid);
        
        if (cache_entry && cache_entry->valid && 
            cache_entry->row_ref && gtk_tree_row_reference_valid(cache_entry->row_ref)) {
            // Process exists - check if data changed
            if (process_data_changed(cache_entry->process, new_proc)) {
                // Update the existing row
                update_tree_row_by_ref(cache_entry->row_ref, new_proc);
                
                // Update cached process data
                free_process(cache_entry->process);
                cache_entry->process = malloc(sizeof(Process));
                memcpy(cache_entry->process, new_proc, sizeof(Process));
            }
        } else {
            // New process - add to TreeView and cache
            GtkTreeIter iter;
            gtk_list_store_append(liststore, &iter);
            gtk_list_store_set(liststore, &iter,
                               COL_PID, new_proc->pid,
                               COL_NAME, new_proc->name,
                               COL_CPU, new_proc->cpu,
                               COL_GPU, new_proc->gpu,
                               COL_MEM, new_proc->mem,
                               COL_NET, new_proc->net,
                               COL_RUNTIME, new_proc->runtime,
                               COL_TYPE, new_proc->type,
                               -1);
            
            // Create row reference for stable access
            GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(liststore), &iter);
            GtkTreeRowReference *row_ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(liststore), path);
            gtk_tree_path_free(path);
            
            // Add to cache
            ProcessCacheEntry *new_entry = g_malloc(sizeof(ProcessCacheEntry));
            new_entry->process = malloc(sizeof(Process));
            memcpy(new_entry->process, new_proc, sizeof(Process));
            new_entry->row_ref = row_ref;
            new_entry->valid = TRUE;
            
            g_hash_table_insert(process_cache, g_strdup(new_proc->pid), new_entry);
        }
        
        free_process(new_proc);
    }
    g_list_free(data->processes);
    
    // Phase 2: Remove processes that are no longer running
    GHashTableIter iter;
    gpointer key, value;
    GList *to_remove = NULL;
    
    g_hash_table_iter_init(&iter, process_cache);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        char *pid = (char *)key;
        ProcessCacheEntry *entry = (ProcessCacheEntry *)value;
        
        if (!g_hash_table_contains(new_pids, pid)) {
            // Process no longer exists - mark for removal
            to_remove = g_list_prepend(to_remove, g_strdup(pid));
            
            // Remove from TreeView using row reference
            if (entry->valid && entry->row_ref && gtk_tree_row_reference_valid(entry->row_ref)) {
                GtkTreePath *path = gtk_tree_row_reference_get_path(entry->row_ref);
                if (path) {
                    GtkTreeIter iter;
                    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(liststore), &iter, path)) {
                        gtk_list_store_remove(liststore, &iter);
                    }
                    gtk_tree_path_free(path);
                }
            }
        }
    }
    
    // Remove from cache
    for (GList *rem = to_remove; rem != NULL; rem = rem->next) {
        g_hash_table_remove(process_cache, (char *)rem->data);
        g_free(rem->data);
    }
    g_list_free(to_remove);
    
    g_hash_table_destroy(new_pids);
    g_mutex_unlock(&cache_mutex);
    
    // Update specs label with dynamic GPU usage in friendly format
    char full_specs[1024];
    
    // Parse GPU percentage for friendly display
    float gpu_percent = 0.0;
    sscanf(data->gpu_usage, "%f%%", &gpu_percent);
    
    char gpu_status[50];
    if (gpu_percent < 5.0) {
        strcpy(gpu_status, "Graphics: Idle");
    } else if (gpu_percent < 25.0) {
        strcpy(gpu_status, "Graphics: Light use");
    } else if (gpu_percent < 50.0) {
        strcpy(gpu_status, "Graphics: Moderate use"); 
    } else if (gpu_percent < 75.0) {
        strcpy(gpu_status, "Graphics: Heavy use");
    } else {
        strcpy(gpu_status, "Graphics: Maximum use");
    }
    
    sprintf(full_specs, "%s\n%s (%.0f%%)", static_specs, gpu_status, gpu_percent);
    gtk_label_set_text(specs_label, full_specs);
    
    // Update system summary label
    gtk_label_set_text(summary_label, data->system_summary);
    
    free(data->gpu_usage);
    free(data->system_summary);
    free(data);

    updating = FALSE;

    return G_SOURCE_REMOVE;
}

// SECURITY: Enhanced timeout callback with deadlock detection and resource monitoring
gboolean timeout_callback(gpointer data) {
    time_t now = time(NULL);
    
    // SECURITY: Detect hung update threads
    if (update_thread_running && (now - update_start_time) > (MAX_UPDATE_TIME_MS / 1000)) {
        // Thread appears hung, reset state
        updating = FALSE;
        update_thread_running = FALSE;
        consecutive_failures++;
    }
    
    if (updating) {
        return TRUE;  // Skip if still updating
    }
    
    // SECURITY: Throttle updates if too many failures
    if (consecutive_failures >= MAX_FAILURES) {
        static time_t last_throttle_log = 0;
        if ((now - last_throttle_log) > 10) {  // Log once every 10 seconds
            g_warning("TaskMini: Throttling updates due to consecutive failures");
            last_throttle_log = now;
        }
        return TRUE;  // Skip this update cycle
    }
    
    updating = TRUE;
    g_thread_new("update_thread", update_thread_func, NULL);
    return TRUE;
}

// Using model detachment technique for perfect scroll preservation

// Activate callback: Set up vertical box for specs label + scrolled window. 
// Init hashes and static specs. Added sorting setup: cast liststore to sortable, 
// set custom sort func for each column, set sort column id on each tree view column 
// to make them clickable for asc/desc sorting. Initial sort on CPU descending. 
// Added g_mutex_init for hash_mutex. Initial update now launches thread instead of direct call.
void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "TaskMini");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);

    // Vertical box for layout
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), box);

    // System specs label
    specs_label = GTK_LABEL(gtk_label_new("Loading specs..."));
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(specs_label), FALSE, FALSE, 0);
    
    // System summary label (Networks, VM, Disks info)
    summary_label = GTK_LABEL(gtk_label_new("Loading system info..."));
    gtk_label_set_justify(summary_label, GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(summary_label), FALSE, FALSE, 0);

    // Scrolled window
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    global_scrolled_window = GTK_SCROLLED_WINDOW(scrolled_window); // Set global reference for scroll preservation
    
    // Get the vertical adjustment for scroll position tracking
    vertical_adjustment = gtk_scrolled_window_get_vadjustment(global_scrolled_window);
    
    gtk_box_pack_start(GTK_BOX(box), scrolled_window, TRUE, TRUE, 0);

    // List store - added G_TYPE_STRING for the new TYPE column
    liststore = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    // Make sortable and set sort functions
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(liststore);
    for (int col = 0; col < NUM_COLS; col++) {
        gtk_tree_sortable_set_sort_func(sortable, col, process_compare_func, GINT_TO_POINTER(col), NULL);
    }

    // Set initial sort: CPU descending (will be applied after data is loaded)
    gtk_tree_sortable_set_sort_column_id(sortable, COL_CPU, GTK_SORT_DESCENDING);

    // Tree view
    GtkWidget *treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(liststore));
    global_treeview = GTK_TREE_VIEW(treeview); // Set global reference for scroll preservation
    gtk_container_add(GTK_CONTAINER(scrolled_window), treeview);
    
    // Connect right-click handler for context menu
    g_signal_connect(treeview, "button-press-event", G_CALLBACK(on_treeview_button_press), NULL);

    // Columns with updated titles, make sortable by setting sort column id
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column;

    column = gtk_tree_view_column_new_with_attributes("PID", renderer, "text", COL_PID, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_PID);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", COL_NAME, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("CPU (% of system)", renderer, "text", COL_CPU, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_CPU);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("GPU (%)", renderer, "text", COL_GPU, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_GPU);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Memory", renderer, "text", COL_MEM, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_MEM);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Network", renderer, "text", COL_NET, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_NET);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Run Time", renderer, "text", COL_RUNTIME, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_RUNTIME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    column = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", COL_TYPE, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_TYPE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    // Init hashes
    prev_net_bytes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    prev_times = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_mutex_init(&hash_mutex);
    
    // Init process cache for incremental updates
    process_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)free_cache_entry);
    g_mutex_init(&cache_mutex);

    // Get static specs
    static_specs = get_static_specs();

    // Initial update via thread
    updating = TRUE;
    g_thread_new("update_thread", update_thread_func, NULL);

    // Timer - refresh every 0.5 seconds for more responsive updates
    g_timeout_add(UI_UPDATE_INTERVAL_MS, timeout_callback, NULL);

    gtk_widget_show_all(window);
}

// Cache maintenance function to clean up stale entries
void cleanup_stale_cache_entries(void) {
    static time_t last_cleanup = 0;
    time_t now = time(NULL);
    
    // Only cleanup every 30 seconds to avoid overhead
    if (now - last_cleanup < 30) return;
    last_cleanup = now;
    
    g_mutex_lock(&cache_mutex);
    
    GHashTableIter iter;
    gpointer key, value;
    GList *to_remove = NULL;
    
    g_hash_table_iter_init(&iter, process_cache);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ProcessCacheEntry *entry = (ProcessCacheEntry *)value;
        
        // Check if row reference is still valid
        if (!entry->row_ref || !gtk_tree_row_reference_valid(entry->row_ref)) {
            to_remove = g_list_prepend(to_remove, g_strdup((char *)key));
        }
    }
    
    // Remove stale entries
    for (GList *rem = to_remove; rem != NULL; rem = rem->next) {
        g_hash_table_remove(process_cache, (char *)rem->data);
        g_free(rem->data);
    }
    g_list_free(to_remove);
    
    g_mutex_unlock(&cache_mutex);
}

// Cleanup function for UI resources
void cleanup_ui_resources(void) {
    if (process_cache) {
        g_hash_table_destroy(process_cache);
        process_cache = NULL;
    }
    
    if (prev_net_bytes) {
        g_hash_table_destroy(prev_net_bytes);
        prev_net_bytes = NULL;
    }
    
    if (prev_times) {
        g_hash_table_destroy(prev_times);
        prev_times = NULL;
    }
    
    g_mutex_clear(&cache_mutex);
    g_mutex_clear(&hash_mutex);
    
    if (static_specs) {
        free(static_specs);
        static_specs = NULL;
    }
}
