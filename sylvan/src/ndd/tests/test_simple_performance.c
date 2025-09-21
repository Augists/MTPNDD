// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

int main() {
    printf("🚀 简化的MTPNDD性能基准测试\n");
    printf("目标：逐步定位段错误问题\n\n");
    
    printf("第1步：初始化NDD系统...\n");
    ndd_config_t config = {
        .n_workers = 2,
        .dqsize = 10000,
        .table_size = 1 << 16,
        .cache_size = 1 << 12,
        .gc_threshold = 50
    };
    
    int init_result = ndd_init(&config);
    if (init_result != 0) {
        printf("❌ NDD初始化失败\n");
        return 1;
    }
    printf("✅ NDD系统初始化成功\n");
    
    printf("第2步：声明字段...\n");
    uint32_t field = ndd_declare_field(4);
    printf("✅ 字段声明成功: %u\n", field);
    
    printf("第3步：创建测试节点...\n");
    ndd_t node1 = ndd_create_node(field);
    ndd_t node2 = ndd_create_node(field);
    printf("✅ 节点创建成功\n");
    
    printf("第4步：添加边...\n");
    ndd_add_edge(&node1, ndd_true(), ndd_sylvan_true);
    ndd_add_edge(&node2, ndd_true(), ndd_sylvan_true);
    printf("✅ 边添加成功\n");
    
    printf("第5步：执行基本逻辑操作...\n");
    printf("  执行AND操作...\n");
    ndd_t and_result = ndd_and(node1, node2);
    printf("  ✅ AND操作成功\n");
    
    printf("  执行OR操作...\n");
    ndd_t or_result = ndd_or(node1, node2);
    printf("  ✅ OR操作成功\n");
    
    printf("  执行NOT操作...\n");
    ndd_t not_result = ndd_not(node1);
    printf("  ✅ NOT操作成功\n");
    
    printf("第6步：获取统计信息...\n");
    ndd_stats_t stats = ndd_get_stats();
    printf("  节点数: %lu\n", stats.node_count);
    printf("  缓存命中: %lu\n", stats.cache_hits);
    printf("  缓存未命中: %lu\n", stats.cache_misses);
    printf("✅ 统计信息获取成功\n");
    
    printf("第7步：清理资源...\n");
    ndd_deref_safe(and_result);
    ndd_deref_safe(or_result);
    ndd_deref_safe(not_result);
    ndd_deref_safe(node1);
    ndd_deref_safe(node2);
    printf("✅ 结果清理成功\n");
    
    ndd_quit();
    printf("✅ NDD系统清理成功\n");
    
    printf("\n🎉 简化性能测试完成！没有段错误\n");
    return 0;
}