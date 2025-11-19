#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "ui/ui.h"
#include "system/system.h"
#include "system/performance.h"
#include "utils/utils.h"
#include "utils/memory_pool.h"

#ifndef TESTING
int main(int argc, char **argv) {
    // Register cleanup handler
    atexit(cleanup_resources);
    
    // Initialize optimized memory pools early
    init_memory_pools();
    init_process_pool();
    
    app = gtk_application_new("com.example.TaskMini", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}
#endif
