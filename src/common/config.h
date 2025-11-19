#ifndef CONFIG_H
#define CONFIG_H

// SECURITY: Resource limits to prevent DoS
#define MAX_PROCESSES_PER_UPDATE 2000
#define MAX_COMMAND_OUTPUT_SIZE (1024 * 1024)  // 1MB limit
#define MAX_UPDATE_TIME_MS 5000  // 5 second timeout

// OPTIMIZATION: Memory pool configuration
#define PROCESS_POOL_SIZE 512

// OPTIMIZATION: String buffer cache configuration  
#define STRING_CACHE_SIZE 16

// Update intervals
#define UI_UPDATE_INTERVAL_MS 250  // 0.25 seconds (faster refresh)

// Network and GPU monitoring intervals
#define GPU_CHECK_INTERVAL 2        // Cache GPU result for 2 seconds
#define NETWORK_CACHE_INTERVAL 1    // Cache network data for 1 second

// Security limits
#define MAX_FAILURES 5              // Maximum consecutive failures before throttling

#endif // CONFIG_H
