#include "utils.h"
#include <ctype.h>

// SECURITY: Input validation for commands - allows safe system commands
gboolean is_safe_command(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return FALSE;
    if (strlen(cmd) > 1024) return FALSE;  // Increased limit for system_profiler commands
    
    // Whitelist of safe command prefixes
    const char *safe_commands[] = {
        "top", "ps", "sysctl", "nettop", "powermetrics", 
        "system_profiler", "sw_vers", "df", "awk", "grep", 
        "head", "tail", "sed", "kill", NULL
    };
    
    // Check if command starts with a safe prefix
    gboolean is_safe = FALSE;
    for (int i = 0; safe_commands[i]; i++) {
        if (strncmp(cmd, safe_commands[i], strlen(safe_commands[i])) == 0) {
            is_safe = TRUE;
            break;
        }
    }
    
    if (!is_safe) return FALSE;
    
    // Special handling for commands that need pipes and quotes - check this FIRST
    if (strncmp(cmd, "system_profiler", 15) == 0 || 
        strncmp(cmd, "nettop", 6) == 0 ||
        strncmp(cmd, "ps", 2) == 0 ||
        strncmp(cmd, "df", 2) == 0) {
        
        // Still check for the most dangerous injection characters even for special commands
        const char *dangerous[] = {";", "&", "`", "$(", "${", "\\", NULL};
        for (int i = 0; dangerous[i]; i++) {
            if (strstr(cmd, dangerous[i])) return FALSE;
        }
        
        return TRUE; // Allow pipes and quotes for these specific commands
    }
    
    // For other commands, check for all dangerous characters including pipes and quotes
    const char *all_dangerous[] = {";", "&", "`", "$(", "${", "\\", "|", ">", "<", "'", "\"", NULL};
    for (int i = 0; all_dangerous[i]; i++) {
        if (strstr(cmd, all_dangerous[i])) return FALSE;
    }
    
    return TRUE;
}

// SECURITY: Safe string operations with bounds checking
void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';  // Ensure null termination
}

void safe_strncat(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    size_t current_len = strnlen(dest, dest_size - 1);
    size_t remaining = dest_size - current_len - 1;
    
    if (remaining > 0) {
        strncat(dest, src, remaining);
        dest[dest_size - 1] = '\0';  // Ensure null termination
    }
}

// Helper to run a command and get its trimmed output as string
char* run_command(const char *cmd) {
    // SECURITY: Validate command before execution
    if (!is_safe_command(cmd)) {
        return strdup("N/A");
    }
    
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return strdup("N/A");

    char *buffer = get_cached_buffer(256);
    if (fgets(buffer, 256, fp) == NULL) {
        return_cached_buffer(buffer, 256);
        pclose(fp);
        return strdup("N/A");
    }
    
    // OPTIMIZATION: In-place newline removal (faster than strcspn)
    char *newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';
    
    pclose(fp);
    
    // Create final result and return buffer to cache
    char *result = strdup(buffer);
    return_cached_buffer(buffer, 256);
    return result;
}

// OPTIMIZATION: Improved get_full_output with better memory management
char* get_full_output(const char *cmd) {
    if (!is_safe_command(cmd)) {
        return NULL;
    }
    
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return NULL;

    size_t buffer_size = 8192;  // Start with larger buffer
    size_t total_len = 0;
    char *buffer = get_cached_buffer(buffer_size);
    char chunk[2048];  // Larger read chunks for better performance
    size_t bytes_read = 0;

    while ((bytes_read = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (total_len + bytes_read + 1 > buffer_size) {
            buffer_size *= 2;
            char *new_buffer = malloc(buffer_size);
            memcpy(new_buffer, buffer, total_len);
            return_cached_buffer(buffer, buffer_size / 2);
            buffer = new_buffer;
        }
        
        // OPTIMIZATION: Direct memory copy instead of strncat (much faster)
        memcpy(buffer + total_len, chunk, bytes_read);
        total_len += bytes_read;
    }
    
    buffer[total_len] = '\0';
    pclose(fp);
    
    // Create final result with exact size needed
    char *result = malloc(total_len + 1);
    memcpy(result, buffer, total_len + 1);
    
    // Return working buffer to cache if it's a reasonable size
    if (buffer_size <= 32768) {
        return_cached_buffer(buffer, buffer_size);
    } else {
        free(buffer);  // Too large for cache
    }
    
    return result;
}
