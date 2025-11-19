#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Simple test for filter syntax
int main() {
    printf("Testing TaskMini Filter Syntax\n");
    printf("==============================\n\n");
    
    printf("PID Filter Examples:\n");
    printf("- '100+' should show PIDs >= 100\n");
    printf("- '500-' should show PIDs <= 500\n");
    printf("- '1234' should show PID exactly 1234\n\n");
    
    printf("Name Filter Examples:\n");
    printf("- 'Safari' should show processes containing 'Safari'\n");
    printf("- 'python' should show processes containing 'python'\n");
    printf("- Case insensitive matching\n\n");
    
    printf("CPU/GPU Filter Examples:\n");
    printf("- '5.0+' should show processes using >= 5.0%% CPU/GPU\n");
    printf("- '10-' should show processes using <= 10%% CPU/GPU\n");
    printf("- '0' should show processes using exactly 0%% CPU/GPU\n\n");
    
    printf("Memory Filter Examples:\n");
    printf("- '100MB+' should show processes using >= 100MB\n");
    printf("- '1GB-' should show processes using <= 1GB\n\n");
    
    printf("Network Filter Examples:\n");
    printf("- '1MB/s+' should show processes using >= 1MB/s\n");
    printf("- '500KB/s-' should show processes using <= 500KB/s\n\n");
    
    printf("Type Filter Examples:\n");
    printf("- 'User' - show only user processes\n");
    printf("- 'System' - show only system processes\n");
    printf("- 'All' - show all processes (default)\n\n");
    
    printf("To test: Run TaskMini and try entering these filter patterns\n");
    printf("in the filter panel on the left side of the window.\n");
    
    return 0;
}
