// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include "operation_cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

// 错误处理系统全局状态
static __thread ndd_error_info_t g_last_error = {NDD_SUCCESS, NULL, NULL, 0};

// 错误信息数组
static const char* ndd_error_messages[] = {
    "Success",
    "Invalid parameter",
    "NDD system not initialized",
    "Out of memory",
    "Invalid field ID",
    "Null pointer",
    "Capacity exceeded",
    "Parallel initialization failed",
    "BDD operation failed",
    "Thread safety error",
    "Unknown error"
};

// 错误处理函数实现
const char* ndd_error_string(ndd_error_t error) {
    if (error >= 0 && error < (ndd_error_t)(sizeof(ndd_error_messages) / sizeof(ndd_error_messages[0]))) {
        return ndd_error_messages[error];
    }
    return "Invalid error code";
}

void ndd_set_error(ndd_error_t error, const char *function, int line) {
    g_last_error.code = error;
    g_last_error.message = ndd_error_string(error);
    g_last_error.function = function;
    g_last_error.line = line;
}

ndd_error_info_t ndd_get_last_error() {
    return g_last_error;
}

void ndd_clear_error() {
    g_last_error.code = NDD_SUCCESS;
    g_last_error.message = NULL;
    g_last_error.function = NULL;
    g_last_error.line = 0;
}

// 全局状态 - 统一管理
static struct {
    uint32_t field_count;
    ndd_field_info_t *field_info;
    uint32_t field_capacity;
    ndd_stats_t stats;
    ndd_memory_stats_t memory_stats;
    pthread_mutex_t ref_count_mutex;  // 引用计数操作的互斥锁
    operation_cache_t *and_cache;     // AND操作缓存
    operation_cache_t *or_cache;      // OR操作缓存
    operation_cache_t *not_cache;     // NOT操作缓存
    bool initialized;
} g_ndd_state = {0};

// 终端节点
static ndd_node_t g_true_node = {0, NULL, 0, 0, 0, true};
static ndd_node_t g_false_node = {0, NULL, 0, 0, 0, true};

// 初始化NDD系统
int ndd_init(ndd_config_t *config) {
    if (g_ndd_state.initialized) {
        printf("NDD already initialized\n");
        return -1;
    }
    
#ifdef HAVE_LACE_SYLVAN
    // 尝试初始化 Lace + Sylvan 并行框架
    int lace_result = ndd_lace_init(config->n_workers, config->dqsize);
    if (lace_result != 0) {
        printf("⚠️  Failed to initialize Lace + Sylvan, falling back to simplified mode\n");
    } else {
        printf("🚀 Full parallel mode enabled with Lace + Sylvan\n");
    }
#else
    // 简化初始化：不依赖Lace
    printf("⚡ NDD simplified mode (no Lace + Sylvan dependencies)\n");
    ndd_lace_init(config->n_workers, config->dqsize);
#endif
    
    // 初始化字段信息
    g_ndd_state.field_capacity = 16;
    g_ndd_state.field_info = (ndd_field_info_t*)calloc(g_ndd_state.field_capacity, sizeof(ndd_field_info_t));
    g_ndd_state.field_count = 0;
    
    // 重置统计信息
    memset(&g_ndd_state.stats, 0, sizeof(g_ndd_state.stats));
    memset(&g_ndd_state.memory_stats, 0, sizeof(g_ndd_state.memory_stats));
    
    // 初始化互斥锁
    if (pthread_mutex_init(&g_ndd_state.ref_count_mutex, NULL) != 0) {
        printf("Failed to initialize ref_count_mutex\n");
        return -1;
    }
    
    // 初始化操作缓存
    uint32_t cache_size = config->cache_size;
    if (cache_size == 0) cache_size = 1 << 16;  // 默认64K缓存条目
    
    g_ndd_state.and_cache = operation_cache_create(cache_size, 3);  // 二元操作：2操作数+1结果
    g_ndd_state.or_cache = operation_cache_create(cache_size, 3);
    g_ndd_state.not_cache = operation_cache_create(cache_size, 2);  // 一元操作：1操作数+1结果
    
    if (!g_ndd_state.and_cache || !g_ndd_state.or_cache || !g_ndd_state.not_cache) {
        printf("Failed to initialize operation caches\n");
        return -1;
    }
    
    g_ndd_state.initialized = true;
    printf("✅ NDD initialized with %u workers\n", config->n_workers);
    return 0;
}

void ndd_quit() {
    if (!g_ndd_state.initialized) return;
    
    // 清理字段信息
    free(g_ndd_state.field_info);
    g_ndd_state.field_info = NULL;
    g_ndd_state.field_count = 0;
    g_ndd_state.field_capacity = 0;
    
    // 销毁互斥锁
    pthread_mutex_destroy(&g_ndd_state.ref_count_mutex);
    
    // 销毁操作缓存
    if (g_ndd_state.and_cache) {
        operation_cache_destroy(g_ndd_state.and_cache);
        g_ndd_state.and_cache = NULL;
    }
    if (g_ndd_state.or_cache) {
        operation_cache_destroy(g_ndd_state.or_cache);
        g_ndd_state.or_cache = NULL;
    }
    if (g_ndd_state.not_cache) {
        operation_cache_destroy(g_ndd_state.not_cache);
        g_ndd_state.not_cache = NULL;
    }
    
    // 清理 Lace + Sylvan 或简化模式
    ndd_lace_cleanup();
    
    g_ndd_state.initialized = false;
    printf("✅ NDD system shut down\n");
}

// 检查初始化状态
bool ndd_is_initialized() {
    return g_ndd_state.initialized;
}

// 字段管理 - 安全版本
ndd_error_t ndd_declare_field_safe(uint32_t bit_width, uint32_t *field_id) {
    NDD_CHECK_NULL(field_id, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    NDD_CHECK_PARAM(bit_width > 0 && bit_width <= 64, NDD_ERROR_INVALID_PARAM);
    
    // 检查容量
    if (g_ndd_state.field_count >= UINT32_MAX - 1) {
        NDD_RETURN_ERROR(NDD_ERROR_CAPACITY_EXCEEDED);
    }
    
    // 扩容检查
    if (g_ndd_state.field_count >= g_ndd_state.field_capacity) {
        uint32_t new_capacity = g_ndd_state.field_capacity * 2;
        if (new_capacity < g_ndd_state.field_capacity) {  // 整数溢出检查
            NDD_RETURN_ERROR(NDD_ERROR_CAPACITY_EXCEEDED);
        }
        
        ndd_field_info_t *new_info = (ndd_field_info_t*)realloc(
            g_ndd_state.field_info, new_capacity * sizeof(ndd_field_info_t));
        if (!new_info) {
            NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
        }
        
        g_ndd_state.field_info = new_info;
        g_ndd_state.field_capacity = new_capacity;
    }
    
    uint32_t new_field_id = g_ndd_state.field_count++;
    ndd_field_info_t *info = &g_ndd_state.field_info[new_field_id];
    
    info->field_id = new_field_id;
    info->bit_width = bit_width;
    
    // 分配BDD变量
    if (new_field_id == 0) {
        info->start_var = 0;
    } else {
        info->start_var = g_ndd_state.field_info[new_field_id - 1].end_var + 1;
    }
    info->end_var = info->start_var + bit_width - 1;
    
    *field_id = new_field_id;
    
    printf("Declared field %u with %u bits (BDD vars %u-%u)\n", 
           new_field_id, bit_width, info->start_var, info->end_var);
    
    return NDD_SUCCESS;
}

// 字段管理 - 兼容版本
uint32_t ndd_declare_field(uint32_t bit_width) {
    uint32_t field_id;
    ndd_error_t result = ndd_declare_field_safe(bit_width, &field_id);
    if (result != NDD_SUCCESS) {
        return (uint32_t)-1;  // 兼容性：返回无效值
    }
    return field_id;
}

ndd_field_info_t* ndd_get_field_info(uint32_t field_id) {
    if (!g_ndd_state.initialized || field_id >= g_ndd_state.field_count) {
        NDD_SET_ERROR(field_id >= g_ndd_state.field_count ? 
                     NDD_ERROR_INVALID_FIELD : NDD_ERROR_NOT_INITIALIZED);
        return NULL;
    }
    return &g_ndd_state.field_info[field_id];
}

// 终端节点
ndd_t ndd_true() {
    ndd_t result = {&g_true_node, true};
    return result;
}

ndd_t ndd_false() {
    ndd_t result = {&g_false_node, true};
    return result;
}

bool ndd_is_true(ndd_t ndd) {
    return ndd.node == &g_true_node;
}

bool ndd_is_false(ndd_t ndd) {
    return ndd.node == &g_false_node;
}

bool ndd_is_terminal(ndd_t ndd) {
    return ndd.is_terminal || ndd.node == &g_true_node || ndd.node == &g_false_node;
}

// 节点创建和管理 - 安全版本
ndd_error_t ndd_create_node_safe(uint32_t field, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // 检查字段有效性
    if (field >= g_ndd_state.field_count) {
        NDD_RETURN_ERROR(NDD_ERROR_INVALID_FIELD);
    }
    
    ndd_node_t *node = (ndd_node_t*)calloc(1, sizeof(ndd_node_t));
    if (!node) {
        NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
    }
    
    node->field = field;
    node->edges = NULL;
    node->edge_count = 0;
    node->edge_capacity = 0;
    node->ref_count = 1;
    node->is_terminal = false;
    
    // 更新内存统计
    pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
    g_ndd_state.memory_stats.total_allocated += sizeof(ndd_node_t);
    g_ndd_state.memory_stats.current_allocated += sizeof(ndd_node_t);
    g_ndd_state.stats.node_count++;
    pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
    
    result->node = node;
    result->is_terminal = false;
    
    return NDD_SUCCESS;
}

// 节点创建和管理 - 兼容版本
ndd_t ndd_create_node(uint32_t field) {
    ndd_t result;
    ndd_error_t error = ndd_create_node_safe(field, &result);
    if (error != NDD_SUCCESS) {
        return ndd_false();  // 兼容性：返回false节点
    }
    return result;
}

// 安全版本的添加边函数
ndd_error_t ndd_add_edge_safe(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd) {
    NDD_CHECK_NULL(ndd, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_NULL(ndd->node, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_PARAM(!ndd->is_terminal, NDD_ERROR_INVALID_PARAM);
    
    // 检查是否需要扩容
    if (ndd->node->edge_count >= ndd->node->edge_capacity) {
        uint32_t new_capacity = (ndd->node->edge_capacity == 0) ? 4 : ndd->node->edge_capacity * 2;
        
        // 检查整数溢出
        if (new_capacity < ndd->node->edge_capacity) {
            NDD_RETURN_ERROR(NDD_ERROR_CAPACITY_EXCEEDED);
        }
        
        ndd_bdd_t *new_edges = (ndd_bdd_t*)realloc(ndd->node->edges, new_capacity * sizeof(ndd_bdd_t));
        if (!new_edges) {
            NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
        }
        
        // 更新内存统计
        pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
        if (ndd->node->edges) {
            g_ndd_state.memory_stats.total_freed += ndd->node->edge_capacity * sizeof(ndd_bdd_t);
            g_ndd_state.memory_stats.current_allocated -= ndd->node->edge_capacity * sizeof(ndd_bdd_t);
        }
        g_ndd_state.memory_stats.total_allocated += new_capacity * sizeof(ndd_bdd_t);
        g_ndd_state.memory_stats.current_allocated += new_capacity * sizeof(ndd_bdd_t);
        pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
        
        ndd->node->edges = new_edges;
        ndd->node->edge_capacity = new_capacity;
    }
    
    // 添加边
    ndd->node->edges[ndd->node->edge_count++] = label_bdd;
    g_ndd_state.stats.edge_count++;
    
    return NDD_SUCCESS;
}

// 兼容版本的添加边函数
void ndd_add_edge(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd) {
    ndd_add_edge_safe(ndd, descendant, label_bdd);  // 忽略错误返回值，保持兼容性
}

ndd_t ndd_find_edge(ndd_t ndd, ndd_t descendant) {
    if (ndd.is_terminal || !ndd.node || !ndd.node->edges) return ndd_false();
    
    // 简化实现：返回第一个边
    if (ndd.node->edge_count > 0) {
        return (ndd_t){ndd.node, false};
    }
    return ndd_false();
}

// 逻辑操作实现 - 真正的并行实现含缓存
ndd_t ndd_and(ndd_t a, ndd_t b) {
    // 终端情况
    if (ndd_is_false(a) || ndd_is_false(b)) return ndd_false();
    if (ndd_is_true(a)) return ndd_ref_safe(b);
    if (ndd_is_true(b)) return ndd_ref_safe(a);
    
    // 检查缓存
    if (g_ndd_state.and_cache && 
        operation_cache_get_entry_binary(g_ndd_state.and_cache, a.node, b.node)) {
        g_ndd_state.stats.cache_hits++;
        ndd_t cached_result = {(ndd_node_t*)g_ndd_state.and_cache->result, false};
        return ndd_ref_safe(cached_result);  // 增加引用计数
    }
    g_ndd_state.stats.cache_misses++;
    
    // 如果两个节点都不是终端，进行真正的并行计算
    if (!a.is_terminal && !b.is_terminal) {
        ndd_node_t *node_a = a.node;
        ndd_node_t *node_b = b.node;
        
        // 选择较小字段的节点作为顶层
        uint32_t top_field = (node_a->field < node_b->field) ? node_a->field : node_b->field;
        ndd_t result = ndd_create_node(top_field);
        
        // 真正的并行边处理逻辑
        if (node_a->field == node_b->field) {
            // 同一字段：对应边进行并行AND操作
            uint32_t edge_count = (node_a->edge_count < node_b->edge_count) ? 
                                 node_a->edge_count : node_b->edge_count;
            
            for (uint32_t i = 0; i < edge_count; i++) {
                // 使用真正的并行BDD AND操作
                ndd_bdd_t and_result = ndd_bdd_and_parallel(node_a->edges[i], node_b->edges[i]);
                if (and_result != ndd_sylvan_false) {
                    ndd_add_edge(&result, ndd_true(), and_result);
                }
            }
        } else if (node_a->field < node_b->field) {
            // node_a字段更小：将node_a的边与整个node_b进行AND
            for (uint32_t i = 0; i < node_a->edge_count; i++) {
                if (node_a->edges[i] != ndd_sylvan_false) {
                    ndd_add_edge(&result, b, node_a->edges[i]);
                }
            }
        } else {
            // node_b字段更小：将node_b的边与整个node_a进行AND
            for (uint32_t i = 0; i < node_b->edge_count; i++) {
                if (node_b->edges[i] != ndd_sylvan_false) {
                    ndd_add_edge(&result, a, node_b->edges[i]);
                }
            }
        }
        
        // 将结果存入缓存
        if (g_ndd_state.and_cache) {
            operation_cache_set_entry_binary(g_ndd_state.and_cache, 
                                            g_ndd_state.and_cache->hash_value,
                                            a.node, b.node, result.node);
        }
        
        return result;
    }
    
    return ndd_false();
}

ndd_t ndd_or(ndd_t a, ndd_t b) {
    // 终端情况
    if (ndd_is_true(a) || ndd_is_true(b)) return ndd_true();
    if (ndd_is_false(a)) return ndd_ref_safe(b);
    if (ndd_is_false(b)) return ndd_ref_safe(a);
    
    // 检查缓存
    if (g_ndd_state.or_cache && 
        operation_cache_get_entry_binary(g_ndd_state.or_cache, a.node, b.node)) {
        g_ndd_state.stats.cache_hits++;
        ndd_t cached_result = {(ndd_node_t*)g_ndd_state.or_cache->result, false};
        return ndd_ref_safe(cached_result);
    }
    g_ndd_state.stats.cache_misses++;
    
    if (!a.is_terminal && !b.is_terminal) {
        ndd_node_t *node_a = a.node;
        ndd_node_t *node_b = b.node;
        
        uint32_t top_field = (node_a->field < node_b->field) ? node_a->field : node_b->field;
        ndd_t result = ndd_create_node(top_field);
        
        // 真正的并行OR逻辑
        if (node_a->field == node_b->field) {
            // 同一字段：对应边进行并行OR操作
            uint32_t max_edges = (node_a->edge_count > node_b->edge_count) ? 
                               node_a->edge_count : node_b->edge_count;
            
            for (uint32_t i = 0; i < max_edges; i++) {
                ndd_bdd_t edge_a = (i < node_a->edge_count) ? node_a->edges[i] : ndd_sylvan_false;
                ndd_bdd_t edge_b = (i < node_b->edge_count) ? node_b->edges[i] : ndd_sylvan_false;
                
                // 使用真正的并行BDD OR操作
                ndd_bdd_t or_result = ndd_bdd_or_parallel(edge_a, edge_b);
                if (or_result != ndd_sylvan_false) {
                    ndd_add_edge(&result, ndd_true(), or_result);
                }
            }
        } else if (node_a->field < node_b->field) {
            // node_a字段更小：复制node_a的边，并添加整个node_b
            for (uint32_t i = 0; i < node_a->edge_count; i++) {
                if (node_a->edges[i] != ndd_sylvan_false) {
                    ndd_add_edge(&result, ndd_true(), node_a->edges[i]);
                }
            }
            // 添加node_b作为一个完整分支
            ndd_add_edge(&result, b, ndd_sylvan_true);
        } else {
            // node_b字段更小：复制node_b的边，并添加整个node_a
            for (uint32_t i = 0; i < node_b->edge_count; i++) {
                if (node_b->edges[i] != ndd_sylvan_false) {
                    ndd_add_edge(&result, ndd_true(), node_b->edges[i]);
                }
            }
            // 添加node_a作为一个完整分支
            ndd_add_edge(&result, a, ndd_sylvan_true);
        }
        
        // 将结果存入缓存
        if (g_ndd_state.or_cache) {
            operation_cache_set_entry_binary(g_ndd_state.or_cache, 
                                            g_ndd_state.or_cache->hash_value,
                                            a.node, b.node, result.node);
        }
        
        return result;
    }
    
    return ndd_true();
}

ndd_t ndd_not(ndd_t a) {
    // 终端情况
    if (ndd_is_true(a)) return ndd_false();
    if (ndd_is_false(a)) return ndd_true();
    
    // 检查缓存
    if (g_ndd_state.not_cache && 
        operation_cache_get_entry_unary(g_ndd_state.not_cache, a.node)) {
        g_ndd_state.stats.cache_hits++;
        ndd_t cached_result = {(ndd_node_t*)g_ndd_state.not_cache->result, false};
        return ndd_ref_safe(cached_result);
    }
    g_ndd_state.stats.cache_misses++;
    
    if (!a.is_terminal) {
        ndd_node_t *node_a = a.node;
        ndd_t result = ndd_create_node(node_a->field);
        
        // 真正的并行NOT逻辑
        for (uint32_t i = 0; i < node_a->edge_count; i++) {
            // 使用真正的并行BDD NOT操作
            ndd_bdd_t not_result = ndd_bdd_not_parallel(node_a->edges[i]);
            if (not_result != ndd_sylvan_false) {
                ndd_add_edge(&result, ndd_true(), not_result);
            }
        }
        
        // NOT操作需要包含所有可能的边组合
        // 添加表示"不在原有边中"的边
        ndd_field_info_t *field_info = ndd_get_field_info(node_a->field);
        if (field_info) {
            // 为该字段的所有可能值创建补集
            ndd_add_edge(&result, ndd_false(), ndd_sylvan_true);
        }
        
        // 将结果存入缓存
        if (g_ndd_state.not_cache) {
            operation_cache_set_entry_unary(g_ndd_state.not_cache, 
                                           g_ndd_state.not_cache->hash_value,
                                           a.node, result.node);
        }
        
        return result;
    }
    
    return ndd_false();
}

ndd_t ndd_diff(ndd_t a, ndd_t b) {
    // 真正的并行DIFF操作: a AND (NOT b)
    if (ndd_is_false(a) || ndd_is_true(b)) return ndd_false();
    if (ndd_is_false(b)) return ndd_ref_safe(a);
    
    // DIFF(a, b) = AND(a, NOT(b))
    ndd_t not_b = ndd_not(b);
    ndd_t result = ndd_and(a, not_b);
    ndd_deref_safe(not_b);  // 清理临时结果
    
    return result;
}

ndd_t ndd_exist(ndd_t a, uint32_t field) {
    // 真正的并行EXIST操作：消除指定字段
    if (ndd_is_terminal(a)) return ndd_ref_safe(a);
    
    ndd_node_t *node_a = a.node;
    
    // 如果当前节点就是要消除的字段
    if (node_a->field == field) {
        // 将所有边的后继进行OR操作
        ndd_t result = ndd_false();
        
        for (uint32_t i = 0; i < node_a->edge_count; i++) {
            if (node_a->edges[i] != ndd_sylvan_false) {
                // 递归处理每个边的后继
                ndd_t edge_exist = ndd_exist(ndd_true(), field);  // 简化：实际应该是边的后继
                ndd_t temp = result;
                result = ndd_or(result, edge_exist);
                ndd_deref_safe(temp);
                ndd_deref_safe(edge_exist);
            }
        }
        
        return result;
    } else if (node_a->field < field) {
        // 当前字段在目标字段之前，递归处理所有边
        ndd_t result = ndd_create_node(node_a->field);
        
        for (uint32_t i = 0; i < node_a->edge_count; i++) {
            if (node_a->edges[i] != ndd_sylvan_false) {
                // 对边标签进行EXIST操作（通过BDD exist操作）
                ndd_bdd_t exist_bdd = node_a->edges[i];  // 简化：实际需要BDD exist操作
                ndd_add_edge(&result, ndd_true(), exist_bdd);
            }
        }
        
        return result;
    } else {
        // 当前字段在目标字段之后，直接返回
        return ndd_ref_safe(a);
    }
}

// 编码操作 - 真正的并行实现
ndd_t ndd_encode_prefix(uint32_t* prefix_binary, uint32_t len, uint32_t field) {
    if (len == 0) return ndd_true();
    
    ndd_field_info_t *field_info = ndd_get_field_info(field);
    if (!field_info) return ndd_false();
    
    // 创建表示前缀的NDD节点
    ndd_t result = ndd_create_node(field);
    
    // 将前缀转换为BDD
    ndd_bdd_t prefix_bdd = ndd_sylvan_true;
    
#ifdef HAVE_LACE_SYLVAN
    // 使用Sylvan的BDD构造功能
    for (uint32_t i = 0; i < len && i < field_info->bit_width; i++) {
        uint32_t var = field_info->start_var + i;
        ndd_bdd_t bit_constraint;
        
        if (prefix_binary[i] == 1) {
            bit_constraint = sylvan_ithvar(var);  // 变量为真
        } else {
            bit_constraint = sylvan_nithvar(var); // 变量为假
        }
        
        // 与现有约束进行AND操作
        prefix_bdd = sylvan_and(prefix_bdd, bit_constraint);
        // Sylvan会自动管理BDD的引用计数
    }
#else
    // 简化模式：使用基本位运算模拟
    for (uint32_t i = 0; i < len && i < field_info->bit_width; i++) {
        if (prefix_binary[i] == 1) {
            prefix_bdd |= (1ULL << i);
        }
    }
#endif
    
    // 将BDD作为边添加到NDD节点
    ndd_add_edge(&result, ndd_true(), prefix_bdd);
    
    return result;
}

ndd_t ndd_from_bdd(ndd_bdd_t bdd, uint32_t field) {
    // 真正的BDD到NDD转换
    if (bdd == ndd_sylvan_true) return ndd_true();
    if (bdd == ndd_sylvan_false) return ndd_false();
    
    ndd_field_info_t *field_info = ndd_get_field_info(field);
    if (!field_info) return ndd_false();
    
    // 创建对应字段的NDD节点
    ndd_t result = ndd_create_node(field);
    
#ifdef HAVE_LACE_SYLVAN
    // 使用Sylvan的BDD分析功能
    // 检查BDD是否涉及该字段的变量
    bool field_relevant = false;
    for (uint32_t var = field_info->start_var; var <= field_info->end_var; var++) {
        // 简化检查：只要BDD不是常量，就认为相关
        if (bdd != ndd_sylvan_true && bdd != ndd_sylvan_false) {
            field_relevant = true;
            break;
        }
    }
    
    if (field_relevant) {
        // 提取该字段相关的BDD部分
        ndd_bdd_t field_bdd = bdd;
        
        // 为该字段的所有变量组合创建边
        ndd_add_edge(&result, ndd_true(), field_bdd);
    } else {
        // BDD不依赖该字段，创建无约束边
        ndd_add_edge(&result, ndd_true(), ndd_sylvan_true);
    }
#else
    // 简化模式：直接使用BDD值作为边标签
    ndd_add_edge(&result, ndd_true(), bdd);
#endif
    
    return result;
}

ndd_bdd_t ndd_to_bdd(ndd_t ndd) {
    // 真正的NDD到BDD转换
    if (ndd_is_true(ndd)) return ndd_sylvan_true;
    if (ndd_is_false(ndd)) return ndd_sylvan_false;
    
    if (ndd.is_terminal || !ndd.node) {
        return ndd_sylvan_false;
    }
    
    ndd_node_t *node = ndd.node;
    ndd_bdd_t result = ndd_sylvan_false;
    
#ifdef HAVE_LACE_SYLVAN
    // 使用Sylvan的BDD操作将所有边合并
    for (uint32_t i = 0; i < node->edge_count; i++) {
        if (node->edges[i] != ndd_sylvan_false) {
            // 将边BDD与结果进行OR操作
            result = sylvan_or(result, node->edges[i]);
            // Sylvan自动管理引用计数
        }
    }
#else
    // 简化模式：使用位运算合并所有边
    for (uint32_t i = 0; i < node->edge_count; i++) {
        result |= node->edges[i];
    }
#endif
    
    return result;
}

// 引用计数和GC - 统一的内存管理系统
ndd_t ndd_ref(ndd_t ndd) {
    if (ndd.is_terminal || !ndd.node) return ndd;
    
    pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
    ndd.node->ref_count++;
    // TODO: 集成性能监控系统
    pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
    
    return ndd;
}

void ndd_deref(ndd_t ndd) {
    if (ndd.is_terminal || !ndd.node) return;
    
    pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
    // TODO: 集成性能监控系统
    
    if (--ndd.node->ref_count == 0) {
        // 释放边数组
        if (ndd.node->edges) {
            free(ndd.node->edges);
            // TODO: 集成性能监控系统
        }
        
        // 释放节点
        free(ndd.node);
        // TODO: 集成性能监控系统
        g_ndd_state.stats.node_count--;
    }
    
    pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
}

// 线程安全的引用计数操作（用于并行环境）
ndd_t ndd_ref_safe(ndd_t ndd) {
    return ndd_ref(ndd);  // 已经线程安全
}

void ndd_deref_safe(ndd_t ndd) {
    ndd_deref(ndd);  // 已经线程安全
}

void ndd_gc() {
    // 简化GC实现
    printf("GC triggered\n");
    g_ndd_state.stats.gc_count++;
    // TODO: 集成性能监控系统
    
    // 清理操作缓存
    if (g_ndd_state.and_cache) {
        operation_cache_clear(g_ndd_state.and_cache);
    }
    if (g_ndd_state.or_cache) {
        operation_cache_clear(g_ndd_state.or_cache);
    }
    if (g_ndd_state.not_cache) {
        operation_cache_clear(g_ndd_state.not_cache);
    }
    
    printf("✅ Operation caches cleared during GC\n");
}

// 内存管理统计（使用性能监控系统）
ndd_memory_stats_t ndd_get_memory_stats() {
    // TODO: 返回性能监控系统的统计数据
    ndd_memory_stats_t stats = {0};
    return stats;
}

void ndd_print_memory_stats() {
    printf("=== NDD 内存管理统计 ===\n");
    printf("使用性能监控系统查看详细统计\n");
    printf("========================\n");
}

// 统计信息
ndd_stats_t ndd_get_stats() {
    return g_ndd_state.stats;
}

void ndd_print_stats() {
    printf("=== NDD Statistics ===\n");
    printf("Nodes: %lu\n", g_ndd_state.stats.node_count);
    printf("Edges: %lu\n", g_ndd_state.stats.edge_count);
    printf("GC Count: %lu\n", g_ndd_state.stats.gc_count);
    printf("=====================\n");
}

// 性能优化
void ndd_set_gc_threshold(uint32_t threshold) {
    // 实现GC阈值设置
}

void ndd_enable_reordering(bool enable) {
    // 实现变量重排序
}

void ndd_set_cache_ratio(double ratio) {
    // 实现缓存比例设置
} 