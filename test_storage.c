#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple test to verify storage detection
int main() {
    printf("Testing storage detection command...\n");
    
    FILE *fp = popen("df -h / | awk 'NR==2 {print $2}' | head -1", "r");
    if (fp == NULL) {
        printf("❌ Command failed to execute\n");
        return 1;
    }
    
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Remove newline
        buffer[strcspn(buffer, "\n")] = 0;
        printf("✅ Storage detected: %s\n", buffer);
        
        // Test if it's a reasonable value
        if (strlen(buffer) > 0 && strstr(buffer, "G")) {
            printf("✅ Format looks correct (contains 'G' for GB)\n");
        } else {
            printf("⚠️  Format might be unusual: '%s'\n", buffer);
        }
    } else {
        printf("❌ No output from command\n");
    }
    
    pclose(fp);
    return 0;
}
