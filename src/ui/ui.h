#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>
#include <glib.h>
#include "../common/types.h"

// UI callback functions
void activate(GtkApplication *app, gpointer user_data);
gboolean timeout_callback(gpointer data);
gboolean update_ui_func(gpointer user_data);
// Scroll position restoration is now done directly in update_ui_func

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

// Hash tables for network tracking
extern GHashTable *prev_net_bytes;
extern GHashTable *prev_times;

// Security tracking
extern time_t last_update_time;
extern int consecutive_failures;
extern gboolean update_thread_running;
extern time_t update_start_time;

#endif // UI_H
