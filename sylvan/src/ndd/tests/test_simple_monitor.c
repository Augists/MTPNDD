// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include "ndd_performance_monitor.h"
#include <stdio.h>

int main() {
    printf("🚀 简单性能监控测试开始\n");
    
    // 初始化性能监控
    printf("1. 初始化性能监控系统...\n");
    ndd_perf_config_t config = {
        .enabled = true,
        .collect_timing = true,
        .collect_memory = true,
        .collect_operations = true,
        .collect_cache = true,
        .collect_parallel = false,
        .sample_interval_ms = 100
    };
    ndd_perf_init(&config);
    
    if (ndd_perf_is_enabled()) {
        printf("✅ 性能监控系统启用成功\n");
    } else {
        printf("❌ 性能监控系统未启用\n");
        return 1;
    }
    
    // 记录一些测试数据
    printf("2. 记录测试数据...\n");
    ndd_perf_record_memory_alloc(1024);
    ndd_perf_record_operation(NDD_PERF_OP_AND);
    ndd_perf_record_cache_hit();
    ndd_perf_record_cache_miss();
    
    // 获取统计信息
    printf("3. 获取统计信息...\n");
    ndd_memory_stats_t memory = ndd_perf_get_memory_stats();
    ndd_operation_stats_t ops = ndd_perf_get_operation_stats();
    ndd_cache_stats_t cache = ndd_perf_get_cache_stats();
    
    printf("内存统计:\n");
    printf("  当前分配: %.1f KB\n", memory.current_allocated / 1024.0);
    printf("  分配次数: %lu\n", memory.allocation_count);
    
    printf("操作统计:\n");
    printf("  AND操作: %lu\n", ops.and_operations);
    
    printf("缓存统计:\n");
    printf("  缓存命中: %lu\n", cache.cache_hits);
    printf("  缓存未命中: %lu\n", cache.cache_misses);
    printf("  命中率: %.1f%%\n", cache.cache_hit_rate * 100.0);
    
    // 打印摘要报告
    printf("4. 生成性能报告...\n");
    ndd_perf_print_summary(stdout);
    
    // 清理
    ndd_perf_shutdown();
    printf("✅ 简单性能监控测试完成\n");
    
    return 0;
}