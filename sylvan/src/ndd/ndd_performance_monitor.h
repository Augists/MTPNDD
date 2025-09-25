// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef NDD_PERFORMANCE_MONITOR_H
#define NDD_PERFORMANCE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Performance monitoring configuration
typedef struct ndd_perf_config_s {
    bool enabled;                    // Global switch
    bool collect_timing;            // Collect timing statistics
    bool collect_memory;            // Collect memory statistics  
    bool collect_operations;        // Collect operation statistics
    bool collect_cache;             // Collect cache statistics
    bool collect_parallel;          // Collect parallel efficiency statistics
    uint32_t sample_interval_ms;    // Sampling interval (milliseconds)
} ndd_perf_config_t;

// Timing statistics
typedef struct ndd_timing_stats_s {
    uint64_t total_operations;      // Total operation count
    double total_time_ns;           // Total time (nanoseconds)
    double min_time_ns;             // Minimum operation time
    double max_time_ns;             // Maximum operation time
    double avg_time_ns;             // Average operation time
    
    // Categorized operation times
    double and_time_ns;
    double or_time_ns;
    double not_time_ns;
    double encode_time_ns;
    double gc_time_ns;
} ndd_timing_stats_t;

// Memory statistics
typedef struct ndd_memory_stats_s {
    uint64_t current_allocated;     // Currently allocated memory
    uint64_t peak_allocated;        // Peak allocated memory
    uint64_t total_allocated;       // Total allocated memory
    uint64_t total_freed;           // Total freed memory
    uint64_t allocation_count;      // Allocation count
    uint64_t deallocation_count;    // Deallocation count
    
    // Node statistics
    uint64_t active_nodes;          // Active node count
    uint64_t total_nodes_created;   // Total nodes created
    uint64_t total_nodes_destroyed; // Total nodes destroyed
} ndd_memory_stats_t;

// Operation statistics
typedef struct ndd_operation_stats_s {
    uint64_t and_operations;        // AND operation count
    uint64_t or_operations;         // OR operation count
    uint64_t not_operations;        // NOT operation count
    uint64_t diff_operations;       // DIFF operation count
    uint64_t exist_operations;      // EXIST operation count
    uint64_t encode_operations;     // Encode operation count
    uint64_t decode_operations;     // Decode operation count
    
    // Complex operations
    uint64_t complex_operations;    // Complex operation count
    uint64_t failed_operations;     // Failed operation count
} ndd_operation_stats_t;

// Cache statistics
typedef struct ndd_cache_stats_s {
    uint64_t cache_hits;            // Cache hits
    uint64_t cache_misses;          // Cache misses
    double cache_hit_rate;          // Cache hit rate
    uint64_t cache_evictions;       // Cache eviction count
    uint64_t cache_size_current;    // Current cache size
    uint64_t cache_size_peak;       // Peak cache size
} ndd_cache_stats_t;

// Parallel efficiency statistics
typedef struct ndd_parallel_stats_s {
    uint32_t worker_count;          // Worker thread count
    double parallel_efficiency;     // Parallel efficiency (0.0-1.0)
    uint64_t tasks_spawned;         // Tasks spawned
    uint64_t tasks_completed;       // Tasks completed
    double load_balance_factor;     // Load balance factor
    uint64_t thread_contention;     // Thread contention count
} ndd_parallel_stats_t;

// Comprehensive performance report
typedef struct ndd_performance_report_s {
    double collection_time_s;       // Statistics collection time
    ndd_timing_stats_t timing;
    ndd_memory_stats_t memory;
    ndd_operation_stats_t operations;
    ndd_cache_stats_t cache;
    ndd_parallel_stats_t parallel;
    
    // Comprehensive metrics
    double throughput_ops_per_sec;  // Throughput (operations/second)
    double memory_efficiency;      // Memory efficiency
    double overall_efficiency;     // Overall efficiency score
} ndd_performance_report_t;

// ============================================================================
// Core API - Designed according to "good taste" principle: simple, unified, zero special cases
// ============================================================================

// Initialization and configuration
void ndd_perf_init(const ndd_perf_config_t *config);
void ndd_perf_shutdown();
void ndd_perf_reset();
bool ndd_perf_is_enabled();

// Time measurement macros - zero overhead design
#ifdef NDD_PERF_ENABLED
    #define NDD_PERF_TIME_START(timer_name) \
        struct timespec _start_##timer_name; \
        if (ndd_perf_is_enabled()) clock_gettime(CLOCK_MONOTONIC, &_start_##timer_name)
    
    #define NDD_PERF_TIME_END(timer_name, operation_type) \
        if (ndd_perf_is_enabled()) { \
            struct timespec _end_##timer_name; \
            clock_gettime(CLOCK_MONOTONIC, &_end_##timer_name); \
            ndd_perf_record_timing(operation_type, &_start_##timer_name, &_end_##timer_name); \
        }
#else
    #define NDD_PERF_TIME_START(timer_name)
    #define NDD_PERF_TIME_END(timer_name, operation_type)
#endif

// Operation type enumeration
typedef enum ndd_perf_operation_e {
    NDD_PERF_OP_AND,
    NDD_PERF_OP_OR,
    NDD_PERF_OP_NOT,
    NDD_PERF_OP_DIFF,
    NDD_PERF_OP_EXIST,
    NDD_PERF_OP_ENCODE,
    NDD_PERF_OP_DECODE,
    NDD_PERF_OP_GC,
    NDD_PERF_OP_MEMORY_ALLOC,
    NDD_PERF_OP_MEMORY_FREE,
    NDD_PERF_OP_COUNT
} ndd_perf_operation_t;

// Recording functions - simple and direct
void ndd_perf_record_timing(ndd_perf_operation_t op, 
                           const struct timespec *start, 
                           const struct timespec *end);
void ndd_perf_record_memory_alloc(size_t bytes);
void ndd_perf_record_memory_free(size_t bytes);
void ndd_perf_record_cache_hit();
void ndd_perf_record_cache_miss();
void ndd_perf_record_operation(ndd_perf_operation_t op);

// Query functions
ndd_performance_report_t ndd_perf_get_report();
ndd_timing_stats_t ndd_perf_get_timing_stats();
ndd_memory_stats_t ndd_perf_get_memory_stats();
ndd_operation_stats_t ndd_perf_get_operation_stats();
ndd_cache_stats_t ndd_perf_get_cache_stats();
ndd_parallel_stats_t ndd_perf_get_parallel_stats();

// Report output
void ndd_perf_print_report(FILE *output);
void ndd_perf_print_summary(FILE *output);
void ndd_perf_save_report_json(const char *filename);

// Real-time monitoring
typedef void (*ndd_perf_alert_callback_t)(const char *alert_message, double value);
void ndd_perf_set_alert_threshold(ndd_perf_operation_t op, double threshold_ms);
void ndd_perf_set_alert_callback(ndd_perf_alert_callback_t callback);

// Convenience macros
#define NDD_PERF_FUNCTION_TIME(op_type) \
    NDD_PERF_TIME_START(func); \
    /* Need to call NDD_PERF_TIME_END(func, op_type) at function end */

// Default configuration
static const ndd_perf_config_t NDD_PERF_DEFAULT_CONFIG = {
    .enabled = true,
    .collect_timing = true,
    .collect_memory = true,
    .collect_operations = true,
    .collect_cache = true,
    .collect_parallel = true,
    .sample_interval_ms = 100
};

// Lightweight configuration (minimum overhead)
static const ndd_perf_config_t NDD_PERF_LIGHTWEIGHT_CONFIG = {
    .enabled = true,
    .collect_timing = true,
    .collect_memory = false,
    .collect_operations = true,
    .collect_cache = true,
    .collect_parallel = false,
    .sample_interval_ms = 1000
};

#ifdef __cplusplus
}
#endif

#endif // NDD_PERFORMANCE_MONITOR_H