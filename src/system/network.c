#include "system.h"
#include "../utils/utils.h"
#include "../common/config.h"
#include <time.h>

// OPTIMIZATION: Network monitoring with reduced calls and improved parsing
// Cache network data collection to avoid excessive nettop calls
time_t last_net_collection = 0;
GHashTable *net_cache = NULL;

// Individual network lookup (for specific PID when needed)
static long long get_net_bytes_individual(const char *pid) {
    char cmd[100];
    snprintf(cmd, sizeof(cmd), "nettop -p %s -L1", pid);
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return 0;

    char line[512];
    long long total = 0;
    int is_header = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (is_header) {
            is_header = 0;
            continue;
        }
        
        if (strlen(line) < 10) continue;
        
        // OPTIMIZATION: Direct parsing without strdup/strtok (faster)
        char *pos = line;
        int col = 0;
        long long bytes_in = 0, bytes_out = 0;
        
        while (*pos && col <= 5) {
            if (col == 4) bytes_in = strtoll(pos, &pos, 10);
            else if (col == 5) bytes_out = strtoll(pos, &pos, 10);
            else {
                // Skip to next comma
                while (*pos && *pos != ',') pos++;
            }
            
            if (*pos == ',') {
                pos++;
                col++;
            } else {
                break;
            }
        }
        
        if (col > 5) {
            total += bytes_in + bytes_out;
        }
    }
    pclose(fp);
    return total;
}

long long get_net_bytes(const char *pid) {
    time_t now = time(NULL);
    
    // OPTIMIZATION: Use cached network data if collected recently (within 1 second)
    if (net_cache && (now - last_net_collection) < NETWORK_CACHE_INTERVAL) {
        gpointer value = g_hash_table_lookup(net_cache, pid);
        return value ? *(long long*)value : 0;
    }
    
    // Return 0 if no cached data available
    return 0;
}

// OPTIMIZATION: Collect network data for all processes in one nettop call
void collect_all_network_data(void) {
    if (!net_cache) {
        net_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    } else {
        g_hash_table_remove_all(net_cache);  // Clear old data
    }
    
    // Single nettop call for all processes (much more efficient)
    FILE *fp = popen("nettop -L1 -P", "r");
    if (fp == NULL) return;

    char line[512];
    int is_header = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (is_header) {
            is_header = 0;
            continue;
        }
        
        if (strlen(line) < 10) continue;
        
        // OPTIMIZATION: Fast CSV parsing with direct pointer manipulation
        char *pos = line;
        char *fields[8];  // We need at least 6 fields
        int field_count = 0;
        
        // Parse CSV fields
        while (*pos && field_count < 8) {
            fields[field_count] = pos;
            
            // Find next comma or end
            while (*pos && *pos != ',') pos++;
            if (*pos) {
                *pos = '\0';  // Null terminate field
                pos++;
            }
            field_count++;
        }
        
        if (field_count >= 6) {
            // PID is field 0, bytes_in is field 4, bytes_out is field 5
            char *pid_str = fields[0];
            long long bytes_in = atoll(fields[4]);
            long long bytes_out = atoll(fields[5]);
            long long total = bytes_in + bytes_out;
            
            if (total > 0) {  // Only cache processes with network activity
                long long *cache_value = malloc(sizeof(long long));
                *cache_value = total;
                g_hash_table_insert(net_cache, g_strdup(pid_str), cache_value);
            }
        }
    }
    pclose(fp);
    
    last_net_collection = time(NULL);
}
