#include "src/system/threaded_collector.h"
#include "src/system/system.h"
#include "src/utils/utils.h"
#include <stdio.h>

int main() {
    printf("Testing data collection...\n");
    
    // Test the synchronous data collection function directly
    extern UpdateData* collect_complete_data_sync(void);
    UpdateData *data = collect_complete_data_sync();
    
    if (data) {
        printf("Data collection successful!\n");
        printf("Process count: %d\n", g_list_length(data->processes));
        printf("System CPU: %.1f%%\n", data->system_cpu_usage);
        printf("System Memory: %.1f%%\n", data->system_memory_usage);
        
        if (data->system_summary) {
            printf("System summary length: %zu\n", strlen(data->system_summary));
        }
        
        if (data->gpu_usage) {
            printf("GPU usage: %s\n", data->gpu_usage);
        }
        
        // Show first few processes
        GList *l = data->processes;
        int count = 0;
        while (l && count < 3) {
            Process *proc = (Process*)l->data;
            printf("Process: PID=%s, Name=%s, CPU=%s, Mem=%s\n", 
                   proc->pid, proc->name, proc->cpu, proc->mem);
            l = l->next;
            count++;
        }
        
        free_update_data(data);
    } else {
        printf("Data collection failed!\n");
    }
    
    return 0;
}
