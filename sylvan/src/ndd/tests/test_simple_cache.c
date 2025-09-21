// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main() {
    printf("🚀 NDD缓存系统基础测试开始\n");
    
    // 基本配置
    ndd_config_t config = {
        .n_workers = 1,
        .dqsize = 1000,
        .table_size = 1 << 12,
        .cache_size = 16,  // 很小的缓存
        .gc_threshold = 10
    };
    
    int init_result = ndd_init(&config);
    if (init_result != 0) {
        printf("❌ 初始化失败\n");
        return 1;
    }
    printf("✅ NDD系统初始化成功\n");
    
    // 创建简单的测试节点
    uint32_t field = ndd_declare_field(2);  // 2位字段
    printf("✅ 声明字段成功: %u\n", field);
    
    ndd_t node_a = ndd_create_node(field);
    ndd_t node_b = ndd_create_node(field);
    printf("✅ 创建节点成功\n");
    
    // 获取初始统计
    ndd_stats_t stats1 = ndd_get_stats();
    printf("初始统计 - 命中: %lu, 未命中: %lu\n", stats1.cache_hits, stats1.cache_misses);
    
    // 第一次操作
    printf("执行第一次AND操作...\n");
    ndd_t result1 = ndd_and(node_a, node_b);
    
    ndd_stats_t stats2 = ndd_get_stats();
    printf("第一次后 - 命中: %lu, 未命中: %lu\n", stats2.cache_hits, stats2.cache_misses);
    
    // 第二次相同操作
    printf("执行第二次AND操作...\n");
    ndd_t result2 = ndd_and(node_a, node_b);
    
    ndd_stats_t stats3 = ndd_get_stats();
    printf("第二次后 - 命中: %lu, 未命中: %lu\n", stats3.cache_hits, stats3.cache_misses);
    
    // 验证缓存效果
    uint64_t cache_hits_gained = stats3.cache_hits - stats2.cache_hits;
    printf("新增缓存命中: %lu\n", cache_hits_gained);
    
    if (cache_hits_gained > 0) {
        printf("✅ 缓存系统工作正常！\n");
    } else {
        printf("⚠️ 缓存系统可能有问题\n");
    }
    
    // 清理
    printf("清理资源...\n");
    ndd_deref_safe(node_a);
    ndd_deref_safe(node_b);
    ndd_deref_safe(result1);
    ndd_deref_safe(result2);
    
    ndd_quit();
    printf("✅ 测试完成\n");
    
    return 0;
}