// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef NDD_H
#define NDD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 性能监控系统
#include "ndd_performance_monitor.h"

// 错误处理系统
typedef enum ndd_error_e {
    NDD_SUCCESS = 0,           // 成功
    NDD_ERROR_INVALID_PARAM,   // 无效参数
    NDD_ERROR_NOT_INITIALIZED, // 未初始化
    NDD_ERROR_OUT_OF_MEMORY,   // 内存不足
    NDD_ERROR_INVALID_FIELD,   // 无效字段
    NDD_ERROR_NULL_POINTER,    // 空指针
    NDD_ERROR_CAPACITY_EXCEEDED, // 容量超限
    NDD_ERROR_PARALLEL_INIT,   // 并行初始化失败
    NDD_ERROR_BDD_OPERATION,   // BDD操作失败
    NDD_ERROR_THREAD_SAFETY,   // 线程安全错误
    NDD_ERROR_UNKNOWN          // 未知错误
} ndd_error_t;

// 错误信息结构
typedef struct ndd_error_info_s {
    ndd_error_t code;
    const char *message;
    const char *function;
    int line;
} ndd_error_info_t;

// 错误处理函数
const char* ndd_error_string(ndd_error_t error);
void ndd_set_error(ndd_error_t error, const char *function, int line);
ndd_error_info_t ndd_get_last_error();
void ndd_clear_error();

// 错误处理宏
#define NDD_SET_ERROR(error) ndd_set_error(error, __FUNCTION__, __LINE__)
#define NDD_RETURN_ERROR(error) do { NDD_SET_ERROR(error); return error; } while(0)
#define NDD_RETURN_NULL_ON_ERROR(error) do { NDD_SET_ERROR(error); return (ndd_t){NULL, false}; } while(0)
#define NDD_RETURN_FALSE_ON_ERROR(error) do { NDD_SET_ERROR(error); return ndd_false(); } while(0)

// 参数验证宏
#define NDD_CHECK_PARAM(condition, error) \
    do { if (!(condition)) { NDD_RETURN_ERROR(error); } } while(0)
#define NDD_CHECK_NULL(ptr, error) \
    do { if ((ptr) == NULL) { NDD_RETURN_ERROR(error); } } while(0)
#define NDD_CHECK_INIT() \
    do { if (!ndd_is_initialized()) { NDD_RETURN_ERROR(NDD_ERROR_NOT_INITIALIZED); } } while(0)

// 参数验证宏（返回ndd_t）
#define NDD_CHECK_PARAM_NDD(condition, error) \
    do { if (!(condition)) { NDD_RETURN_NULL_ON_ERROR(error); } } while(0)
#define NDD_CHECK_NULL_NDD(ptr, error) \
    do { if ((ptr) == NULL) { NDD_RETURN_NULL_ON_ERROR(error); } } while(0)
#define NDD_CHECK_INIT_NDD() \
    do { if (!ndd_is_initialized()) { NDD_RETURN_NULL_ON_ERROR(NDD_ERROR_NOT_INITIALIZED); } } while(0)

#ifdef HAVE_LACE_SYLVAN
// 使用真实的 Sylvan BDD
#include <sylvan.h>
#include <lace.h>
typedef BDD ndd_bdd_t;
#define ndd_sylvan_true sylvan_true
#define ndd_sylvan_false sylvan_false
#else
// 简化的BDD类型定义（仅在没有Sylvan时使用）
typedef uint64_t ndd_bdd_t;
#define ndd_sylvan_true ((ndd_bdd_t)1)
#define ndd_sylvan_false ((ndd_bdd_t)0)
// 前向声明，避免循环依赖
typedef struct lace_worker_s lace_worker_t;
typedef struct lace_dq_s lace_dq_t;
#endif

// 统一的数据结构定义
typedef struct ndd_node_s {
    uint32_t field;
    ndd_bdd_t *edges;           // BDD边标签数组
    uint32_t edge_count;
    uint32_t edge_capacity;
    uint32_t ref_count;   // 引用计数
    bool is_terminal;     // 是否为终端节点
} ndd_node_t;

// NDD句柄类型
typedef struct ndd_s {
    ndd_node_t *node;
    bool is_terminal;
} ndd_t;

// 字段信息
typedef struct ndd_field_info_s {
    uint32_t field_id;
    uint32_t bit_width;
    uint32_t start_var;
    uint32_t end_var;
} ndd_field_info_t;

// 配置结构
typedef struct ndd_config_s {
    uint32_t n_workers;
    size_t dqsize;
    size_t table_size;
    size_t cache_size;
    uint32_t gc_threshold;
} ndd_config_t;

// 统计信息
typedef struct ndd_stats_s {
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t bdd_node_count;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t gc_count;
    double parallel_efficiency;
} ndd_stats_t;

// 全局状态管理
extern uint32_t g_field_count;
extern ndd_field_info_t *g_field_info;
extern uint32_t g_field_capacity;

// 初始化和清理
int ndd_init(ndd_config_t *config);
void ndd_quit();
bool ndd_is_initialized();  // 检查初始化状态

// 字段管理
ndd_error_t ndd_declare_field_safe(uint32_t bit_width, uint32_t *field_id);  // 安全版本
uint32_t ndd_declare_field(uint32_t bit_width);  // 兼容版本
ndd_field_info_t* ndd_get_field_info(uint32_t field_id);

// 终端节点
ndd_t ndd_true();
ndd_t ndd_false();
bool ndd_is_true(ndd_t ndd);
bool ndd_is_false(ndd_t ndd);
bool ndd_is_terminal(ndd_t ndd);

// 节点创建和管理
ndd_error_t ndd_create_node_safe(uint32_t field, ndd_t *result);  // 安全版本
ndd_t ndd_create_node(uint32_t field);  // 兼容版本
ndd_error_t ndd_add_edge_safe(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd);  // 安全版本
void ndd_add_edge(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd);  // 兼容版本
ndd_t ndd_find_edge(ndd_t ndd, ndd_t descendant);

// 逻辑操作 - 统一并行实现
ndd_t ndd_and(ndd_t a, ndd_t b);
ndd_t ndd_or(ndd_t a, ndd_t b);
ndd_t ndd_not(ndd_t a);
ndd_t ndd_diff(ndd_t a, ndd_t b);
ndd_t ndd_exist(ndd_t a, uint32_t field);

// 向后兼容的并行接口（内部调用统一实现）
#define ndd_and_parallel(a, b) ndd_and(a, b)
#define ndd_or_parallel(a, b) ndd_or(a, b)
#define ndd_not_parallel(a) ndd_not(a)
#define ndd_diff_parallel(a, b) ndd_diff(a, b)
#define ndd_exist_parallel(a, field) ndd_exist(a, field)

// 编码操作
ndd_error_t ndd_encode_prefix_safe(uint32_t* prefix_binary, uint32_t len, uint32_t field, ndd_t *result);
ndd_t ndd_encode_prefix(uint32_t* prefix_binary, uint32_t len, uint32_t field);  // 兼容版本
ndd_error_t ndd_from_bdd_safe(ndd_bdd_t bdd, uint32_t field, ndd_t *result);
ndd_t ndd_from_bdd(ndd_bdd_t bdd, uint32_t field);  // 兼容版本
ndd_error_t ndd_to_bdd_safe(ndd_t ndd, ndd_bdd_t *result);
ndd_bdd_t ndd_to_bdd(ndd_t ndd);  // 兼容版本

// 引用计数和GC - 统一的内存管理系统
ndd_t ndd_ref(ndd_t ndd);
void ndd_deref(ndd_t ndd);
void ndd_gc();

// 线程安全的引用计数操作
ndd_t ndd_ref_safe(ndd_t ndd);
void ndd_deref_safe(ndd_t ndd);

// 内存管理统计（使用性能监控系统中的定义）

ndd_memory_stats_t ndd_get_memory_stats();
void ndd_print_memory_stats();

// 统计信息
ndd_stats_t ndd_get_stats();
void ndd_print_stats();

// 性能优化
void ndd_set_gc_threshold(uint32_t threshold);
void ndd_enable_reordering(bool enable);
void ndd_set_cache_ratio(double ratio);

// 并行任务管理
void ndd_spawn_task(void (*task_func)(void*), void *arg);
ndd_t ndd_sync_task(ndd_t result);

// Lace框架管理函数
int ndd_lace_init(uint32_t n_workers, size_t dqsize);
void ndd_lace_cleanup();

// 并行支持检查
bool ndd_parallel_available();

// 并行BDD操作接口
ndd_bdd_t ndd_bdd_and_parallel(ndd_bdd_t a, ndd_bdd_t b);
ndd_bdd_t ndd_bdd_or_parallel(ndd_bdd_t a, ndd_bdd_t b);
ndd_bdd_t ndd_bdd_not_parallel(ndd_bdd_t a);

#endif // NDD_H
