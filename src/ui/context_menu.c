#include "../ui/ui.h"
#include "../system/system.h"

// Context menu for process management
void show_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    GtkTreeView *treeview = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return; // No selection
    }
    
    // Get process info
    gchar *pid = NULL, *name = NULL, *type = NULL;
    gtk_tree_model_get(model, &iter, 
                       COL_PID, &pid, 
                       COL_NAME, &name, 
                       COL_TYPE, &type, 
                       -1);
    
    // Don't show context menu for system processes
    if (type && strstr(type, "🛡️") != NULL) {
        g_free(pid);
        g_free(name);
        g_free(type);
        return;
    }
    
    // Create context menu
    GtkWidget *menu = gtk_menu_new();
    
    // Kill process menu item
    gchar *menu_text = g_strdup_printf("Terminate Process %s (%s)", name ? name : "Unknown", pid ? pid : "0");
    GtkWidget *kill_item = gtk_menu_item_new_with_label(menu_text);
    g_free(menu_text);
    
    // Store PID and name in menu item for callback
    g_object_set_data_full(G_OBJECT(kill_item), "pid", g_strdup(pid), g_free);
    g_object_set_data_full(G_OBJECT(kill_item), "name", g_strdup(name), g_free);
    
    g_signal_connect(kill_item, "activate", G_CALLBACK(kill_process_callback), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), kill_item);
    
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
    
    g_free(pid);
    g_free(name);
    g_free(type);
}

// Right-click event handler
gboolean on_treeview_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { // Right click
        GtkTreeView *treeview = GTK_TREE_VIEW(widget);
        GtkTreePath *path;
        
        // Select the row under cursor
        if (gtk_tree_view_get_path_at_pos(treeview, (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL)) {
            gtk_tree_view_set_cursor(treeview, path, NULL, FALSE);
            gtk_tree_path_free(path);
            
            show_context_menu(widget, event, treeview);
        }
        return TRUE; // Handled
    }
    return FALSE; // Let other handlers process
}

// Kill process callback
void kill_process_callback(GtkWidget *menuitem, gpointer user_data) {
    const char *pid = (const char*)g_object_get_data(G_OBJECT(menuitem), "pid");
    const char *name = (const char*)g_object_get_data(G_OBJECT(menuitem), "name");
    
    if (!pid || !name) return;
    
    // Create confirmation dialog
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "Are you sure you want to terminate process '%s' (PID: %s)?", 
        name, pid
    );
    
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "This action cannot be undone. The process will be forcefully terminated."
    );
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (result == GTK_RESPONSE_YES) {
        // Kill the process
        char cmd[100];
        snprintf(cmd, sizeof(cmd), "kill -TERM %s 2>/dev/null || kill -9 %s 2>/dev/null", pid, pid);
        
        int exit_code = system(cmd);
        
        // Show result
        const char *message = (exit_code == 0) ? 
            "Process terminated successfully." : 
            "Failed to terminate process. It may have already ended or require administrative privileges.";
            
        GtkWidget *result_dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            (exit_code == 0) ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "%s", message
        );
        
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
    }
}
