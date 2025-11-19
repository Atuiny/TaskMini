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

// Security tracking
time_t last_update_time = 0;
int consecutive_failures = 0;

// Function to restore scroll position with proper timing
gboolean restore_scroll_position(gpointer user_data) {
    gdouble *scroll_position = (gdouble *)user_data;
    
    if (vertical_adjustment && *scroll_position > 0.0) {
        // Clamp the value to valid range
        gdouble upper = gtk_adjustment_get_upper(vertical_adjustment);
        gdouble page_size = gtk_adjustment_get_page_size(vertical_adjustment);
        gdouble max_value = upper - page_size;
        
        if (*scroll_position > max_value) {
            *scroll_position = max_value;
        }
        
        gtk_adjustment_set_value(vertical_adjustment, *scroll_position);
    }
    
    return G_SOURCE_REMOVE; // Remove this source
}

// Function called in main thread via g_idle_add. Updates the liststore with 
// robust scroll position preservation using both model detachment and explicit scroll restoration.
gboolean update_ui_func(gpointer user_data) {
    UpdateData *data = (UpdateData *)user_data;

    // Save current scroll position before any updates
    gdouble scroll_position = 0.0;
    if (vertical_adjustment) {
        scroll_position = gtk_adjustment_get_value(vertical_adjustment);
    }

    // Temporarily detach model from TreeView to prevent scroll jumping
    GtkTreeModel *model = NULL;
    if (global_treeview) {
        model = gtk_tree_view_get_model(global_treeview);
        if (model) {
            g_object_ref(model);  // Keep reference while detached
            gtk_tree_view_set_model(global_treeview, NULL);  // Detach
        }
    }

    // Update the list store while TreeView is detached
    gtk_list_store_clear(liststore);

    GtkTreeIter iter;
    GList *l;
    for (l = data->processes; l != NULL; l = l->next) {
        Process *proc = (Process *)l->data;
        gtk_list_store_append(liststore, &iter);
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
        free_process(proc);
    }
    g_list_free(data->processes);

    // Reattach the model
    if (global_treeview && model) {
        gtk_tree_view_set_model(global_treeview, model);
        g_object_unref(model);
    }

    // Explicitly restore scroll position after a short delay to ensure model is fully updated
    if (vertical_adjustment && scroll_position > 0.0) {
        // Use g_idle_add_full with low priority to ensure this runs after all other updates
        gdouble *pos_copy = g_malloc(sizeof(gdouble));
        *pos_copy = scroll_position;
        g_idle_add_full(G_PRIORITY_LOW, (GSourceFunc)restore_scroll_position, pos_copy, g_free);
    }

    // Don't force re-sort - let user's column sort choice persist
    // The TreeView will maintain the current sort order automatically

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

    // Get static specs
    static_specs = get_static_specs();

    // Initial update via thread
    updating = TRUE;
    g_thread_new("update_thread", update_thread_func, NULL);

    // Timer - refresh every 0.5 seconds for more responsive updates
    g_timeout_add(UI_UPDATE_INTERVAL_MS, timeout_callback, NULL);

    gtk_widget_show_all(window);
}
