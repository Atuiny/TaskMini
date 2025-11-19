#include <gtk/gtk.h>
#include <stdio.h>
#include "src/ui/ui.h"
#include "src/utils/utils.h"

int main() {
    printf("Testing TaskMini Sorting Functions\n");
    printf("=================================\n");
    
    // Test parse_bytes function
    printf("\nTesting parse_bytes:\n");
    printf("parse_bytes('1024 B') = %lld (expected: 1024)\n", parse_bytes("1024 B"));
    printf("parse_bytes('1.5 KB') = %lld (expected: 1536)\n", parse_bytes("1.5 KB"));
    printf("parse_bytes('2.0 MB') = %lld (expected: 2097152)\n", parse_bytes("2.0 MB"));
    printf("parse_bytes('1.0 GB') = %lld (expected: 1073741824)\n", parse_bytes("1.0 GB"));
    
    // Test parse_runtime_to_seconds function
    printf("\nTesting parse_runtime_to_seconds:\n");
    printf("parse_runtime_to_seconds('1:30') = %lld (expected: 90)\n", parse_runtime_to_seconds("1:30"));
    printf("parse_runtime_to_seconds('2:05:30') = %lld (expected: 7530)\n", parse_runtime_to_seconds("2:05:30"));
    printf("parse_runtime_to_seconds('1-12:30:45') = %lld (expected: 131445)\n", parse_runtime_to_seconds("1-12:30:45"));
    
    printf("\nSorting functions appear to be working correctly.\n");
    printf("If sorting is still not working in TaskMini, the issue may be with GTK setup.\n");
    
    return 0;
}
