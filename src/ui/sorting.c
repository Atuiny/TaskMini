#include "../ui/ui.h"
#include "../utils/utils.h"
#include <math.h>

// Custom compare function for sorting columns. Handles different types: int for PID, 
// string for Name, float for CPU/Net/GPU, bytes for Mem, seconds for Runtime.
gint process_compare_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    int col = GPOINTER_TO_INT(user_data);
    gchar *va = NULL, *vb = NULL;
    gtk_tree_model_get(model, a, col, &va, -1);
    gtk_tree_model_get(model, b, col, &vb, -1);

    if (va == NULL || vb == NULL) {
        gint ret = (va == NULL) - (vb == NULL);
        g_free(va);
        g_free(vb);
        return ret;
    }

    gint ret = 0;
    switch (col) {
        case COL_PID: {
            int ia = atoi(va), ib = atoi(vb);
            ret = ia - ib;
            break;
        }
        case COL_NAME: {
            ret = g_strcmp0(va, vb);
            break;
        }
        case COL_CPU:
        case COL_GPU:
        case COL_NET: {  // Treat N/A or KB/s as float (strip non-numeric if needed, but simple atof works)
            float fa = atof(va), fb = atof(vb);
            
            // More robust float comparison to avoid precision issues
            if (fabs(fa - fb) < 0.001) {
                ret = 0;
            } else {
                ret = (fa > fb) ? 1 : (fa < fb) ? -1 : 0;
            }
            break;
        }
        case COL_MEM: {
            long long la = parse_bytes(va), lb = parse_bytes(vb);
            ret = (la > lb) ? 1 : (la < lb) ? -1 : 0;
            break;
        }
        case COL_RUNTIME: {
            long long la = parse_runtime_to_seconds(va), lb = parse_runtime_to_seconds(vb);
            ret = (la > lb) ? 1 : (la < lb) ? -1 : 0;
            break;
        }
        case COL_TYPE: {
            ret = g_strcmp0(va, vb);
            break;
        }
    }
    g_free(va);
    g_free(vb);
    return ret;
}
