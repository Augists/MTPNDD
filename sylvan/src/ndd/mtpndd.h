// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef MTPNDD_H
#define MTPNDD_H

#include "ndd.h"
#include <string.h>
#include <math.h>

// 终端值类型枚举
typedef enum mtpndd_terminal_type_e {
    MTPNDD_TERMINAL_BOOLEAN = 0,    // 布尔值（兼容现有NDD）
    MTPNDD_TERMINAL_INTEGER,        // 64位整数  
    MTPNDD_TERMINAL_DOUBLE,         // 双精度浮点数
    MTPNDD_TERMINAL_STRING,         // 字符串（以\0结尾）
    MTPNDD_TERMINAL_BYTES,          // 二进制数据
    MTPNDD_TERMINAL_CUSTOM          // 自定义类型
} mtpndd_terminal_type_t;

// 字符串终端值（带长度，避免频繁strlen）
typedef struct mtpndd_string_s {
    char *data;
    size_t length;
    size_t capacity;
} mtpndd_string_t;

// 二进制数据终端值
typedef struct mtpndd_bytes_s {
    void *data;
    size_t length;
    size_t capacity;
} mtpndd_bytes_t;

// 自定义类型终端值
typedef struct mtpndd_custom_s {
    void *data;
    size_t size;
    // 函数指针用于自定义操作
    int (*compare)(const void *a, const void *b);
    void* (*copy)(const void *data);
    void (*destroy)(void *data);
    char* (*to_string)(const void *data);
} mtpndd_custom_t;

// 终端值联合体（好品味：统一所有类型，避免分支）
typedef union mtpndd_terminal_value_u {
    bool boolean;                   // 布尔值
    int64_t integer;               // 整数
    double floating;               // 浮点数
    mtpndd_string_t string;        // 字符串
    mtpndd_bytes_t bytes;          // 二进制数据
    mtpndd_custom_t custom;        // 自定义类型
} mtpndd_terminal_value_t;

// 完整的终端值结构（类型+值+引用计数）
typedef struct mtpndd_terminal_s {
    mtpndd_terminal_type_t type;
    mtpndd_terminal_value_t value;
    uint32_t ref_count;            // 引用计数，线程安全
} mtpndd_terminal_t;

// MTPNDD节点结构（扩展自ndd_node_t）
typedef struct mtpndd_node_s {
    // 继承所有ndd_node_t字段，保持兼容性
    uint32_t field;
    ndd_bdd_t *edges;              // BDD边标签数组
    uint32_t edge_count;
    uint32_t edge_capacity;
    uint32_t ref_count;            // 节点引用计数
    bool is_terminal;              // 是否为终端节点
    
    // MTPNDD扩展字段
    mtpndd_terminal_t *terminal;   // 终端值（仅当is_terminal=true时有效）
    bool is_multiterminal;         // 标识是否为多终端节点
} mtpndd_node_t;

// MTPNDD句柄类型（扩展自ndd_t）
typedef struct mtpndd_s {
    mtpndd_node_t *node;           // 指向MTPNDD节点
    bool is_terminal;              // 继承自ndd_t，保持兼容
    bool is_multiterminal;         // 是否为多终端
} mtpndd_t;

// ===========================================
// 向后兼容性：NDD <-> MTPNDD 无缝转换
// ===========================================

// 将NDD转换为MTPNDD（零成本转换）
static inline mtpndd_t ndd_to_mtpndd(ndd_t ndd) {
    mtpndd_t result;
    result.node = (mtpndd_node_t*)ndd.node;  // 安全转换
    result.is_terminal = ndd.is_terminal;
    result.is_multiterminal = false;         // NDD始终为单终端
    return result;
}

// 将MTPNDD转换为NDD（兼容性检查）
static inline ndd_t mtpndd_to_ndd(mtpndd_t mtpndd) {
    ndd_t result;
    result.node = (ndd_node_t*)mtpndd.node;  // 安全转换
    result.is_terminal = mtpndd.is_terminal;
    return result;
}

// ===========================================
// 终端值创建和管理
// ===========================================

// 创建布尔终端值（兼容现有NDD）
mtpndd_terminal_t* mtpndd_terminal_boolean(bool value);

// 创建整数终端值
mtpndd_terminal_t* mtpndd_terminal_integer(int64_t value);

// 创建浮点数终端值
mtpndd_terminal_t* mtpndd_terminal_double(double value);

// 创建字符串终端值（自动计算长度）
mtpndd_terminal_t* mtpndd_terminal_string(const char *str);

// 创建字符串终端值（指定长度）
mtpndd_terminal_t* mtpndd_terminal_string_n(const char *str, size_t len);

// 创建二进制数据终端值
mtpndd_terminal_t* mtpndd_terminal_bytes(const void *data, size_t len);

// 创建自定义类型终端值
mtpndd_terminal_t* mtpndd_terminal_custom(
    void *data, 
    size_t size,
    int (*compare)(const void *a, const void *b),
    void* (*copy)(const void *data),
    void (*destroy)(void *data),
    char* (*to_string)(const void *data)
);

// 终端值引用计数管理（线程安全）
mtpndd_terminal_t* mtpndd_terminal_ref(mtpndd_terminal_t *terminal);
void mtpndd_terminal_deref(mtpndd_terminal_t *terminal);

// 终端值比较
int mtpndd_terminal_compare(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);

// 终端值复制
mtpndd_terminal_t* mtpndd_terminal_copy(const mtpndd_terminal_t *terminal);

// ===========================================
// MTPNDD节点创建和管理
// ===========================================

// 创建多终端节点
mtpndd_t mtpndd_create_terminal(mtpndd_terminal_t *terminal);

// 创建普通MTPNDD节点（兼容NDD）
mtpndd_t mtpndd_create_node(uint32_t field);

// 添加边到MTPNDD节点
int mtpndd_add_edge_ex(mtpndd_t *mtpndd, mtpndd_t descendant, ndd_bdd_t label_bdd);

// ===========================================
// 向后兼容的便捷函数
// ===========================================

// 从现有NDD布尔值创建MTPNDD
mtpndd_t mtpndd_from_boolean(bool value);

// 从其他类型创建MTPNDD
mtpndd_t mtpndd_from_integer(int64_t value);
mtpndd_t mtpndd_from_double(double value);
mtpndd_t mtpndd_from_string(const char *str);
mtpndd_t mtpndd_from_bytes(const void *data, size_t len);

// 兼容现有的ndd_true()和ndd_false()
#define mtpndd_true() mtpndd_from_boolean(true)
#define mtpndd_false() mtpndd_from_boolean(false)

// 类型检查
bool mtpndd_is_terminal(mtpndd_t mtpndd);
bool mtpndd_is_multiterminal(mtpndd_t mtpndd);
bool mtpndd_is_boolean_terminal(mtpndd_t mtpndd);
bool mtpndd_is_integer_terminal(mtpndd_t mtpndd);
bool mtpndd_is_double_terminal(mtpndd_t mtpndd);
bool mtpndd_is_string_terminal(mtpndd_t mtpndd);

// 值获取（带类型检查）
bool mtpndd_get_boolean(mtpndd_t mtpndd);
int64_t mtpndd_get_integer(mtpndd_t mtpndd);
double mtpndd_get_double(mtpndd_t mtpndd);
const char* mtpndd_get_string(mtpndd_t mtpndd);
const void* mtpndd_get_bytes(mtpndd_t mtpndd, size_t *length);
const void* mtpndd_get_custom(mtpndd_t mtpndd, size_t *size);

// 节点属性访问
mtpndd_terminal_type_t mtpndd_get_terminal_type(mtpndd_t mtpndd);
uint32_t mtpndd_get_field(mtpndd_t mtpndd);
uint32_t mtpndd_get_edge_count(mtpndd_t mtpndd);
uint32_t mtpndd_get_ref_count(mtpndd_t mtpndd);
uint32_t mtpndd_get_depth(mtpndd_t mtpndd);
uint32_t mtpndd_count_nodes(mtpndd_t mtpndd);
uint32_t mtpndd_count_terminals(mtpndd_t mtpndd);

// 节点修改操作
int mtpndd_set_terminal_integer(mtpndd_t *mtpndd, int64_t value);
int mtpndd_set_terminal_string(mtpndd_t *mtpndd, const char *str);
int mtpndd_set_terminal_bytes(mtpndd_t *mtpndd, const void *data, size_t len);
int mtpndd_replace_terminal(mtpndd_t *mtpndd, mtpndd_terminal_t *new_terminal);
int mtpndd_remove_edge(mtpndd_t *mtpndd, uint32_t edge_index);
int mtpndd_clear_edges(mtpndd_t *mtpndd);
int mtpndd_set_terminal_integer(mtpndd_t *mtpndd, int64_t value);
int mtpndd_set_terminal_string(mtpndd_t *mtpndd, const char *str);
int mtpndd_set_terminal_bytes(mtpndd_t *mtpndd, const void *data, size_t len);
int mtpndd_replace_terminal(mtpndd_t *mtpndd, mtpndd_terminal_t *new_terminal);
int mtpndd_remove_edge(mtpndd_t *mtpndd, uint32_t edge_index);
int mtpndd_clear_edges(mtpndd_t *mtpndd);

// ===========================================
// 高级节点创建操作
// ===========================================

// 便捷的节点创建函数（类型推断）
mtpndd_t mtpndd_create_integer_node(int64_t value);
mtpndd_t mtpndd_create_double_node(double value);
mtpndd_t mtpndd_create_string_node(const char *str);
mtpndd_t mtpndd_create_bytes_node(const void *data, size_t len);

// 节点复制和克隆
mtpndd_t mtpndd_copy(mtpndd_t source);
mtpndd_t mtpndd_clone(mtpndd_t source);

// 引用计数和内存管理
mtpndd_t mtpndd_ref(mtpndd_t mtpndd);
void mtpndd_deref(mtpndd_t mtpndd);

// ===========================================
// MTPNDD边和值集合结构
// ===========================================

// MTPNDD边结构
typedef struct mtpndd_edge_s {
    ndd_bdd_t label_bdd;     // 边标签BDD
    mtpndd_t target;         // 目标节点
    bool is_valid;           // 是否有效
} mtpndd_edge_t;

// 值集合联合体
typedef union mtpndd_value_set_union_u {
    bool *booleans;
    int64_t *integers;
    double *doubles;
    char **strings;
    mtpndd_bytes_t *bytes;
    mtpndd_custom_t *customs;
} mtpndd_value_set_union_t;

// 值集合结构
typedef struct mtpndd_value_set_s {
    mtpndd_terminal_type_t type;
    uint32_t count;
    mtpndd_value_set_union_t values;
} mtpndd_value_set_t;

// ===========================================
// MTPNDD多终端逻辑操作
// ===========================================

// 多终端值逻辑操作类型
typedef enum mtpndd_logic_operation_e {
    MTPNDD_OP_AND,         // 逻辑与
    MTPNDD_OP_OR,          // 逻辑或
    MTPNDD_OP_NOT,         // 逻辑非
    MTPNDD_OP_XOR,         // 逻辑异或
    MTPNDD_OP_IMP,         // 逻辑蕴含
    MTPNDD_OP_DIFF,        // 逻辑差
    MTPNDD_OP_ITE          // if-then-else
} mtpndd_logic_operation_t;

// 终端值运算策略
typedef enum mtpndd_terminal_operation_strategy_e {
    MTPNDD_STRATEGY_BOOLEAN,     // 布尔逻辑（true/false）
    MTPNDD_STRATEGY_NUMERIC_MIN, // 数值最小值
    MTPNDD_STRATEGY_NUMERIC_MAX, // 数值最大值
    MTPNDD_STRATEGY_NUMERIC_ADD, // 数值加法
    MTPNDD_STRATEGY_NUMERIC_MUL, // 数值乘法
    MTPNDD_STRATEGY_BITWISE,     // 按位运算
    MTPNDD_STRATEGY_STRING_CONCAT, // 字符串连接
    MTPNDD_STRATEGY_STRING_CHOICE, // 字符串选择（首选第一个）
    MTPNDD_STRATEGY_CUSTOM        // 自定义运算
} mtpndd_terminal_operation_strategy_t;

// 逻辑操作配置
typedef struct mtpndd_logic_config_s {
    mtpndd_terminal_operation_strategy_t strategy;
    void *user_data;                               // 用户数据
    bool auto_type_promotion;                      // 自动类型提升
    bool preserve_type_safety;                     // 保持类型安全
} mtpndd_logic_config_t;

// 基础多终端逻辑操作
mtpndd_t mtpndd_and(mtpndd_t a, mtpndd_t b);
mtpndd_t mtpndd_or(mtpndd_t a, mtpndd_t b);
mtpndd_t mtpndd_not(mtpndd_t a);

// 配置化的多终端逻辑操作
mtpndd_t mtpndd_and_with_config(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);
mtpndd_t mtpndd_or_with_config(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);
mtpndd_t mtpndd_not_with_config(mtpndd_t a, const mtpndd_logic_config_t *config);

// 条件分支操作（if-then-else）
mtpndd_t mtpndd_ite(mtpndd_t condition, mtpndd_t then_branch, mtpndd_t else_branch);
mtpndd_t mtpndd_ite_with_config(mtpndd_t condition, mtpndd_t then_branch, mtpndd_t else_branch,
                               const mtpndd_logic_config_t *config);

// 逻辑操作辅助函数
bool mtpndd_terminals_compatible(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_logic_config_t mtpndd_create_default_logic_config(mtpndd_terminal_operation_strategy_t strategy);

// 逻辑运算统计和性能监控
typedef struct mtpndd_logic_stats_s {
    uint64_t and_operations;
    uint64_t or_operations;
    uint64_t not_operations;
    uint64_t ite_operations;
    uint64_t terminal_conversions;
    uint64_t type_promotions;
    double average_operation_time;
    uint64_t cache_hits;
    uint64_t cache_misses;
} mtpndd_logic_stats_t;

// 获取逻辑运算统计信息
mtpndd_logic_stats_t mtpndd_get_logic_stats();
void mtpndd_reset_logic_stats();
void mtpndd_print_logic_stats();

// ===========================================
// MTPNDD序列化和反序列化操作（简化版本）
// ===========================================

// 序列化格式枚举
typedef enum mtpndd_serialization_format_e {
    MTPNDD_FORMAT_BINARY,     // 二进制格式（高效）
    MTPNDD_FORMAT_JSON,       // JSON格式（可读）
    MTPNDD_FORMAT_XML,        // XML格式（兼容）
    MTPNDD_FORMAT_CUSTOM      // 自定义格式
} mtpndd_serialization_format_t;

// 序列化选项
typedef struct mtpndd_serialization_options_s {
    mtpndd_serialization_format_t format;
    bool compress_data;               // 是否压缩数据
    bool include_metadata;            // 是否包含元数据
    bool validate_integrity;          // 是否验证完整性
    uint32_t version;                 // 序列化版本号
    const char *encoding;             // 编码格式（如UTF-8）
} mtpndd_serialization_options_t;

// 序列化结果
typedef struct mtpndd_serialized_data_s {
    void *data;                       // 序列化后的数据
    size_t size;                      // 数据大小
    mtpndd_serialization_format_t format; // 数据格式
    uint32_t checksum;                // 数据校验和
    char *error_message;              // 错误信息（如果有）
} mtpndd_serialized_data_t;

// 反序列化结果
typedef struct mtpndd_deserialized_result_s {
    mtpndd_t *nodes;                  // 反序列化的节点数组
    uint32_t node_count;              // 节点数量
    mtpndd_serialization_options_t options; // 原始序列化选项
    bool integrity_verified;          // 完整性验证结果
    char *warning_message;            // 警告信息（如果有）
} mtpndd_deserialized_result_t;

// 序列化统计信息
typedef struct mtpndd_serialization_stats_s {
    size_t total_nodes;               // 总节点数
    size_t terminal_nodes;            // 终端节点数
    size_t internal_nodes;            // 内部节点数
    size_t unique_terminals;          // 唯一终端值数
    size_t compressed_size;           // 压缩后大小
    size_t uncompressed_size;         // 压缩前大小
    double compression_ratio;         // 压缩比
    uint64_t serialization_time_ms;   // 序列化耗时（毫秒）
} mtpndd_serialization_stats_t;

// 批量创建结构
typedef struct mtpndd_batch_create_s {
    mtpndd_terminal_type_t type;
    mtpndd_terminal_value_t value;
} mtpndd_batch_create_t;

// 序列化操作
mtpndd_serialized_data_t* mtpndd_serialize_node(mtpndd_t mtpndd, 
                                               const mtpndd_serialization_options_t *options);
mtpndd_deserialized_result_t* mtpndd_deserialize(const mtpndd_serialized_data_t *data);
int mtpndd_serialize_to_file(mtpndd_t mtpndd, const char *filename,
                           const mtpndd_serialization_options_t *options);
mtpndd_deserialized_result_t* mtpndd_deserialize_from_file(const char *filename);
bool mtpndd_verify_serialized_data(const mtpndd_serialized_data_t *data);
mtpndd_serialization_stats_t mtpndd_get_serialization_stats(const mtpndd_serialized_data_t *data);
void mtpndd_destroy_serialized_data(mtpndd_serialized_data_t *data);
void mtpndd_destroy_deserialized_result(mtpndd_deserialized_result_t *result);

// 批量创建
int mtpndd_create_terminals_batch(const mtpndd_batch_create_t *specs, size_t count, mtpndd_t *results);

// ===========================================
// MTPNDD自定义终端值运算系统
// ===========================================

// 自定义运算函数类型定义
typedef mtpndd_terminal_t* (*mtpndd_custom_operation_fn)(
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2,
    void *user_data
);

// 一元运算函数类型
typedef mtpndd_terminal_t* (*mtpndd_unary_operation_fn)(
    const mtpndd_terminal_t *operand,
    void *user_data
);

// 运算函数元数据
typedef struct mtpndd_operation_metadata_s {
    const char *name;                    // 运算名称
    const char *description;             // 运算描述
    mtpndd_terminal_type_t input_type1;  // 第一个操作数类型
    mtpndd_terminal_type_t input_type2;  // 第二个操作数类型（一元运算时忽略）
    mtpndd_terminal_type_t output_type;  // 输出类型
    bool is_commutative;                 // 是否满足交换律
    bool is_associative;                 // 是否满足结合律
    bool is_unary;                       // 是否为一元运算
    uint32_t operation_id;               // 运算ID（用于缓存）
} mtpndd_operation_metadata_t;

// 运算注册项
typedef struct mtpndd_operation_registry_s {
    mtpndd_operation_metadata_t metadata;
    mtpndd_custom_operation_fn binary_fn;  // 二元运算函数
    mtpndd_unary_operation_fn unary_fn;    // 一元运算函数
    void *user_data;                       // 用户数据
    bool is_active;                        // 是否激活
} mtpndd_operation_registry_t;

// 运算管理器
typedef struct mtpndd_operation_manager_s {
    mtpndd_operation_registry_t *operations;  // 注册的运算数组
    uint32_t operation_count;                 // 运算数量
    uint32_t operation_capacity;              // 运算容量
    uint32_t next_operation_id;               // 下一个运算ID
} mtpndd_operation_manager_t;

// 运算结果缓存项
typedef struct mtpndd_operation_cache_entry_s {
    uint32_t operation_id;                // 运算ID
    const mtpndd_terminal_t *operand1;   // 第一个操作数
    const mtpndd_terminal_t *operand2;   // 第二个操作数（一元运算时为NULL）
    mtpndd_terminal_t *result;            // 运算结果
    uint64_t access_count;                // 访问次数
    uint64_t last_access_time;            // 最后访问时间
    bool is_valid;                        // 是否有效
} mtpndd_operation_cache_entry_t;

// 运算缓存管理器
typedef struct mtpndd_operation_cache_manager_s {
    mtpndd_operation_cache_entry_t *cache; // 缓存数组
    uint32_t cache_size;                   // 缓存大小
    uint32_t cache_used;                   // 已使用缓存
    uint64_t cache_hits;                   // 缓存命中次数
    uint64_t cache_misses;                 // 缓存未命中次数
    bool enable_cache;                     // 是否启用缓存
} mtpndd_operation_cache_manager_t;

// 预定义运算类型
typedef enum mtpndd_predefined_operation_e {
    MTPNDD_OP_ADD,           // 加法
    MTPNDD_OP_SUB,           // 减法
    MTPNDD_OP_MUL,           // 乘法
    MTPNDD_OP_DIV,           // 除法
    MTPNDD_OP_MOD,           // 取模
    MTPNDD_OP_POW,           // 幂运算
    MTPNDD_OP_ABS,           // 绝对值（一元）
    MTPNDD_OP_NEG,           // 取负（一元）
    MTPNDD_OP_SQRT,          // 平方根（一元）
    MTPNDD_OP_LOG,           // 对数（一元）
    MTPNDD_OP_SIN,           // 正弦（一元）
    MTPNDD_OP_COS,           // 余弦（一元）
    MTPNDD_OP_CONCAT,        // 字符串连接
    MTPNDD_OP_SUBSTRING,     // 子字符串
    MTPNDD_OP_LENGTH,        // 字符串长度（一元）
    MTPNDD_OP_UPPERCASE,     // 转大写（一元）
    MTPNDD_OP_LOWERCASE,     // 转小写（一元）
    MTPNDD_OP_TRIM,          // 去空格（一元）
    MTPNDD_OP_EQUALS,        // 相等比较
    MTPNDD_OP_COMPARE,       // 大小比较
    MTPNDD_OP_CONTAINS,      // 包含检查
    MTPNDD_OP_STARTS_WITH,   // 前缀检查
    MTPNDD_OP_ENDS_WITH,     // 后缀检查
    MTPNDD_OP_COUNT          // 预定义运算总数
} mtpndd_predefined_operation_t;

// ===========================================
// 自定义运算管理API
// ===========================================

// 初始化运算管理器
int mtpndd_init_operation_manager();
void mtpndd_cleanup_operation_manager();

// 注册自定义运算
uint32_t mtpndd_register_binary_operation(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t input_type1,
    mtpndd_terminal_type_t input_type2,
    mtpndd_terminal_type_t output_type,
    mtpndd_custom_operation_fn operation_fn,
    void *user_data,
    bool is_commutative,
    bool is_associative
);

uint32_t mtpndd_register_unary_operation(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t input_type,
    mtpndd_terminal_type_t output_type,
    mtpndd_unary_operation_fn operation_fn,
    void *user_data
);

// 注销运算
int mtpndd_unregister_operation(uint32_t operation_id);

// 查找运算
uint32_t mtpndd_find_operation_by_name(const char *name);
uint32_t mtpndd_find_operation_by_types(
    mtpndd_terminal_type_t input_type1,
    mtpndd_terminal_type_t input_type2,
    mtpndd_terminal_type_t output_type,
    bool is_unary
);

// 获取运算信息
const mtpndd_operation_metadata_t* mtpndd_get_operation_metadata(uint32_t operation_id);
int mtpndd_list_operations(mtpndd_operation_metadata_t *operations, uint32_t max_count);

// ===========================================
// 自定义运算执行API
// ===========================================

// 执行自定义运算
mtpndd_terminal_t* mtpndd_execute_custom_operation(
    uint32_t operation_id,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2  // 一元运算时传NULL
);

// 按名称执行运算
mtpndd_terminal_t* mtpndd_execute_operation_by_name(
    const char *operation_name,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
);

// 自动匹配并执行运算
mtpndd_terminal_t* mtpndd_execute_auto_operation(
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2,
    mtpndd_terminal_type_t desired_output_type
);

// 执行预定义运算
mtpndd_terminal_t* mtpndd_execute_predefined_operation(
    mtpndd_predefined_operation_t op_type,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
);

// ===========================================
// 运算优化和缓存API
// ===========================================

// 初始化运算缓存
int mtpndd_init_operation_cache(uint32_t cache_size);
void mtpndd_cleanup_operation_cache();

// 缓存控制
void mtpndd_enable_operation_cache(bool enable);
void mtpndd_clear_operation_cache();
void mtpndd_optimize_operation_cache();  // 缓存优化（清理未使用项）

// 批量运算
typedef struct mtpndd_batch_operation_s {
    uint32_t operation_id;
    const mtpndd_terminal_t *operand1;
    const mtpndd_terminal_t *operand2;
    mtpndd_terminal_t *result;  // 输出结果
} mtpndd_batch_operation_t;

// 批量执行运算
int mtpndd_execute_batch_operations(
    mtpndd_batch_operation_t *operations,
    uint32_t operation_count
);

// 并行批量运算
int mtpndd_execute_parallel_batch_operations(
    mtpndd_batch_operation_t *operations,
    uint32_t operation_count,
    uint32_t thread_count
);

// ===========================================
// 高级自定义运算功能
// ===========================================

// 运算链式调用
typedef struct mtpndd_operation_chain_s {
    uint32_t *operation_ids;     // 运算ID序列
    uint32_t operation_count;    // 运算数量
    void **intermediate_data;    // 中间数据（可选）
} mtpndd_operation_chain_t;

// 创建和执行运算链
mtpndd_operation_chain_t* mtpndd_create_operation_chain();
int mtpndd_add_operation_to_chain(mtpndd_operation_chain_t *chain, uint32_t operation_id);
mtpndd_terminal_t* mtpndd_execute_operation_chain(
    mtpndd_operation_chain_t *chain,
    const mtpndd_terminal_t *initial_operand
);
void mtpndd_destroy_operation_chain(mtpndd_operation_chain_t *chain);

// 条件运算
typedef struct mtpndd_conditional_operation_s {
    bool (*condition_fn)(const mtpndd_terminal_t *operand, void *user_data);
    uint32_t true_operation_id;
    uint32_t false_operation_id;
    void *condition_data;
} mtpndd_conditional_operation_t;

// 执行条件运算
mtpndd_terminal_t* mtpndd_execute_conditional_operation(
    const mtpndd_conditional_operation_t *cond_op,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
);

// 运算统计信息
typedef struct mtpndd_operation_stats_s {
    uint64_t total_operations;           // 总运算次数
    uint64_t cached_operations;          // 缓存运算次数
    uint64_t failed_operations;          // 失败运算次数
    uint64_t registered_operations;      // 注册运算数量
    uint64_t active_operations;          // 激活运算数量
    double average_operation_time;       // 平均运算时间
    double cache_hit_ratio;              // 缓存命中率
    uint64_t memory_usage;               // 内存使用量
} mtpndd_operation_stats_t;

// 获取运算统计信息
mtpndd_operation_stats_t mtpndd_get_operation_stats();
void mtpndd_reset_operation_stats();
void mtpndd_print_operation_stats();

// ===========================================
// 预定义运算快捷函数
// ===========================================

// 数值运算
mtpndd_terminal_t* mtpndd_add(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_subtract(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_multiply(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_divide(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_modulo(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_power(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);

// 一元数值运算
mtpndd_terminal_t* mtpndd_abs(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_negate(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_sqrt(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_log(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_sin(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_cos(const mtpndd_terminal_t *a);

// 字符串运算
mtpndd_terminal_t* mtpndd_concat(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_substring(const mtpndd_terminal_t *str, const mtpndd_terminal_t *pos);
mtpndd_terminal_t* mtpndd_length(const mtpndd_terminal_t *str);
mtpndd_terminal_t* mtpndd_uppercase(const mtpndd_terminal_t *str);
mtpndd_terminal_t* mtpndd_lowercase(const mtpndd_terminal_t *str);
mtpndd_terminal_t* mtpndd_trim(const mtpndd_terminal_t *str);

// 比较运算
mtpndd_terminal_t* mtpndd_equals(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_compare(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_contains(const mtpndd_terminal_t *str, const mtpndd_terminal_t *substr);
mtpndd_terminal_t* mtpndd_starts_with(const mtpndd_terminal_t *str, const mtpndd_terminal_t *prefix);
mtpndd_terminal_t* mtpndd_ends_with(const mtpndd_terminal_t *str, const mtpndd_terminal_t *suffix);

// 内部函数声明
static void mtpndd_register_predefined_operations();

// ===========================================
// 终端值约束和验证机制
// ===========================================

// 约束类型枚举
typedef enum mtpndd_constraint_type_e {
    MTPNDD_CONSTRAINT_RANGE,          // 数值范围约束
    MTPNDD_CONSTRAINT_SET,            // 值集合约束（枚举值）
    MTPNDD_CONSTRAINT_PATTERN,        // 字符串模式约束（正则表达式）
    MTPNDD_CONSTRAINT_LENGTH,         // 长度约束
    MTPNDD_CONSTRAINT_CUSTOM,         // 自定义验证函数
    MTPNDD_CONSTRAINT_TYPE_SAFE,      // 类型安全约束
    MTPNDD_CONSTRAINT_NOT_NULL        // 非空约束
} mtpndd_constraint_type_t;

// 约束条件联合体
typedef union mtpndd_constraint_value_u {
    struct {
        double min;
        double max;
        bool min_inclusive;
        bool max_inclusive;
    } range;
    
    struct {
        void **values;
        uint32_t count;
        mtpndd_terminal_type_t value_type;
    } set;
    
    struct {
        char *pattern;
        bool case_sensitive;
    } pattern;
    
    struct {
        uint32_t min_length;
        uint32_t max_length;
    } length;
    
    struct {
        bool (*validate_fn)(const mtpndd_terminal_t *terminal, void *user_data);
        void *user_data;
    } custom;
} mtpndd_constraint_value_t;

// 约束结构定义
typedef struct mtpndd_constraint_s {
    uint32_t constraint_id;
    const char *name;
    const char *description;
    mtpndd_constraint_type_t type;
    mtpndd_terminal_type_t target_type;
    mtpndd_constraint_value_t value;
    bool is_active;
    uint64_t validation_count;
    uint64_t violation_count;
} mtpndd_constraint_t;

// 验证结果结构
typedef struct mtpndd_validation_result_s {
    bool is_valid;
    uint32_t violated_constraint_count;
    uint32_t *violated_constraint_ids;
    char **violation_messages;
    ndd_error_t error_code;
    const char *error_message;
} mtpndd_validation_result_t;

// 约束管理器结构
typedef struct mtpndd_constraint_manager_s {
    mtpndd_constraint_t *constraints;
    uint32_t constraint_count;
    uint32_t constraint_capacity;
    uint32_t next_constraint_id;
    bool global_validation_enabled;
    bool strict_mode;  // 严格模式：任何约束违反都导致操作失败
} mtpndd_constraint_manager_t;

// 约束统计信息
typedef struct mtpndd_constraint_stats_s {
    uint64_t total_validations;
    uint64_t successful_validations;
    uint64_t failed_validations;
    uint64_t constraint_violations;
    uint64_t type_violations;
    double average_validation_time;
    uint32_t active_constraints;
    uint32_t total_constraints;
} mtpndd_constraint_stats_t;

// ===========================================
// 约束管理API
// ===========================================

// 初始化约束管理器
int mtpndd_init_constraint_manager();
void mtpndd_cleanup_constraint_manager();

// 启用/禁用全局验证
void mtpndd_enable_global_validation(bool enable);
void mtpndd_set_strict_mode(bool strict);

// 约束注册和管理
uint32_t mtpndd_register_range_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    double min_value,
    double max_value,
    bool min_inclusive,
    bool max_inclusive
);

uint32_t mtpndd_register_set_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    void **allowed_values,
    uint32_t value_count
);

uint32_t mtpndd_register_pattern_constraint(
    const char *name,
    const char *description,
    const char *pattern,
    bool case_sensitive
);

uint32_t mtpndd_register_length_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    uint32_t min_length,
    uint32_t max_length
);

uint32_t mtpndd_register_custom_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    bool (*validate_fn)(const mtpndd_terminal_t *terminal, void *user_data),
    void *user_data
);

// 约束查询和控制
int mtpndd_activate_constraint(uint32_t constraint_id);
int mtpndd_deactivate_constraint(uint32_t constraint_id);
int mtpndd_remove_constraint(uint32_t constraint_id);
const mtpndd_constraint_t* mtpndd_get_constraint(uint32_t constraint_id);
uint32_t mtpndd_find_constraint_by_name(const char *name);
int mtpndd_list_constraints(uint32_t *constraint_ids, uint32_t max_count);

// ===========================================
// 验证API
// ===========================================

// 单个终端值验证
mtpndd_validation_result_t* mtpndd_validate_terminal(const mtpndd_terminal_t *terminal);
mtpndd_validation_result_t* mtpndd_validate_terminal_with_constraints(
    const mtpndd_terminal_t *terminal,
    const uint32_t *constraint_ids,
    uint32_t constraint_count
);

// 批量验证
int mtpndd_validate_terminals_batch(
    const mtpndd_terminal_t **terminals,
    uint32_t terminal_count,
    mtpndd_validation_result_t **results
);

// MTPNDD节点验证
mtpndd_validation_result_t* mtpndd_validate_node(mtpndd_t mtpndd);

// 验证结果管理
void mtpndd_destroy_validation_result(mtpndd_validation_result_t *result);
bool mtpndd_is_validation_successful(const mtpndd_validation_result_t *result);

// ===========================================
// 预定义约束
// ===========================================

// 常用数值约束
uint32_t mtpndd_register_positive_constraint(const char *name);     // > 0
uint32_t mtpndd_register_non_negative_constraint(const char *name);  // >= 0
uint32_t mtpndd_register_percentage_constraint(const char *name);    // [0, 100]
uint32_t mtpndd_register_probability_constraint(const char *name);   // [0.0, 1.0]

// 常用字符串约束
uint32_t mtpndd_register_non_empty_string_constraint(const char *name);
uint32_t mtpndd_register_email_constraint(const char *name);
uint32_t mtpndd_register_phone_constraint(const char *name);
uint32_t mtpndd_register_alphanumeric_constraint(const char *name);

// 类型安全约束
uint32_t mtpndd_register_type_constraint(
    const char *name,
    mtpndd_terminal_type_t required_type
);

// ===========================================
// 约束统计和监控
// ===========================================

// 获取约束统计信息
mtpndd_constraint_stats_t mtpndd_get_constraint_stats();
void mtpndd_reset_constraint_stats();
void mtpndd_print_constraint_stats();

// 约束性能监控
void mtpndd_start_constraint_profiling();
void mtpndd_stop_constraint_profiling();
double mtpndd_get_constraint_validation_time(uint32_t constraint_id);

// ===========================================
// 高级验证功能
// ===========================================

// 条件约束：根据其他字段值决定是否应用约束
typedef struct mtpndd_conditional_constraint_s {
    uint32_t base_constraint_id;
    bool (*condition_fn)(const mtpndd_terminal_t *terminal, void *context);
    void *condition_context;
} mtpndd_conditional_constraint_t;

uint32_t mtpndd_register_conditional_constraint(
    const char *name,
    const char *description,
    uint32_t base_constraint_id,
    bool (*condition_fn)(const mtpndd_terminal_t *terminal, void *context),
    void *condition_context
);

// 复合约束：多个约束的逻辑组合
typedef enum mtpndd_compound_operator_e {
    MTPNDD_COMPOUND_AND,    // 所有约束都必须满足
    MTPNDD_COMPOUND_OR,     // 至少一个约束必须满足
    MTPNDD_COMPOUND_XOR,    // 恰好一个约束必须满足
    MTPNDD_COMPOUND_NOT     // 约束必须不满足
} mtpndd_compound_operator_t;

uint32_t mtpndd_register_compound_constraint(
    const char *name,
    const char *description,
    const uint32_t *constraint_ids,
    uint32_t constraint_count,
    mtpndd_compound_operator_t operator
);

// 动态约束：运行时可修改的约束
typedef struct mtpndd_dynamic_constraint_s {
    uint32_t constraint_id;
    void *dynamic_data;
    int (*update_fn)(mtpndd_constraint_t *constraint, void *dynamic_data);
} mtpndd_dynamic_constraint_t;

uint32_t mtpndd_register_dynamic_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    void *initial_data,
    int (*update_fn)(mtpndd_constraint_t *constraint, void *dynamic_data)
);

int mtpndd_update_dynamic_constraint(uint32_t constraint_id, void *new_data);

// ===========================================
// 错误处理
// ===========================================

// MTPNDD特定错误类型
typedef enum mtpndd_error_e {
    MTPNDD_SUCCESS = 0,
    MTPNDD_ERROR_TYPE_MISMATCH,    // 类型不匹配
    MTPNDD_ERROR_INVALID_TERMINAL, // 无效终端值
    MTPNDD_ERROR_STRING_TOO_LONG,  // 字符串过长
    MTPNDD_ERROR_UNSUPPORTED_TYPE, // 不支持的类型
    MTPNDD_ERROR_CONVERSION_FAILED // 类型转换失败
} mtpndd_error_t;

// 错误处理函数
const char* mtpndd_error_string(mtpndd_error_t error);
void mtpndd_set_error(mtpndd_error_t error, const char *function, int line);
mtpndd_error_t mtpndd_get_last_error();

// ===========================================
// 配置和统计
// ===========================================

// MTPNDD配置结构（扩展自ndd_config_t）
typedef struct mtpndd_config_s {
    ndd_config_t base;             // 基础NDD配置
    size_t max_string_length;      // 最大字符串长度
    size_t max_bytes_length;       // 最大二进制数据长度
    bool enable_string_interning;  // 启用字符串内化（节省内存）
    bool enable_terminal_cache;    // 启用终端值缓存
} mtpndd_config_t;

// MTPNDD统计信息
typedef struct mtpndd_stats_s {
    ndd_stats_t base;              // 基础NDD统计
    uint64_t terminal_count;       // 终端值数量
    uint64_t multiterminal_count;  // 多终端节点数量
    uint64_t string_count;         // 字符串终端数量
    uint64_t integer_count;        // 整数终端数量
    uint64_t double_count;         // 浮点数终端数量
    uint64_t bytes_count;          // 二进制数据终端数量
    uint64_t custom_count;         // 自定义类型终端数量
} mtpndd_stats_t;

mtpndd_stats_t mtpndd_get_stats();
void mtpndd_print_stats();

// ===========================================
// 初始化和清理
// ===========================================

// MTPNDD初始化（内部调用ndd_init）
int mtpndd_init(mtpndd_config_t *config);

// MTPNDD清理（内部调用ndd_quit）
void mtpndd_quit();

// 检查MTPNDD是否已初始化
bool mtpndd_is_initialized();

#endif // MTPNDD_H