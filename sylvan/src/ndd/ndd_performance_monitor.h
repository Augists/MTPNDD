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

// 性能监控配置
typedef struct ndd_perf_config_s {
    bool enabled;                    // 全局开关
    bool collect_timing;            // 收集时间统计
    bool collect_memory;            // 收集内存统计  
    bool collect_operations;        // 收集操作统计
    bool collect_cache;             // 收集缓存统计
    bool collect_parallel;          // 收集并行效率统计
    uint32_t sample_interval_ms;    // 采样间隔（毫秒）
} ndd_perf_config_t;

// 时间统计
typedef struct ndd_timing_stats_s {
    uint64_t total_operations;      // 总操作数
    double total_time_ns;           // 总时间（纳秒）
    double min_time_ns;             // 最小操作时间
    double max_time_ns;             // 最大操作时间
    double avg_time_ns;             // 平均操作时间
    
    // 分类操作时间
    double and_time_ns;
    double or_time_ns;
    double not_time_ns;
    double encode_time_ns;
    double gc_time_ns;
} ndd_timing_stats_t;

// 内存统计
typedef struct ndd_memory_stats_s {
    uint64_t current_allocated;     // 当前分配内存
    uint64_t peak_allocated;        // 峰值分配内存
    uint64_t total_allocated;       // 总分配内存
    uint64_t total_freed;           // 总释放内存
    uint64_t allocation_count;      // 分配次数
    uint64_t deallocation_count;    // 释放次数
    
    // 节点统计
    uint64_t active_nodes;          // 活跃节点数
    uint64_t total_nodes_created;   // 总创建节点数
    uint64_t total_nodes_destroyed; // 总销毁节点数
} ndd_memory_stats_t;

// 操作统计
typedef struct ndd_operation_stats_s {
    uint64_t and_operations;        // AND操作次数
    uint64_t or_operations;         // OR操作次数
    uint64_t not_operations;        // NOT操作次数
    uint64_t diff_operations;       // DIFF操作次数
    uint64_t exist_operations;      // EXIST操作次数
    uint64_t encode_operations;     // 编码操作次数
    uint64_t decode_operations;     // 解码操作次数
    
    // 复合操作
    uint64_t complex_operations;    // 复合操作次数
    uint64_t failed_operations;     // 失败操作次数
} ndd_operation_stats_t;

// 缓存统计
typedef struct ndd_cache_stats_s {
    uint64_t cache_hits;            // 缓存命中
    uint64_t cache_misses;          // 缓存未命中
    double cache_hit_rate;          // 缓存命中率
    uint64_t cache_evictions;       // 缓存驱逐次数
    uint64_t cache_size_current;    // 当前缓存大小
    uint64_t cache_size_peak;       // 峰值缓存大小
} ndd_cache_stats_t;

// 并行效率统计
typedef struct ndd_parallel_stats_s {
    uint32_t worker_count;          // 工作线程数
    double parallel_efficiency;     // 并行效率（0.0-1.0）
    uint64_t tasks_spawned;         // 生成的任务数
    uint64_t tasks_completed;       // 完成的任务数
    double load_balance_factor;     // 负载均衡因子
    uint64_t thread_contention;     // 线程竞争次数
} ndd_parallel_stats_t;

// 综合性能报告
typedef struct ndd_performance_report_s {
    double collection_time_s;       // 统计收集时间
    ndd_timing_stats_t timing;
    ndd_memory_stats_t memory;
    ndd_operation_stats_t operations;
    ndd_cache_stats_t cache;
    ndd_parallel_stats_t parallel;
    
    // 综合指标
    double throughput_ops_per_sec;  // 吞吐量（操作/秒）
    double memory_efficiency;      // 内存效率
    double overall_efficiency;     // 总体效率评分
} ndd_performance_report_t;

// ============================================================================
// 核心API - 按照"好品味"原则设计：简单、统一、零特殊情况
// ============================================================================

// 初始化和配置
void ndd_perf_init(const ndd_perf_config_t *config);
void ndd_perf_shutdown();
void ndd_perf_reset();
bool ndd_perf_is_enabled();

// 时间测量宏 - 零开销设计
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

// 操作类型枚举
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

// 记录函数 - 简单直接
void ndd_perf_record_timing(ndd_perf_operation_t op, 
                           const struct timespec *start, 
                           const struct timespec *end);
void ndd_perf_record_memory_alloc(size_t bytes);
void ndd_perf_record_memory_free(size_t bytes);
void ndd_perf_record_cache_hit();
void ndd_perf_record_cache_miss();
void ndd_perf_record_operation(ndd_perf_operation_t op);

// 查询函数
ndd_performance_report_t ndd_perf_get_report();
ndd_timing_stats_t ndd_perf_get_timing_stats();
ndd_memory_stats_t ndd_perf_get_memory_stats();
ndd_operation_stats_t ndd_perf_get_operation_stats();
ndd_cache_stats_t ndd_perf_get_cache_stats();
ndd_parallel_stats_t ndd_perf_get_parallel_stats();

// 报告输出
void ndd_perf_print_report(FILE *output);
void ndd_perf_print_summary(FILE *output);
void ndd_perf_save_report_json(const char *filename);

// 实时监控
typedef void (*ndd_perf_alert_callback_t)(const char *alert_message, double value);
void ndd_perf_set_alert_threshold(ndd_perf_operation_t op, double threshold_ms);
void ndd_perf_set_alert_callback(ndd_perf_alert_callback_t callback);

// 便捷宏
#define NDD_PERF_FUNCTION_TIME(op_type) \
    NDD_PERF_TIME_START(func); \
    /* 在函数结尾处需要调用 NDD_PERF_TIME_END(func, op_type) */

// 默认配置
static const ndd_perf_config_t NDD_PERF_DEFAULT_CONFIG = {
    .enabled = true,
    .collect_timing = true,
    .collect_memory = true,
    .collect_operations = true,
    .collect_cache = true,
    .collect_parallel = true,
    .sample_interval_ms = 100
};

// 轻量级配置（最小开销）
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