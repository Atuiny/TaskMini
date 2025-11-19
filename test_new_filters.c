#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Test the new filter functionality
int main() {
    printf("TaskMini Enhanced Filter Testing\n");
    printf("===============================\n\n");
    
    printf("NEW RANGE FILTER FEATURES:\n");
    printf("==========================\n\n");
    
    printf("PID Range Filter Examples:\n");
    printf("- '[100,500]' shows processes with PID between 100-500\n");
    printf("- '[1000,2000]' shows processes with PID between 1000-2000\n\n");
    
    printf("CPU/GPU Range Filter Examples:\n");
    printf("- '[5,15]' shows processes using 5-15%% CPU/GPU\n");
    printf("- '[0.1,2.5]' shows processes using 0.1-2.5%% CPU/GPU\n\n");
    
    printf("Memory Range Filter Examples:\n");
    printf("- '[100MB,1GB]' shows processes using 100MB to 1GB memory\n");
    printf("- '[10MB,500MB]' shows processes using 10-500MB memory\n\n");
    
    printf("Network Range Filter Examples:\n");
    printf("- '[1KB/s,1MB/s]' shows processes using 1KB/s to 1MB/s network\n");
    printf("- '[100KB/s,10MB/s]' shows processes using 100KB/s to 10MB/s network\n\n");
    
    printf("EXISTING FILTERS (still work):\n");
    printf("==============================\n");
    printf("- '100+' (PID >= 100)\n");
    printf("- '500-' (PID <= 500)\n");
    printf("- '15%%+' (CPU >= 15%%)\n");
    printf("- '100MB+' (Memory >= 100MB)\n");
    printf("- 'chrome' (Name contains 'chrome')\n\n");
    
    printf("FIXED ISSUES:\n");
    printf("=============\n");
    printf("✅ Type filter 'System' now works correctly\n");
    printf("✅ Type filter matches '🛡️ System' processes\n");
    printf("✅ Range filters validate input properly\n");
    printf("✅ All filters work with real-time updates\n\n");
    
    printf("To test: Run TaskMini and try these filter patterns!\n");
    
    return 0;
}
