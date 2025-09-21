// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include "ndd_performance_monitor.h"
#include <stdio.h>
#include <assert.h>

int main() {
    printf("🔧 调试版本性能监控测试\n");
    
    // 1. 先只测试性能监控系统
    printf("1. 测试纯性能监控系统...\n");
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
    printf("   ✅ 性能监控系统初始化成功\n");
    
    // 记录一些数据
    ndd_perf_record_memory_alloc(100);
    ndd_perf_record_operation(NDD_PERF_OP_AND);
    ndd_perf_record_cache_hit();
    printf("   ✅ 性能数据记录成功\n");
    
    // 获取统计
    ndd_memory_stats_t memory = ndd_perf_get_memory_stats();
    printf("   内存分配: %.1f KB\n", memory.current_allocated / 1024.0);
    
    ndd_perf_shutdown();
    printf("   ✅ 性能监控系统关闭成功\n");
    
    // 2. 然后测试NDD系统
    printf("\n2. 测试纯NDD系统...\n");
    ndd_config_t ndd_config = {
        .n_workers = 2,
        .dqsize = 10000,
        .table_size = 1 << 16,
        .cache_size = 1 << 12,
        .gc_threshold = 50
    };
    
    int init_result = ndd_init(&ndd_config);
    printf("   NDD初始化结果: %d\n", init_result);
    if (init_result != 0) {
        printf("   ❌ NDD初始化失败\n");
        return 1;
    }
    
    uint32_t field = ndd_declare_field(4);
    printf("   声明字段: %u\n", field);
    
    ndd_t node1 = ndd_create_node(field);
    printf("   创建节点1: %p\n", (void*)node1.node);
    
    ndd_t node2 = ndd_create_node(field);
    printf("   创建节点2: %p\n", (void*)node2.node);
    
    printf("   尝试添加边...\n");
    ndd_add_edge(&node1, ndd_true(), ndd_sylvan_true);
    printf("   ✅ 边添加成功\n");
    
    printf("   尝试AND操作...\n");
    ndd_t result = ndd_and(node1, node2);
    printf("   AND结果: %p\n", (void*)result.node);
    
    // 清理
    printf("   清理资源...\n");
    ndd_deref_safe(result);
    ndd_deref_safe(node1);
    ndd_deref_safe(node2);
    ndd_quit();
    printf("   ✅ NDD系统清理成功\n");
    
    printf("\n🎉 调试测试完成，没有段错误\n");
    return 0;
}