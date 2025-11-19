#include "ui.h"
#include "../system/system.h"
#include "../utils/utils.h"
#include "../common/config.h"
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>

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

// Filter system
FilterCriteria current_filter = {0};
GtkWidget *filter_entries[7] = {0}; // PID, Name, CPU, GPU, Memory, Network, Type

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

// Forward declarations
void cleanup_stale_cache_entries(void);
gboolean process_matches_filter(Process *proc);
void on_filter_changed(GtkWidget *widget, gpointer user_data);
gboolean parse_memory_filter(const char *filter, long long *bytes, char *op);
gboolean parse_network_filter(const char *filter, long long *bps, char *op);
long long network_to_bps(const char *net_str);
gboolean validate_filter_input(const char *text, int filter_type);
void apply_filters_to_display(void);

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
        
        // Check if process matches current filter
        gboolean matches_filter = process_matches_filter(new_proc);
        
        if (cache_entry && cache_entry->valid && 
            cache_entry->row_ref && gtk_tree_row_reference_valid(cache_entry->row_ref)) {
            // Process exists in cache
            gboolean was_visible = process_matches_filter(cache_entry->process);
            
            if (matches_filter && was_visible) {
                // Still visible - check if data changed
                if (process_data_changed(cache_entry->process, new_proc)) {
                    update_tree_row_by_ref(cache_entry->row_ref, new_proc);
                    
                    // Update cached process data
                    free_process(cache_entry->process);
                    cache_entry->process = malloc(sizeof(Process));
                    memcpy(cache_entry->process, new_proc, sizeof(Process));
                }
            } else if (matches_filter && !was_visible) {
                // Now matches filter - show it
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
                
                // Update row reference
                GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(liststore), &iter);
                if (cache_entry->row_ref) {
                    gtk_tree_row_reference_free(cache_entry->row_ref);
                }
                cache_entry->row_ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(liststore), path);
                gtk_tree_path_free(path);
                
                // Update cached process data
                free_process(cache_entry->process);
                cache_entry->process = malloc(sizeof(Process));
                memcpy(cache_entry->process, new_proc, sizeof(Process));
            } else if (!matches_filter && was_visible) {
                // No longer matches filter - hide it
                GtkTreePath *path = gtk_tree_row_reference_get_path(cache_entry->row_ref);
                if (path) {
                    GtkTreeIter iter;
                    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(liststore), &iter, path)) {
                        gtk_list_store_remove(liststore, &iter);
                    }
                    gtk_tree_path_free(path);
                }
                
                // Update cached process data but mark as hidden
                free_process(cache_entry->process);
                cache_entry->process = malloc(sizeof(Process));
                memcpy(cache_entry->process, new_proc, sizeof(Process));
            }
        } else if (matches_filter) {
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
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);
    
    // Add CSS for error styling
    GtkCssProvider *css_provider = gtk_css_provider_new();
    const char *css = ".error { background-color: #ffcccc; border: 1px solid #ff6666; }";
    gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                               GTK_STYLE_PROVIDER(css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER);

    // Main vertical box for layout
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    // System specs label
    specs_label = GTK_LABEL(gtk_label_new("Loading specs..."));
    gtk_box_pack_start(GTK_BOX(main_box), GTK_WIDGET(specs_label), FALSE, FALSE, 0);
    
    // System summary label (Networks, VM, Disks info)
    summary_label = GTK_LABEL(gtk_label_new("Loading system info..."));
    gtk_label_set_justify(summary_label, GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(GTK_BOX(main_box), GTK_WIDGET(summary_label), FALSE, FALSE, 0);

    // Horizontal box for filter panel and main content
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), content_box, TRUE, TRUE, 0);

    // Filter panel (left side)
    GtkWidget *filter_frame = gtk_frame_new("Filters");
    gtk_widget_set_size_request(filter_frame, 200, -1);
    gtk_box_pack_start(GTK_BOX(content_box), filter_frame, FALSE, FALSE, 0);

    GtkWidget *filter_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(filter_box), 10);
    gtk_container_add(GTK_CONTAINER(filter_frame), filter_box);

    // Filter entries with labels
    const char *filter_labels[] = {"PID (e.g. 100+):", "Name:", "CPU (e.g. 15%+):", 
                                   "GPU (e.g. 10%-):", "Memory (e.g. 100MB+):", 
                                   "Network (e.g. 1KB/S+):", "Type:"};
    const char *placeholders[] = {"100+", "chrome", "15%+", "5%-", "100MB+", "1KB/S+", "All"};

    for (int i = 0; i < 7; i++) {
        GtkWidget *label = gtk_label_new(filter_labels[i]);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(filter_box), label, FALSE, FALSE, 0);

        if (i == 6) { // Type filter - use combo box
            filter_entries[i] = gtk_combo_box_text_new_with_entry();
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(filter_entries[i]), "All");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(filter_entries[i]), "System");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(filter_entries[i]), "User");
            gtk_combo_box_set_active(GTK_COMBO_BOX(filter_entries[i]), 0); // Default to "All"
            g_signal_connect(filter_entries[i], "changed", 
                           G_CALLBACK(on_filter_changed), GINT_TO_POINTER(i));
        } else {
            filter_entries[i] = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(filter_entries[i]), placeholders[i]);
            g_signal_connect(filter_entries[i], "changed", G_CALLBACK(on_filter_changed), GINT_TO_POINTER(i));
        }
        
        gtk_box_pack_start(GTK_BOX(filter_box), filter_entries[i], FALSE, FALSE, 0);
    }

    // Clear filters button
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear All");
    gtk_box_pack_start(GTK_BOX(filter_box), clear_btn, FALSE, FALSE, 5);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_filters), NULL);

    // Main content area (right side)
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    global_scrolled_window = GTK_SCROLLED_WINDOW(scrolled_window);
    
    // Get the vertical adjustment for scroll position tracking
    vertical_adjustment = gtk_scrolled_window_get_vadjustment(global_scrolled_window);
    
    gtk_box_pack_start(GTK_BOX(content_box), scrolled_window, TRUE, TRUE, 0);

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

// Helper function to convert memory string to bytes for comparison
long long memory_to_bytes(const char *mem_str) {
    if (!mem_str || strlen(mem_str) == 0) return -1;
    
    char temp[50];
    strncpy(temp, mem_str, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    // Convert to uppercase for consistent comparison
    for (int i = 0; temp[i]; i++) {
        temp[i] = toupper(temp[i]);
    }
    
    char *endptr;
    double value = strtod(temp, &endptr);
    if (value < 0) return -1;
    
    // Skip whitespace
    while (*endptr && isspace(*endptr)) endptr++;
    
    // Check suffix (case-insensitive)
    if (strstr(endptr, "TB")) {
        return (long long)(value * 1024LL * 1024LL * 1024LL * 1024LL);
    } else if (strstr(endptr, "GB")) {
        return (long long)(value * 1024LL * 1024LL * 1024LL);
    } else if (strstr(endptr, "MB")) {
        return (long long)(value * 1024LL * 1024LL);
    } else if (strstr(endptr, "KB")) {
        return (long long)(value * 1024LL);
    } else if (strstr(endptr, "B") || strlen(endptr) == 0) {
        return (long long)value; // Bytes or no suffix
    } else {
        return -1; // Invalid suffix
    }
}

// Helper function to extract numeric value and operator from filter string
gboolean parse_numeric_filter(const char *filter, double *value, char *op, const char *suffix) {
    if (!filter || strlen(filter) == 0) return FALSE;
    
    char temp[50];
    strncpy(temp, filter, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    int len = strlen(temp);
    if (len == 0) return FALSE;
    
    // Check for operators at the end
    if (temp[len - 1] == '+') {
        *op = '+';
        temp[len - 1] = '\0';
        len--;
    } else if (temp[len - 1] == '-') {
        *op = '-';
        temp[len - 1] = '\0';
        len--;
    } else {
        *op = '='; // Exact match
    }
    
    // Remove suffix if provided (like % for percentages)
    if (suffix && strlen(suffix) > 0) {
        char *pos = strstr(temp, suffix);
        if (pos) {
            *pos = '\0';
            len = strlen(temp);
        }
    }
    
    if (len == 0) return FALSE;
    
    char *endptr;
    *value = strtod(temp, &endptr);
    return (endptr != temp && *value >= 0); // Valid if we parsed something and it's non-negative
}

// Helper function to parse memory filter with operators
gboolean parse_memory_filter(const char *filter, long long *bytes, char *op) {
    if (!filter || strlen(filter) == 0) return FALSE;
    
    char temp[50];
    strncpy(temp, filter, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    int len = strlen(temp);
    if (len == 0) return FALSE;
    
    // Check for operators at the end
    if (temp[len - 1] == '+') {
        *op = '+';
        temp[len - 1] = '\0';
    } else if (temp[len - 1] == '-') {
        *op = '-';
        temp[len - 1] = '\0';
    } else {
        *op = '='; // Exact match
    }
    
    *bytes = memory_to_bytes(temp);
    return (*bytes >= 0);
}

// Helper function to convert network rate string to bytes per second
long long network_to_bps(const char *net_str) {
    if (!net_str || strlen(net_str) == 0) return -1;
    
    char temp[50];
    strncpy(temp, net_str, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    // Convert to uppercase for consistent comparison
    for (int i = 0; temp[i]; i++) {
        temp[i] = toupper(temp[i]);
    }
    
    char *endptr;
    double value = strtod(temp, &endptr);
    if (value < 0) return -1;
    
    // Skip whitespace
    while (*endptr && isspace(*endptr)) endptr++;
    
    // Check for /S suffix
    char *per_sec = strstr(endptr, "/S");
    if (!per_sec) return -1; // Must have /s or /S
    
    // Check prefix before /S
    if (strstr(endptr, "GB/S")) {
        return (long long)(value * 1024LL * 1024LL * 1024LL);
    } else if (strstr(endptr, "MB/S")) {
        return (long long)(value * 1024LL * 1024LL);
    } else if (strstr(endptr, "KB/S")) {
        return (long long)(value * 1024LL);
    } else if (strstr(endptr, "B/S")) {
        return (long long)value;
    } else {
        return -1; // Invalid format
    }
}

// Helper function to parse network filter with operators
gboolean parse_network_filter(const char *filter, long long *bps, char *op) {
    if (!filter || strlen(filter) == 0) return FALSE;
    
    char temp[50];
    strncpy(temp, filter, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    int len = strlen(temp);
    if (len == 0) return FALSE;
    
    // Check for operators at the end
    if (temp[len - 1] == '+') {
        *op = '+';
        temp[len - 1] = '\0';
    } else if (temp[len - 1] == '-') {
        *op = '-';
        temp[len - 1] = '\0';
    } else {
        *op = '='; // Exact match
    }
    
    *bps = network_to_bps(temp);
    return (*bps >= 0);
}

// Function to check if a process matches the current filter criteria
gboolean process_matches_filter(Process *proc) {
    if (!current_filter.active) return TRUE;
    
    // PID filter
    if (strlen(current_filter.pid_filter) > 0) {
        double filter_pid;
        char op;
        if (parse_numeric_filter(current_filter.pid_filter, &filter_pid, &op, NULL)) {
            int proc_pid = atoi(proc->pid);
            switch (op) {
                case '+':
                    if (proc_pid < filter_pid) return FALSE;
                    break;
                case '-':
                    if (proc_pid > filter_pid) return FALSE;
                    break;
                case '=':
                    if (proc_pid != (int)filter_pid) return FALSE;
                    break;
            }
        }
    }
    
    // Name filter (substring search, case-insensitive)
    if (strlen(current_filter.name_filter) > 0) {
        char proc_name_lower[100], filter_lower[100];
        strncpy(proc_name_lower, proc->name, sizeof(proc_name_lower) - 1);
        strncpy(filter_lower, current_filter.name_filter, sizeof(filter_lower) - 1);
        
        // Convert to lowercase for case-insensitive search
        for (int i = 0; proc_name_lower[i]; i++) {
            proc_name_lower[i] = tolower(proc_name_lower[i]);
        }
        for (int i = 0; filter_lower[i]; i++) {
            filter_lower[i] = tolower(filter_lower[i]);
        }
        
        if (!strstr(proc_name_lower, filter_lower)) return FALSE;
    }
    
    // CPU filter
    if (strlen(current_filter.cpu_filter) > 0) {
        double filter_cpu;
        char op;
        if (parse_numeric_filter(current_filter.cpu_filter, &filter_cpu, &op, "%")) {
            double proc_cpu = strtod(proc->cpu, NULL);
            switch (op) {
                case '+':
                    if (proc_cpu < filter_cpu) return FALSE;
                    break;
                case '-':
                    if (proc_cpu > filter_cpu) return FALSE;
                    break;
                case '=':
                    if (fabs(proc_cpu - filter_cpu) > 0.1) return FALSE;
                    break;
            }
        }
    }
    
    // GPU filter
    if (strlen(current_filter.gpu_filter) > 0) {
        double filter_gpu;
        char op;
        if (parse_numeric_filter(current_filter.gpu_filter, &filter_gpu, &op, "%")) {
            double proc_gpu = strtod(proc->gpu, NULL);
            switch (op) {
                case '+':
                    if (proc_gpu < filter_gpu) return FALSE;
                    break;
                case '-':
                    if (proc_gpu > filter_gpu) return FALSE;
                    break;
                case '=':
                    if (fabs(proc_gpu - filter_gpu) > 0.1) return FALSE;
                    break;
            }
        }
    }
    
    // Memory filter
    if (strlen(current_filter.memory_filter) > 0) {
        long long filter_mem;
        char op;
        if (parse_memory_filter(current_filter.memory_filter, &filter_mem, &op)) {
            long long proc_mem = memory_to_bytes(proc->mem);
            if (proc_mem >= 0) {
                switch (op) {
                    case '+':
                        if (proc_mem < filter_mem) return FALSE;
                        break;
                    case '-':
                        if (proc_mem > filter_mem) return FALSE;
                        break;
                    case '=':
                        // Exact match within 10% tolerance
                        if (filter_mem > 0 && (llabs(proc_mem - filter_mem) > filter_mem * 0.1)) return FALSE;
                        break;
                }
            }
        }
    }
    
    // Network filter
    if (strlen(current_filter.network_filter) > 0) {
        long long filter_net;
        char op;
        if (parse_network_filter(current_filter.network_filter, &filter_net, &op)) {
            long long proc_net = network_to_bps(proc->net);
            if (proc_net >= 0) {
                switch (op) {
                    case '+':
                        if (proc_net < filter_net) return FALSE;
                        break;
                    case '-':
                        if (proc_net > filter_net) return FALSE;
                        break;
                    case '=':
                        // Exact match within 10% tolerance
                        if (filter_net > 0 && (llabs(proc_net - filter_net) > filter_net * 0.1)) return FALSE;
                        break;
                }
            }
        }
    }
    
    // Type filter
    if (strlen(current_filter.type_filter) > 0 && 
        strcmp(current_filter.type_filter, "All") != 0) {
        if (strcasecmp(current_filter.type_filter, proc->type) != 0) return FALSE;
    }
    
    return TRUE;
}

// Callback for filter entry changes
void on_filter_changed(GtkWidget *widget, gpointer user_data) {
    int filter_index = GPOINTER_TO_INT(user_data);
    const char *text;
    
    // Get text from entry or combo box
    if (filter_index == 6) { // Type combo box
        GtkWidget *entry = gtk_bin_get_child(GTK_BIN(widget));
        text = gtk_entry_get_text(GTK_ENTRY(entry));
    } else {
        text = gtk_entry_get_text(GTK_ENTRY(widget));
    }
    
    // Validate input and set background color
    gboolean valid = validate_filter_input(text, filter_index);
    
    // Set background color based on validation
    if (filter_index != 6) { // Don't color combo box
        GtkStyleContext *context = gtk_widget_get_style_context(widget);
        if (strlen(text) > 0) {
            if (valid) {
                gtk_style_context_remove_class(context, "error");
            } else {
                gtk_style_context_add_class(context, "error");
                return; // Don't apply invalid filter
            }
        } else {
            gtk_style_context_remove_class(context, "error");
        }
    }
    
    // Update filter criteria if valid
    if (valid) {
        switch (filter_index) {
            case 0: // PID
                strncpy(current_filter.pid_filter, text, sizeof(current_filter.pid_filter) - 1);
                current_filter.pid_filter[sizeof(current_filter.pid_filter) - 1] = '\0';
                break;
            case 1: // Name
                strncpy(current_filter.name_filter, text, sizeof(current_filter.name_filter) - 1);
                current_filter.name_filter[sizeof(current_filter.name_filter) - 1] = '\0';
                break;
            case 2: // CPU
                strncpy(current_filter.cpu_filter, text, sizeof(current_filter.cpu_filter) - 1);
                current_filter.cpu_filter[sizeof(current_filter.cpu_filter) - 1] = '\0';
                break;
            case 3: // GPU
                strncpy(current_filter.gpu_filter, text, sizeof(current_filter.gpu_filter) - 1);
                current_filter.gpu_filter[sizeof(current_filter.gpu_filter) - 1] = '\0';
                break;
            case 4: // Memory
                strncpy(current_filter.memory_filter, text, sizeof(current_filter.memory_filter) - 1);
                current_filter.memory_filter[sizeof(current_filter.memory_filter) - 1] = '\0';
                break;
            case 5: // Network
                strncpy(current_filter.network_filter, text, sizeof(current_filter.network_filter) - 1);
                current_filter.network_filter[sizeof(current_filter.network_filter) - 1] = '\0';
                break;
            case 6: // Type
                strncpy(current_filter.type_filter, text, sizeof(current_filter.type_filter) - 1);
                current_filter.type_filter[sizeof(current_filter.type_filter) - 1] = '\0';
                break;
        }
        
        // Enable filtering if any field has content
        current_filter.active = (strlen(current_filter.pid_filter) > 0 ||
                               strlen(current_filter.name_filter) > 0 ||
                               strlen(current_filter.cpu_filter) > 0 ||
                               strlen(current_filter.gpu_filter) > 0 ||
                               strlen(current_filter.memory_filter) > 0 ||
                               strlen(current_filter.network_filter) > 0 ||
                               (strlen(current_filter.type_filter) > 0 && 
                                strcmp(current_filter.type_filter, "All") != 0));
        
        // Apply filters to current display immediately
        apply_filters_to_display();
    }
}

// Callback for clear filters button
void on_clear_filters(GtkWidget *widget, gpointer user_data) {
    (void)widget; // Unused parameter
    (void)user_data; // Unused parameter
    
    // Clear all filter criteria
    memset(&current_filter, 0, sizeof(FilterCriteria));
    current_filter.active = FALSE;
    
    // Clear all filter entry widgets
    for (int i = 0; i < 7; i++) {
        if (filter_entries[i]) {
            if (i == 6) { // Type combo box
                gtk_combo_box_set_active(GTK_COMBO_BOX(filter_entries[i]), 0); // "All"
            } else {
                gtk_entry_set_text(GTK_ENTRY(filter_entries[i]), "");
                // Remove error styling
                GtkStyleContext *context = gtk_widget_get_style_context(filter_entries[i]);
                gtk_style_context_remove_class(context, "error");
            }
        }
    }
    
    // Apply cleared filters to display
    apply_filters_to_display();
}

// Function to validate filter input and provide visual feedback
gboolean validate_filter_input(const char *text, int filter_type) {
    if (!text || strlen(text) == 0) return TRUE; // Empty is valid
    
    switch (filter_type) {
        case 0: { // PID - should be numeric with optional +/-
            char temp[50];
            strncpy(temp, text, sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';
            
            // Remove operator suffix
            int len = strlen(temp);
            if (len > 0 && (temp[len - 1] == '+' || temp[len - 1] == '-')) {
                temp[len - 1] = '\0';
            }
            
            // Check if remaining is numeric
            char *endptr;
            long val = strtol(temp, &endptr, 10);
            return (endptr != temp && *endptr == '\0' && val >= 0);
        }
        case 1: // Name - any text is valid
            return TRUE;
        case 2: // CPU - should be numeric with % and optional +/-
        case 3: { // GPU - same as CPU
            char temp[50];
            strncpy(temp, text, sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';
            
            // Remove % suffix
            char *percent_pos = strstr(temp, "%");
            if (percent_pos) *percent_pos = '\0';
            
            // Remove operator suffix
            int len = strlen(temp);
            if (len > 0 && (temp[len - 1] == '+' || temp[len - 1] == '-')) {
                temp[len - 1] = '\0';
            }
            
            // Check if remaining is numeric
            char *endptr;
            double val = strtod(temp, &endptr);
            return (endptr != temp && *endptr == '\0' && val >= 0 && val <= 100);
        }
        case 4: { // Memory - should be numeric with unit and optional +/-
            char temp[50];
            strncpy(temp, text, sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';
            
            // Remove operator suffix
            int len = strlen(temp);
            if (len > 0 && (temp[len - 1] == '+' || temp[len - 1] == '-')) {
                temp[len - 1] = '\0';
            }
            
            // Remove unit suffixes
            char *units[] = {"TB", "GB", "MB", "KB", "B", "tb", "gb", "mb", "kb", "b"};
            for (int i = 0; i < 10; i++) {
                char *pos = strstr(temp, units[i]);
                if (pos && pos == temp + strlen(temp) - strlen(units[i])) {
                    *pos = '\0';
                    break;
                }
            }
            
            // Check if remaining is numeric
            char *endptr;
            double val = strtod(temp, &endptr);
            return (endptr != temp && *endptr == '\0' && val >= 0);
        }
        case 5: { // Network - should be numeric with rate units and +/-
            if (strlen(text) == 0) return TRUE;
            long long bps;
            char op;
            return parse_network_filter(text, &bps, &op);
        }
        case 6: // Type - should be System, User, or All
            return (strcasecmp(text, "System") == 0 || 
                   strcasecmp(text, "User") == 0 || 
                   strcasecmp(text, "All") == 0);
        default:
            return TRUE;
    }
}

// Function to apply current filters to all visible processes
void apply_filters_to_display(void) {
    if (!process_cache || !current_filter.active) return;
    
    g_mutex_lock(&cache_mutex);
    
    // Get all processes from cache and check filter status
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, process_cache);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ProcessCacheEntry *entry = (ProcessCacheEntry *)value;
        if (!entry || !entry->process) continue;
        
        gboolean should_be_visible = process_matches_filter(entry->process);
        gboolean is_currently_visible = (entry->row_ref && 
                                        gtk_tree_row_reference_valid(entry->row_ref));
        
        if (should_be_visible && !is_currently_visible) {
            // Show this process
            GtkTreeIter tree_iter;
            gtk_list_store_append(liststore, &tree_iter);
            gtk_list_store_set(liststore, &tree_iter,
                               COL_PID, entry->process->pid,
                               COL_NAME, entry->process->name,
                               COL_CPU, entry->process->cpu,
                               COL_GPU, entry->process->gpu,
                               COL_MEM, entry->process->mem,
                               COL_NET, entry->process->net,
                               COL_RUNTIME, entry->process->runtime,
                               COL_TYPE, entry->process->type,
                               -1);
            
            // Update row reference
            GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(liststore), &tree_iter);
            if (entry->row_ref) {
                gtk_tree_row_reference_free(entry->row_ref);
            }
            entry->row_ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(liststore), path);
            gtk_tree_path_free(path);
            
        } else if (!should_be_visible && is_currently_visible) {
            // Hide this process
            GtkTreePath *path = gtk_tree_row_reference_get_path(entry->row_ref);
            if (path) {
                GtkTreeIter tree_iter;
                if (gtk_tree_model_get_iter(GTK_TREE_MODEL(liststore), &tree_iter, path)) {
                    gtk_list_store_remove(liststore, &tree_iter);
                }
                gtk_tree_path_free(path);
            }
            
            // Clear row reference
            if (entry->row_ref) {
                gtk_tree_row_reference_free(entry->row_ref);
                entry->row_ref = NULL;
            }
        }
    }
    
    g_mutex_unlock(&cache_mutex);
}
