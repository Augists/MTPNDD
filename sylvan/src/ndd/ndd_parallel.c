// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#ifdef HAVE_LACE_SYLVAN
// 真正的 Lace + Sylvan 集成
#include <lace.h>
#include <sylvan.h>
#include <sylvan_bdd.h>

// 全局 Lace + Sylvan 状态
static struct {
    bool initialized;
    uint32_t n_workers;
    size_t dqsize;
    size_t table_size;
    size_t cache_size;
} g_parallel_state = {0};

// 真正的 Lace + Sylvan 初始化
int ndd_lace_init(uint32_t n_workers, size_t dqsize) {
    if (g_parallel_state.initialized) {
        return 0;
    }
    
    printf("🚀 Initializing Lace + Sylvan parallel framework...\n");
    
    // 初始化 Lace 工作窃取框架
    lace_start(n_workers, dqsize);
    
    // 初始化 Sylvan BDD 库
    sylvan_set_sizes(1LL<<24, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_bdd();
    
    g_parallel_state.n_workers = n_workers;
    g_parallel_state.dqsize = dqsize;
    g_parallel_state.initialized = true;
    
    printf("✅ Lace parallel framework initialized with %u workers\n", n_workers);
    printf("✅ Sylvan BDD library initialized\n");
    
    return 0;
}

void ndd_lace_cleanup() {
    if (!g_parallel_state.initialized) {
        return;
    }
    
    printf("🔧 Cleaning up Lace + Sylvan...\n");
    
    // 清理 Sylvan
    sylvan_quit();
    
    // 清理 Lace
    lace_stop();
    
    g_parallel_state.initialized = false;
    printf("✅ Lace + Sylvan parallel framework cleaned up\n");
}

// 真正的并行 BDD 操作包装
ndd_bdd_t ndd_bdd_and_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    return sylvan_and(a, b);
}

ndd_bdd_t ndd_bdd_or_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    return sylvan_or(a, b);
}

ndd_bdd_t ndd_bdd_not_parallel(ndd_bdd_t a) {
    return sylvan_not(a);
}

// 检查 Lace + Sylvan 是否可用
bool ndd_parallel_available() {
    return g_parallel_state.initialized;
}

#else
// 简化模式：没有 Lace + Sylvan 依赖

int ndd_lace_init(uint32_t n_workers, size_t dqsize) {
    printf("⚡ NDD simplified mode (no Lace + Sylvan)\n");
    printf("   Workers: %u, Queue size: %zu\n", n_workers, dqsize);
    return 0;
}

void ndd_lace_cleanup() {
    printf("⚡ NDD simplified mode cleanup\n");
}

// 简化的 BDD 操作
ndd_bdd_t ndd_bdd_and_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    // 简单的位运算模拟
    if (a == ndd_sylvan_false || b == ndd_sylvan_false) return ndd_sylvan_false;
    if (a == ndd_sylvan_true) return b;
    if (b == ndd_sylvan_true) return a;
    return a & b;  // 简化处理
}

ndd_bdd_t ndd_bdd_or_parallel(ndd_bdd_t a, ndd_bdd_t b) {
    if (a == ndd_sylvan_true || b == ndd_sylvan_true) return ndd_sylvan_true;
    if (a == ndd_sylvan_false) return b;
    if (b == ndd_sylvan_false) return a;
    return a | b;  // 简化处理
}

ndd_bdd_t ndd_bdd_not_parallel(ndd_bdd_t a) {
    if (a == ndd_sylvan_true) return ndd_sylvan_false;
    if (a == ndd_sylvan_false) return ndd_sylvan_true;
    return ~a;  // 简化处理
}

bool ndd_parallel_available() {
    return false;
}

#endif

// 通用的并行任务管理接口
void ndd_spawn_task(void (*task_func)(void*), void *arg) {
#ifdef HAVE_LACE_SYLVAN
    if (g_parallel_state.initialized) {
        // 使用真正的 Lace 任务调度
        // TODO: 实现 Lace SPAWN 宏的包装
        task_func(arg);
        return;
    }
#endif
    // 直接执行任务（串行回退）
    task_func(arg);
}

ndd_t ndd_sync_task(ndd_t result) {
#ifdef HAVE_LACE_SYLVAN
    if (g_parallel_state.initialized) {
        // 使用真正的 Lace 同步
        // TODO: 实现 Lace SYNC 宏的包装
        return result;
    }
#endif
    // 直接返回结果（串行回退）
    return result;
}
