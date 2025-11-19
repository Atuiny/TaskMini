#include "utils.h"
#include <ctype.h>
#include <math.h>

// Parse bytes string like "1234 B" or "5.6 MiB" to long long bytes
long long parse_bytes(const char *str) {
    if (!str) return 0;
    
    char *end;
    double val = strtod(str, &end);
    if (end == str) return 0;

    while (*end == ' ') end++;  // Skip space before unit

    if (strncasecmp(end, "B", 1) == 0) return (long long)val;
    if (strncasecmp(end, "K", 1) == 0) return (long long)(val * 1024);
    if (strncasecmp(end, "M", 1) == 0) return (long long)(val * 1024 * 1024);
    if (strncasecmp(end, "G", 1) == 0) return (long long)(val * 1024 * 1024 * 1024);
    return 0;
}

// Helper function to format bytes as human-readable strings (for rates)
char* format_bytes_human_readable(long long bytes) {
    if (bytes < 1024) {
        return g_strdup_printf("%lld B", bytes);
    } else if (bytes < 1024 * 1024) {
        return g_strdup_printf("%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        return g_strdup_printf("%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        return g_strdup_printf("%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

// Helper function to parse memory strings like "26G", "1598M" into bytes
long long parse_memory_string(const char *str) {
    if (!str || strlen(str) == 0) return 0;
    
    long long value = strtoll(str, NULL, 10);
    char unit = str[strlen(str) - 1];
    
    switch (unit) {
        case 'K': case 'k':
            return value * 1024LL;
        case 'M': case 'm':
            return value * 1024LL * 1024LL;
        case 'G': case 'g':
            return value * 1024LL * 1024LL * 1024LL;
        case 'T': case 't':
            return value * 1024LL * 1024LL * 1024LL * 1024LL;
        default:
            // If no unit, assume bytes
            return value;
    }
}

// Convert memory string from top output to human-readable format
char* format_memory_human_readable(const char *mem_str) {
    if (!mem_str) return strdup("0 B");
    
    // Parse the memory value from top output (like "1024M", "512K", "2.5G")
    long long bytes = parse_memory_string(mem_str);
    
    char *result = malloc(20);
    
    if (bytes >= 1024LL * 1024 * 1024) {
        // >= 1 GB: show as GB with 2 decimal places
        double gb = (double)bytes / (1024LL * 1024 * 1024);
        if (gb >= 10.0) {
            sprintf(result, "%.1f GB", gb);  // 10.1 GB (1 decimal for large values)
        } else {
            sprintf(result, "%.2f GB", gb);  // 1.25 GB (2 decimals for smaller GB values)
        }
    } else if (bytes >= 1024 * 1024) {
        // >= 1 MB: show as MB with 1 decimal place
        double mb = (double)bytes / (1024 * 1024);
        if (mb >= 100.0) {
            sprintf(result, "%.0f MB", mb);  // 512 MB (no decimals for large MB values)
        } else {
            sprintf(result, "%.1f MB", mb);  // 64.5 MB (1 decimal for smaller MB values)
        }
    } else if (bytes >= 1024) {
        // >= 1 KB: show as KB with no decimals
        long kb = bytes / 1024;
        sprintf(result, "%ld KB", kb);  // 512 KB
    } else {
        // < 1 KB: show as bytes
        sprintf(result, "%lld B", bytes);  // 256 B
    }
    
    return result;
}

// Parse run time string to seconds for sorting, e.g., "01:23:45" or "1-02:34:56"
long long parse_runtime_to_seconds(const char *str) {
    if (!str) return 0;
    
    int days = 0, hours = 0, mins = 0, secs = 0;
    
    if (sscanf(str, "%d-%d:%d:%d", &days, &hours, &mins, &secs) == 4) {
        // Format: days-hours:mins:secs
    } else if (sscanf(str, "%d:%d:%d", &hours, &mins, &secs) == 3) {
        // Format: hours:mins:secs
        days = 0;
    } else if (sscanf(str, "%d:%d", &mins, &secs) == 2) {
        // Format: mins:secs
        days = 0;
        hours = 0;
    } else {
        return 0;
    }
    
    return (long long)days * 86400 + hours * 3600 + mins * 60 + secs;
}
