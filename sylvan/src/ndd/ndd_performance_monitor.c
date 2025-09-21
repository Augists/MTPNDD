// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd_performance_monitor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

// 全局性能监控状态
static struct {
    bool initialized;
    ndd_perf_config_t config;
    pthread_mutex_t mutex;
    
    // 各类统计数据
    ndd_timing_stats_t timing;
    ndd_memory_stats_t memory;
    ndd_operation_stats_t operations;
    ndd_cache_stats_t cache;
    ndd_parallel_stats_t parallel;
    
    // 实时监控
    ndd_perf_alert_callback_t alert_callback;
    double alert_thresholds[NDD_PERF_OP_COUNT];
    
    // 元数据
    struct timespec start_time;
    uint64_t total_samples;
} g_perf_state = {0};

// ============================================================================
// 工具函数 - "好品味"：没有重复代码
// ============================================================================

static double timespec_diff_ns(const struct timespec *start, const struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1e9 + (end->tv_nsec - start->tv_nsec);
}

static void update_timing_stats(double time_ns, ndd_timing_stats_t *stats) {
    stats->total_operations++;
    stats->total_time_ns += time_ns;
    
    if (stats->total_operations == 1) {
        stats->min_time_ns = stats->max_time_ns = time_ns;
    } else {
        if (time_ns < stats->min_time_ns) stats->min_time_ns = time_ns;
        if (time_ns > stats->max_time_ns) stats->max_time_ns = time_ns;
    }
    
    stats->avg_time_ns = stats->total_time_ns / stats->total_operations;
}

static void check_alert_threshold(ndd_perf_operation_t op, double time_ms) {
    if (g_perf_state.alert_callback && 
        g_perf_state.alert_thresholds[op] > 0 && 
        time_ms > g_perf_state.alert_thresholds[op]) {
        
        char alert_msg[256];
        const char *op_names[] = {
            "AND", "OR", "NOT", "DIFF", "EXIST", 
            "ENCODE", "DECODE", "GC", "MEMORY_ALLOC", "MEMORY_FREE"
        };
        
        snprintf(alert_msg, sizeof(alert_msg), 
                "Performance alert: %s operation took %.2f ms (threshold: %.2f ms)",
                op_names[op], time_ms, g_perf_state.alert_thresholds[op]);
        
        g_perf_state.alert_callback(alert_msg, time_ms);
    }
}

// ============================================================================
// 核心API实现 - "好品味"：每个函数只做一件事
// ============================================================================

void ndd_perf_init(const ndd_perf_config_t *config) {
    if (g_perf_state.initialized) {
        return; // 已初始化，避免重复
    }
    
    // 复制配置
    if (config) {
        g_perf_state.config = *config;
    } else {
        g_perf_state.config = NDD_PERF_DEFAULT_CONFIG;
    }
    
    // 初始化互斥锁
    pthread_mutex_init(&g_perf_state.mutex, NULL);
    
    // 清零所有统计数据
    memset(&g_perf_state.timing, 0, sizeof(g_perf_state.timing));
    memset(&g_perf_state.memory, 0, sizeof(g_perf_state.memory));
    memset(&g_perf_state.operations, 0, sizeof(g_perf_state.operations));
    memset(&g_perf_state.cache, 0, sizeof(g_perf_state.cache));
    memset(&g_perf_state.parallel, 0, sizeof(g_perf_state.parallel));
    
    // 设置初始值
    g_perf_state.timing.min_time_ns = INFINITY;
    clock_gettime(CLOCK_MONOTONIC, &g_perf_state.start_time);
    
    g_perf_state.initialized = true;
}

void ndd_perf_shutdown() {
    if (!g_perf_state.initialized) {
        return;
    }
    
    pthread_mutex_destroy(&g_perf_state.mutex);
    g_perf_state.initialized = false;
}

void ndd_perf_reset() {
    if (!g_perf_state.initialized || !g_perf_state.config.enabled) {
        return;
    }
    
    pthread_mutex_lock(&g_perf_state.mutex);
    
    // 保存配置和初始化状态
    ndd_perf_config_t saved_config = g_perf_state.config;
    bool was_initialized = g_perf_state.initialized;
    
    // 清零统计数据
    memset(&g_perf_state.timing, 0, sizeof(g_perf_state.timing));
    memset(&g_perf_state.memory, 0, sizeof(g_perf_state.memory));
    memset(&g_perf_state.operations, 0, sizeof(g_perf_state.operations));
    memset(&g_perf_state.cache, 0, sizeof(g_perf_state.cache));
    memset(&g_perf_state.parallel, 0, sizeof(g_perf_state.parallel));
    
    // 恢复配置
    g_perf_state.config = saved_config;
    g_perf_state.initialized = was_initialized;
    g_perf_state.timing.min_time_ns = INFINITY;
    clock_gettime(CLOCK_MONOTONIC, &g_perf_state.start_time);
    g_perf_state.total_samples = 0;
    
    pthread_mutex_unlock(&g_perf_state.mutex);
}

bool ndd_perf_is_enabled() {
    return g_perf_state.initialized && g_perf_state.config.enabled;
}

// ============================================================================
// 记录函数 - "好品味"：统一的模式，无特殊情况
// ============================================================================

void ndd_perf_record_timing(ndd_perf_operation_t op, 
                           const struct timespec *start, 
                           const struct timespec *end) {
    if (!ndd_perf_is_enabled() || !g_perf_state.config.collect_timing) {
        return;
    }
    
    double time_ns = timespec_diff_ns(start, end);
    double time_ms = time_ns / 1e6;
    
    pthread_mutex_lock(&g_perf_state.mutex);
    
    // 更新总体时间统计
    update_timing_stats(time_ns, &g_perf_state.timing);
    
    // 更新特定操作时间统计
    switch (op) {
        case NDD_PERF_OP_AND:
            g_perf_state.timing.and_time_ns += time_ns;
            break;
        case NDD_PERF_OP_OR:
            g_perf_state.timing.or_time_ns += time_ns;
            break;
        case NDD_PERF_OP_NOT:
            g_perf_state.timing.not_time_ns += time_ns;
            break;
        case NDD_PERF_OP_ENCODE:
            g_perf_state.timing.encode_time_ns += time_ns;
            break;
        case NDD_PERF_OP_GC:
            g_perf_state.timing.gc_time_ns += time_ns;
            break;
        default:
            break;
    }
    
    g_perf_state.total_samples++;
    pthread_mutex_unlock(&g_perf_state.mutex);
    
    // 检查告警阈值
    check_alert_threshold(op, time_ms);
}

void ndd_perf_record_memory_alloc(size_t bytes) {
    if (!ndd_perf_is_enabled() || !g_perf_state.config.collect_memory) {
        return;
    }
    
    pthread_mutex_lock(&g_perf_state.mutex);
    
    g_perf_state.memory.current_allocated += bytes;
    g_perf_state.memory.total_allocated += bytes;
    g_perf_state.memory.allocation_count++;
    
    if (g_perf_state.memory.current_allocated > g_perf_state.memory.peak_allocated) {
        g_perf_state.memory.peak_allocated = g_perf_state.memory.current_allocated;
    }
    
    pthread_mutex_unlock(&g_perf_state.mutex);
}

void ndd_perf_record_memory_free(size_t bytes) {
    if (!ndd_perf_is_enabled() || !g_perf_state.config.collect_memory) {
        return;
    }
    
    pthread_mutex_lock(&g_perf_state.mutex);
    
    if (g_perf_state.memory.current_allocated >= bytes) {
        g_perf_state.memory.current_allocated -= bytes;
    }
    g_perf_state.memory.total_freed += bytes;
    g_perf_state.memory.deallocation_count++;
    
    pthread_mutex_unlock(&g_perf_state.mutex);
}

void ndd_perf_record_cache_hit() {
    if (!ndd_perf_is_enabled() || !g_perf_state.config.collect_cache) {
        return;
    }
    
    pthread_mutex_lock(&g_perf_state.mutex);
    g_perf_state.cache.cache_hits++;
    
    uint64_t total = g_perf_state.cache.cache_hits + g_perf_state.cache.cache_misses;
    if (total > 0) {
        g_perf_state.cache.cache_hit_rate = (double)g_perf_state.cache.cache_hits / total;
    }
    
    pthread_mutex_unlock(&g_perf_state.mutex);
}

void ndd_perf_record_cache_miss() {
    if (!ndd_perf_is_enabled() || !g_perf_state.config.collect_cache) {
        return;
    }
    
    pthread_mutex_lock(&g_perf_state.mutex);
    g_perf_state.cache.cache_misses++;
    
    uint64_t total = g_perf_state.cache.cache_hits + g_perf_state.cache.cache_misses;
    if (total > 0) {
        g_perf_state.cache.cache_hit_rate = (double)g_perf_state.cache.cache_hits / total;
    }
    
    pthread_mutex_unlock(&g_perf_state.mutex);
}

void ndd_perf_record_operation(ndd_perf_operation_t op) {
    if (!ndd_perf_is_enabled() || !g_perf_state.config.collect_operations) {
        return;
    }
    
    pthread_mutex_lock(&g_perf_state.mutex);
    
    switch (op) {
        case NDD_PERF_OP_AND:
            g_perf_state.operations.and_operations++;
            break;
        case NDD_PERF_OP_OR:
            g_perf_state.operations.or_operations++;
            break;
        case NDD_PERF_OP_NOT:
            g_perf_state.operations.not_operations++;
            break;
        case NDD_PERF_OP_DIFF:
            g_perf_state.operations.diff_operations++;
            break;
        case NDD_PERF_OP_EXIST:
            g_perf_state.operations.exist_operations++;
            break;
        case NDD_PERF_OP_ENCODE:
            g_perf_state.operations.encode_operations++;
            break;
        case NDD_PERF_OP_DECODE:
            g_perf_state.operations.decode_operations++;
            break;
        default:
            break;
    }
    
    pthread_mutex_unlock(&g_perf_state.mutex);
}

// ============================================================================
// 查询函数 - "好品味"：只返回数据，不做复杂计算
// ============================================================================

ndd_performance_report_t ndd_perf_get_report() {
    ndd_performance_report_t report = {0};
    
    if (!ndd_perf_is_enabled()) {
        return report;
    }
    
    pthread_mutex_lock(&g_perf_state.mutex);
    
    // 复制基础统计数据
    report.timing = g_perf_state.timing;
    report.memory = g_perf_state.memory;
    report.operations = g_perf_state.operations;
    report.cache = g_perf_state.cache;
    report.parallel = g_perf_state.parallel;
    
    // 计算综合指标
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    report.collection_time_s = timespec_diff_ns(&g_perf_state.start_time, &current_time) / 1e9;
    
    if (report.collection_time_s > 0 && report.timing.total_operations > 0) {
        report.throughput_ops_per_sec = report.timing.total_operations / report.collection_time_s;
        
        // 内存效率：操作数 / 内存使用量
        if (report.memory.peak_allocated > 0) {
            report.memory_efficiency = report.timing.total_operations / 
                                     (double)(report.memory.peak_allocated / 1024.0); // ops per KB
        }
        
        // 总体效率评分（0-100）
        double cache_efficiency = report.cache.cache_hit_rate;
        double memory_waste = report.memory.peak_allocated > 0 ? 
                             (double)report.memory.current_allocated / report.memory.peak_allocated : 1.0;
        
        report.overall_efficiency = (cache_efficiency * 0.4 + memory_waste * 0.3 + 0.3) * 100.0;
    }
    
    pthread_mutex_unlock(&g_perf_state.mutex);
    
    return report;
}

ndd_timing_stats_t ndd_perf_get_timing_stats() {
    ndd_timing_stats_t stats = {0};
    if (ndd_perf_is_enabled()) {
        pthread_mutex_lock(&g_perf_state.mutex);
        stats = g_perf_state.timing;
        pthread_mutex_unlock(&g_perf_state.mutex);
    }
    return stats;
}

ndd_memory_stats_t ndd_perf_get_memory_stats() {
    ndd_memory_stats_t stats = {0};
    if (ndd_perf_is_enabled()) {
        pthread_mutex_lock(&g_perf_state.mutex);
        stats = g_perf_state.memory;
        pthread_mutex_unlock(&g_perf_state.mutex);
    }
    return stats;
}

ndd_operation_stats_t ndd_perf_get_operation_stats() {
    ndd_operation_stats_t stats = {0};
    if (ndd_perf_is_enabled()) {
        pthread_mutex_lock(&g_perf_state.mutex);
        stats = g_perf_state.operations;
        pthread_mutex_unlock(&g_perf_state.mutex);
    }
    return stats;
}

ndd_cache_stats_t ndd_perf_get_cache_stats() {
    ndd_cache_stats_t stats = {0};
    if (ndd_perf_is_enabled()) {
        pthread_mutex_lock(&g_perf_state.mutex);
        stats = g_perf_state.cache;
        pthread_mutex_unlock(&g_perf_state.mutex);
    }
    return stats;
}

ndd_parallel_stats_t ndd_perf_get_parallel_stats() {
    ndd_parallel_stats_t stats = {0};
    if (ndd_perf_is_enabled()) {
        pthread_mutex_lock(&g_perf_state.mutex);
        stats = g_perf_state.parallel;
        pthread_mutex_unlock(&g_perf_state.mutex);
    }
    return stats;
}

// ============================================================================
// 输出函数 - "好品味"：简洁的表格格式
// ============================================================================

void ndd_perf_print_summary(FILE *output) {
    if (!output) output = stdout;
    
    ndd_performance_report_t report = ndd_perf_get_report();
    
    fprintf(output, "\n🚀 MTPNDD 性能监控摘要\n");
    fprintf(output, "==================================================\n");
    fprintf(output, "运行时间:       %.2f 秒\n", report.collection_time_s);
    fprintf(output, "总操作数:       %lu\n", report.timing.total_operations);
    fprintf(output, "吞吐量:         %.0f ops/sec\n", report.throughput_ops_per_sec);
    fprintf(output, "平均延迟:       %.3f ms\n", report.timing.avg_time_ns / 1e6);
    fprintf(output, "缓存命中率:     %.1f%%\n", report.cache.cache_hit_rate * 100.0);
    fprintf(output, "内存使用:       %.1f KB (峰值: %.1f KB)\n", 
            report.memory.current_allocated / 1024.0,
            report.memory.peak_allocated / 1024.0);
    fprintf(output, "总体效率:       %.1f/100\n", report.overall_efficiency);
    fprintf(output, "==================================================\n\n");
}

void ndd_perf_print_report(FILE *output) {
    if (!output) output = stdout;
    
    ndd_performance_report_t report = ndd_perf_get_report();
    
    fprintf(output, "\n📊 MTPNDD 详细性能报告\n");
    fprintf(output, "================================================================\n");
    
    // 时间统计
    fprintf(output, "\n⏱️  时间统计:\n");
    fprintf(output, "   总操作数:      %lu\n", report.timing.total_operations);
    fprintf(output, "   总时间:        %.2f ms\n", report.timing.total_time_ns / 1e6);
    fprintf(output, "   平均时间:      %.3f ms\n", report.timing.avg_time_ns / 1e6);
    fprintf(output, "   最小时间:      %.3f ms\n", report.timing.min_time_ns / 1e6);
    fprintf(output, "   最大时间:      %.3f ms\n", report.timing.max_time_ns / 1e6);
    
    // 操作分类统计
    fprintf(output, "\n🔧 操作统计:\n");
    fprintf(output, "   AND操作:       %lu (%.1f ms)\n", 
            report.operations.and_operations, report.timing.and_time_ns / 1e6);
    fprintf(output, "   OR操作:        %lu (%.1f ms)\n", 
            report.operations.or_operations, report.timing.or_time_ns / 1e6);
    fprintf(output, "   NOT操作:       %lu (%.1f ms)\n", 
            report.operations.not_operations, report.timing.not_time_ns / 1e6);
    fprintf(output, "   编码操作:      %lu (%.1f ms)\n", 
            report.operations.encode_operations, report.timing.encode_time_ns / 1e6);
    
    // 内存统计
    fprintf(output, "\n💾 内存统计:\n");
    fprintf(output, "   当前分配:      %.1f KB\n", report.memory.current_allocated / 1024.0);
    fprintf(output, "   峰值分配:      %.1f KB\n", report.memory.peak_allocated / 1024.0);
    fprintf(output, "   总分配:        %.1f KB\n", report.memory.total_allocated / 1024.0);
    fprintf(output, "   分配次数:      %lu\n", report.memory.allocation_count);
    
    // 缓存统计
    fprintf(output, "\n🗄️  缓存统计:\n");
    fprintf(output, "   缓存命中:      %lu\n", report.cache.cache_hits);
    fprintf(output, "   缓存未命中:    %lu\n", report.cache.cache_misses);
    fprintf(output, "   命中率:        %.1f%%\n", report.cache.cache_hit_rate * 100.0);
    
    // 综合评估
    fprintf(output, "\n📈 性能评估:\n");
    fprintf(output, "   吞吐量:        %.0f ops/sec\n", report.throughput_ops_per_sec);
    fprintf(output, "   内存效率:      %.1f ops/KB\n", report.memory_efficiency);
    fprintf(output, "   总体评分:      %.1f/100\n", report.overall_efficiency);
    
    fprintf(output, "================================================================\n\n");
}

// ============================================================================
// 实时监控功能
// ============================================================================

void ndd_perf_set_alert_threshold(ndd_perf_operation_t op, double threshold_ms) {
    if (op < NDD_PERF_OP_COUNT) {
        g_perf_state.alert_thresholds[op] = threshold_ms;
    }
}

void ndd_perf_set_alert_callback(ndd_perf_alert_callback_t callback) {
    g_perf_state.alert_callback = callback;
}

void ndd_perf_save_report_json(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) return;
    
    ndd_performance_report_t report = ndd_perf_get_report();
    
    fprintf(file, "{\n");
    fprintf(file, "  \"collection_time_s\": %.2f,\n", report.collection_time_s);
    fprintf(file, "  \"throughput_ops_per_sec\": %.0f,\n", report.throughput_ops_per_sec);
    fprintf(file, "  \"overall_efficiency\": %.1f,\n", report.overall_efficiency);
    fprintf(file, "  \"timing\": {\n");
    fprintf(file, "    \"total_operations\": %lu,\n", report.timing.total_operations);
    fprintf(file, "    \"avg_time_ms\": %.3f\n", report.timing.avg_time_ns / 1e6);
    fprintf(file, "  },\n");
    fprintf(file, "  \"cache\": {\n");
    fprintf(file, "    \"hit_rate\": %.3f,\n", report.cache.cache_hit_rate);
    fprintf(file, "    \"hits\": %lu,\n", report.cache.cache_hits);
    fprintf(file, "    \"misses\": %lu\n", report.cache.cache_misses);
    fprintf(file, "  }\n");
    fprintf(file, "}\n");
    
    fclose(file);
}