#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>
#include <glib.h>
#include "../common/types.h"
#include "../system/threaded_collector.h"

// UI callback functions
void activate(GtkApplication *app, gpointer user_data);
gboolean timeout_callback(gpointer data);
gboolean update_ui_func(gpointer user_data);
gboolean update_ui_progressive(gpointer user_data);
gboolean restore_scroll_position(gpointer user_data);

// Progressive update functions
void update_process_list_basic(GList *processes);
void update_process_list_complete(GList *processes);
void update_system_summary(UpdateData *data);
void update_column_headers(UpdateData *data);
void save_scroll_position(void);
// Scroll position preserved using model detachment + explicit adjustment restoration

// Context menu functions
void show_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
gboolean on_treeview_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
void kill_process_callback(GtkWidget *menuitem, gpointer user_data);

// Sorting functions
gint process_compare_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data);

// Global UI variables (declared in ui/ui.c)
extern GtkApplication *app;
extern GtkListStore *liststore;
extern GtkLabel *specs_label;
extern GtkLabel *summary_label;
extern char *static_specs;
extern gboolean updating;
extern GMutex hash_mutex;

// Global widgets for scroll position preservation
extern GtkTreeView *global_treeview;
extern GtkScrolledWindow *global_scrolled_window;
extern GtkAdjustment *vertical_adjustment;

// Hash tables for network tracking
extern GHashTable *prev_net_bytes;
extern GHashTable *prev_times;

// Security tracking
extern time_t last_update_time;
extern int consecutive_failures;
extern gboolean update_thread_running;
extern time_t update_start_time;

// Filter functions
void on_clear_filters(GtkWidget *widget, gpointer user_data);

// Cleanup function
void cleanup_ui_resources(void);

#endif // UI_H
