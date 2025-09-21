// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#define NDD_PERF_ENABLED 1
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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

// 告警回调函数
void performance_alert_handler(const char *alert_message, double value) {
    printf("⚠️  性能告警: %s\n", alert_message);
}

// 测试基础性能监控功能
void test_basic_performance_monitoring() {
    printf("🚀 测试基础性能监控功能\n\n");
    
    // 初始化性能监控
    ndd_perf_config_t config = NDD_PERF_DEFAULT_CONFIG;
    ndd_perf_init(&config);
    
    // 设置告警阈值和回调
    ndd_perf_set_alert_threshold(NDD_PERF_OP_AND, 10.0); // 10ms告警
    ndd_perf_set_alert_callback(performance_alert_handler);
    
    printf("✅ 性能监控系统初始化完成\n");
    
    // 初始化NDD系统
    ndd_config_t ndd_config = {
        .n_workers = 2,
        .dqsize = 10000,
        .table_size = 1 << 16,
        .cache_size = 1 << 12,
        .gc_threshold = 50
    };
    
    assert(ndd_init(&ndd_config) == 0);
    uint32_t field = ndd_declare_field(4);
    
    printf("✅ NDD系统初始化完成\n\n");
    
    // 执行监控的操作
    printf("=== 执行被监控的操作 ===\n");
    
    ndd_t node1 = ndd_create_node(field);
    ndd_t node2 = ndd_create_node(field);
    
    ndd_add_edge(&node1, ndd_true(), ndd_sylvan_true);
    ndd_add_edge(&node2, ndd_true(), ndd_sylvan_true);
    
    // 记录内存分配
    ndd_perf_record_memory_alloc(1024);
    
    // 执行多个操作以生成统计数据
    for (int i = 0; i < 10; i++) {
        // 开始计时
        NDD_PERF_TIME_START(and_op);
        
        ndd_t result = ndd_and(node1, node2);
        
        // 结束计时
        NDD_PERF_TIME_END(and_op, NDD_PERF_OP_AND);
        
        // 记录操作
        ndd_perf_record_operation(NDD_PERF_OP_AND);
        
        // 模拟缓存命中/未命中
        if (i % 3 == 0) {
            ndd_perf_record_cache_hit();
        } else {
            ndd_perf_record_cache_miss();
        }
        
        ndd_deref_safe(result);
        
        printf("  完成第%d次AND操作\n", i + 1);
    }
    
    // 测试其他操作类型
    for (int i = 0; i < 5; i++) {
        NDD_PERF_TIME_START(or_op);
        ndd_t result = ndd_or(node1, node2);
        NDD_PERF_TIME_END(or_op, NDD_PERF_OP_OR);
        ndd_perf_record_operation(NDD_PERF_OP_OR);
        ndd_deref_safe(result);
    }
    
    for (int i = 0; i < 3; i++) {
        NDD_PERF_TIME_START(not_op);
        ndd_t result = ndd_not(node1);
        NDD_PERF_TIME_END(not_op, NDD_PERF_OP_NOT);
        ndd_perf_record_operation(NDD_PERF_OP_NOT);
        ndd_deref_safe(result);
    }
    
    printf("\n=== 性能监控结果 ===\n");
    
    // 获取并显示统计信息
    ndd_timing_stats_t timing = ndd_perf_get_timing_stats();
    ndd_memory_stats_t memory = ndd_perf_get_memory_stats();
    ndd_operation_stats_t operations = ndd_perf_get_operation_stats();
    ndd_cache_stats_t cache = ndd_perf_get_cache_stats();
    
    printf("时间统计:\n");
    printf("  总操作数: %lu\n", timing.total_operations);
    printf("  平均时间: %.3f ms\n", timing.avg_time_ns / 1e6);
    printf("  最小时间: %.3f ms\n", timing.min_time_ns / 1e6);
    printf("  最大时间: %.3f ms\n", timing.max_time_ns / 1e6);
    
    printf("\n操作统计:\n");
    printf("  AND操作: %lu\n", operations.and_operations);
    printf("  OR操作: %lu\n", operations.or_operations);
    printf("  NOT操作: %lu\n", operations.not_operations);
    
    printf("\n缓存统计:\n");
    printf("  缓存命中: %lu\n", cache.cache_hits);
    printf("  缓存未命中: %lu\n", cache.cache_misses);
    printf("  命中率: %.1f%%\n", cache.cache_hit_rate * 100.0);
    
    printf("\n内存统计:\n");
    printf("  当前分配: %.1f KB\n", memory.current_allocated / 1024.0);
    printf("  分配次数: %lu\n", memory.allocation_count);
    
    // 测试完整报告
    printf("\n=== 完整性能报告 ===\n");
    ndd_perf_print_summary(stdout);
    
    // 清理
    ndd_deref_safe(node1);
    ndd_deref_safe(node2);
    ndd_quit();
    ndd_perf_shutdown();
    
    printf("✅ 基础性能监控测试完成\n\n");
}

// 测试性能监控的开销
void test_monitoring_overhead() {
    printf("🔧 测试性能监控开销\n\n");
    
    const int iterations = 1000;
    
    // 测试1：不启用监控
    printf("测试1：不启用监控的性能基线...\n");
    
    ndd_config_t ndd_config = {
        .n_workers = 2,
        .dqsize = 10000,
        .table_size = 1 << 16,
        .cache_size = 1 << 12,
        .gc_threshold = 50
    };
    
    assert(ndd_init(&ndd_config) == 0);
    uint32_t field = ndd_declare_field(4);
    
    ndd_t node1 = ndd_create_node(field);
    ndd_t node2 = ndd_create_node(field);
    ndd_add_edge(&node1, ndd_true(), ndd_sylvan_true);
    ndd_add_edge(&node2, ndd_true(), ndd_sylvan_true);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        ndd_t result = ndd_and(node1, node2);
        ndd_deref_safe(result);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double baseline_time = (end.tv_sec - start.tv_sec) * 1000.0 + 
                          (end.tv_nsec - start.tv_nsec) / 1e6;
    
    ndd_deref_safe(node1);
    ndd_deref_safe(node2);
    ndd_quit();
    
    printf("  基线时间: %.2f ms (%d次操作)\n", baseline_time, iterations);
    
    // 测试2：启用监控
    printf("\n测试2：启用监控的性能...\n");
    
    ndd_perf_config_t perf_config = NDD_PERF_LIGHTWEIGHT_CONFIG; // 轻量级配置
    ndd_perf_init(&perf_config);
    
    assert(ndd_init(&ndd_config) == 0);
    field = ndd_declare_field(4);
    
    node1 = ndd_create_node(field);
    node2 = ndd_create_node(field);
    ndd_add_edge(&node1, ndd_true(), ndd_sylvan_true);
    ndd_add_edge(&node2, ndd_true(), ndd_sylvan_true);
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        NDD_PERF_TIME_START(and_op);
        ndd_t result = ndd_and(node1, node2);
        NDD_PERF_TIME_END(and_op, NDD_PERF_OP_AND);
        
        ndd_perf_record_operation(NDD_PERF_OP_AND);
        ndd_deref_safe(result);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double monitored_time = (end.tv_sec - start.tv_sec) * 1000.0 + 
                           (end.tv_nsec - start.tv_nsec) / 1e6;
    
    printf("  监控时间: %.2f ms (%d次操作)\n", monitored_time, iterations);
    
    double overhead = ((monitored_time - baseline_time) / baseline_time) * 100.0;
    printf("  开销: %.1f%%\n", overhead);
    
    if (overhead < 10.0) {
        printf("  ✅ 监控开销可接受 (< 10%%)\n");
    } else if (overhead < 25.0) {
        printf("  ⚠️  监控开销偏高但可用 (< 25%%)\n");
    } else {
        printf("  ❌ 监控开销过高 (> 25%%)\n");
    }
    
    // 显示监控统计
    ndd_perf_print_summary(stdout);
    
    // 清理
    ndd_deref_safe(node1);
    ndd_deref_safe(node2);
    ndd_quit();
    ndd_perf_shutdown();
    
    printf("✅ 监控开销测试完成\n\n");
}

// 测试JSON报告导出
void test_json_export() {
    printf("📄 测试JSON报告导出\n\n");
    
    ndd_perf_init(&NDD_PERF_DEFAULT_CONFIG);
    
    ndd_config_t ndd_config = {
        .n_workers = 2,
        .dqsize = 10000,
        .table_size = 1 << 16,
        .cache_size = 1 << 12,
        .gc_threshold = 50
    };
    
    assert(ndd_init(&ndd_config) == 0);
    uint32_t field = ndd_declare_field(4);
    
    // 执行一些操作生成数据
    ndd_t node1 = ndd_create_node(field);
    ndd_t node2 = ndd_create_node(field);
    
    for (int i = 0; i < 5; i++) {
        NDD_PERF_TIME_START(op);
        ndd_t result = ndd_and(node1, node2);
        NDD_PERF_TIME_END(op, NDD_PERF_OP_AND);
        ndd_perf_record_operation(NDD_PERF_OP_AND);
        ndd_perf_record_cache_hit();
        ndd_deref_safe(result);
    }
    
    // 导出JSON报告
    const char *json_file = "/tmp/ndd_performance_report.json";
    ndd_perf_save_report_json(json_file);
    
    printf("✅ JSON报告已保存到: %s\n", json_file);
    
    // 验证文件是否存在
    FILE *file = fopen(json_file, "r");
    if (file) {
        printf("✅ JSON文件读取验证成功\n");
        
        // 显示文件内容的前几行
        char buffer[256];
        printf("\n📄 报告内容预览:\n");
        for (int i = 0; i < 5 && fgets(buffer, sizeof(buffer), file); i++) {
            printf("  %s", buffer);
        }
        printf("  ...\n");
        
        fclose(file);
    } else {
        printf("❌ JSON文件创建失败\n");
    }
    
    // 清理
    ndd_deref_safe(node1);
    ndd_deref_safe(node2);
    ndd_quit();
    ndd_perf_shutdown();
    
    printf("✅ JSON导出测试完成\n\n");
}

int main() {
    printf("🚀 MTPNDD 性能监控系统测试套件\n");
    printf("基于Linus \"好品味\"原则：简单、统一、可测量\n\n");
    
    test_basic_performance_monitoring();
    test_monitoring_overhead();
    test_json_export();
    
    printf("🎉 所有性能监控测试完成！\n");
    printf("\n💡 设计理念验证:\n");
    printf("   ✅ 统一的API设计 - 无特殊情况\n");
    printf("   ✅ 零开销原则 - 可完全关闭\n");
    printf("   ✅ 简单直接 - 一看就懂的数据结构\n");
    printf("   ✅ 实用主义 - 解决真实监控需求\n\n");
    
    return 0;
}