// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#define _GNU_SOURCE  // 启用GNU扩展，包括strdup
#include "mtpndd.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

// ===========================================
// 全局状态管理
// ===========================================

static struct {
    bool initialized;
    mtpndd_config_t config;
    mtpndd_stats_t stats;
    pthread_mutex_t terminal_mutex;  // 终端值操作锁
} g_mtpndd_state = {0};

// 线程本地错误状态
static __thread mtpndd_error_t g_last_mtpndd_error = MTPNDD_SUCCESS;

// ===========================================
// 多终端逻辑操作内部函数声明
// ===========================================

// 辅助函数：将终端值转换为布尔值
static bool mtpndd_terminal_to_bool(const mtpndd_terminal_t *terminal);

// 终端值AND操作：支持所有类型组合
static mtpndd_t mtpndd_and_terminals(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);

// 终端值与非终端节点的AND操作
static mtpndd_t mtpndd_and_terminal_with_node(mtpndd_t terminal, mtpndd_t node, const mtpndd_logic_config_t *config);

// 两个非终端节点的AND操作
static mtpndd_t mtpndd_and_nodes(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);

// OR操作的对应函数声明
static mtpndd_t mtpndd_or_terminals(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);
static mtpndd_t mtpndd_or_terminal_with_node(mtpndd_t terminal, mtpndd_t node, const mtpndd_logic_config_t *config);
static mtpndd_t mtpndd_or_nodes(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);

// NOT操作的对应函数声明
static mtpndd_t mtpndd_not_terminal(mtpndd_t a, const mtpndd_logic_config_t *config);
static mtpndd_t mtpndd_not_node(mtpndd_t a, const mtpndd_logic_config_t *config);

// ===========================================
// 错误处理实现
// ===========================================

const char* mtpndd_error_string(mtpndd_error_t error) {
    switch (error) {
        case MTPNDD_SUCCESS: return "Success";
        case MTPNDD_ERROR_TYPE_MISMATCH: return "Type mismatch";
        case MTPNDD_ERROR_INVALID_TERMINAL: return "Invalid terminal value";
        case MTPNDD_ERROR_STRING_TOO_LONG: return "String too long";
        case MTPNDD_ERROR_UNSUPPORTED_TYPE: return "Unsupported type";
        case MTPNDD_ERROR_CONVERSION_FAILED: return "Type conversion failed";
        default: return "Unknown MTPNDD error";
    }
}

void mtpndd_set_error(mtpndd_error_t error, const char *function, int line) {
    g_last_mtpndd_error = error;
    // 可以在这里添加日志记录
}

mtpndd_error_t mtpndd_get_last_error() {
    return g_last_mtpndd_error;
}

// ===========================================
// 从不同类型值创建MTPNDD终端节点的便捷函数
mtpndd_t mtpndd_from_integer(int64_t value) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_integer(value);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}

mtpndd_t mtpndd_from_double(double value) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_double(value);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}

mtpndd_t mtpndd_from_string(const char *str) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_string(str);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}

mtpndd_t mtpndd_from_bytes(const void *data, size_t len) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_bytes(data, len);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}


// ===========================================

static void mtpndd_terminal_destroy_value(mtpndd_terminal_t *terminal) {
    if (!terminal) return;
    
    switch (terminal->type) {
        case MTPNDD_TERMINAL_BOOLEAN:
        case MTPNDD_TERMINAL_INTEGER:
        case MTPNDD_TERMINAL_DOUBLE:
            // 基本类型，无需特殊清理
            break;
            
        case MTPNDD_TERMINAL_STRING:
            if (terminal->value.string.data) {
                free(terminal->value.string.data);
                terminal->value.string.data = NULL;
            }
            break;
            
        case MTPNDD_TERMINAL_BYTES:
            if (terminal->value.bytes.data) {
                free(terminal->value.bytes.data);
                terminal->value.bytes.data = NULL;
            }
            break;
            
        case MTPNDD_TERMINAL_CUSTOM:
            if (terminal->value.custom.destroy && terminal->value.custom.data) {
                terminal->value.custom.destroy(terminal->value.custom.data);
                terminal->value.custom.data = NULL;
            }
            break;
    }
}

// ===========================================
// 终端值创建函数实现
// ===========================================

mtpndd_terminal_t* mtpndd_terminal_boolean(bool value) {
    mtpndd_terminal_t *terminal = malloc(sizeof(mtpndd_terminal_t));
    if (!terminal) return NULL;
    
    terminal->type = MTPNDD_TERMINAL_BOOLEAN;
    terminal->value.boolean = value;
    terminal->ref_count = 1;
    
    return terminal;
}

mtpndd_terminal_t* mtpndd_terminal_integer(int64_t value) {
    mtpndd_terminal_t *terminal = malloc(sizeof(mtpndd_terminal_t));
    if (!terminal) return NULL;
    
    terminal->type = MTPNDD_TERMINAL_INTEGER;
    terminal->value.integer = value;
    terminal->ref_count = 1;
    
    return terminal;
}

mtpndd_terminal_t* mtpndd_terminal_double(double value) {
    mtpndd_terminal_t *terminal = malloc(sizeof(mtpndd_terminal_t));
    if (!terminal) return NULL;
    
    terminal->type = MTPNDD_TERMINAL_DOUBLE;
    terminal->value.floating = value;
    terminal->ref_count = 1;
    
    return terminal;
}

mtpndd_terminal_t* mtpndd_terminal_string(const char *str) {
    if (!str) return NULL;
    return mtpndd_terminal_string_n(str, strlen(str));
}

mtpndd_terminal_t* mtpndd_terminal_string_n(const char *str, size_t len) {
    if (!str || len == 0) return NULL;
    
    // 检查字符串长度限制
    if (g_mtpndd_state.initialized && len > g_mtpndd_state.config.max_string_length) {
        mtpndd_set_error(MTPNDD_ERROR_STRING_TOO_LONG, __FUNCTION__, __LINE__);
        return NULL;
    }
    
    mtpndd_terminal_t *terminal = malloc(sizeof(mtpndd_terminal_t));
    if (!terminal) return NULL;
    
    terminal->type = MTPNDD_TERMINAL_STRING;
    terminal->value.string.length = len;
    terminal->value.string.capacity = len + 1;  // +1 for null terminator
    terminal->value.string.data = malloc(terminal->value.string.capacity);
    
    if (!terminal->value.string.data) {
        free(terminal);
        return NULL;
    }
    
    memcpy(terminal->value.string.data, str, len);
    terminal->value.string.data[len] = '\0';  // 确保null终止
    terminal->ref_count = 1;
    
    return terminal;
}

mtpndd_terminal_t* mtpndd_terminal_bytes(const void *data, size_t len) {
    if (!data || len == 0) return NULL;
    
    // 检查数据长度限制
    if (g_mtpndd_state.initialized && len > g_mtpndd_state.config.max_bytes_length) {
        mtpndd_set_error(MTPNDD_ERROR_STRING_TOO_LONG, __FUNCTION__, __LINE__);
        return NULL;
    }
    
    mtpndd_terminal_t *terminal = malloc(sizeof(mtpndd_terminal_t));
    if (!terminal) return NULL;
    
    terminal->type = MTPNDD_TERMINAL_BYTES;
    terminal->value.bytes.length = len;
    terminal->value.bytes.capacity = len;
    terminal->value.bytes.data = malloc(len);
    
    if (!terminal->value.bytes.data) {
        free(terminal);
        return NULL;
    }
    
    memcpy(terminal->value.bytes.data, data, len);
    terminal->ref_count = 1;
    
    return terminal;
}

mtpndd_terminal_t* mtpndd_terminal_custom(
    void *data, 
    size_t size,
    int (*compare)(const void *a, const void *b),
    void* (*copy)(const void *data),
    void (*destroy)(void *data),
    char* (*to_string)(const void *data)
) {
    if (!data || size == 0 || !compare || !copy || !destroy) return NULL;
    
    mtpndd_terminal_t *terminal = malloc(sizeof(mtpndd_terminal_t));
    if (!terminal) return NULL;
    
    terminal->type = MTPNDD_TERMINAL_CUSTOM;
    terminal->value.custom.data = copy(data);  // 使用提供的copy函数
    terminal->value.custom.size = size;
    terminal->value.custom.compare = compare;
    terminal->value.custom.copy = copy;
    terminal->value.custom.destroy = destroy;
    terminal->value.custom.to_string = to_string;
    terminal->ref_count = 1;
    
    if (!terminal->value.custom.data) {
        free(terminal);
        return NULL;
    }
    
    return terminal;
}

// ===========================================
// 终端值引用计数管理（线程安全）
// ===========================================

mtpndd_terminal_t* mtpndd_terminal_ref(mtpndd_terminal_t *terminal) {
    if (!terminal) return NULL;
    
    pthread_mutex_lock(&g_mtpndd_state.terminal_mutex);
    terminal->ref_count++;
    pthread_mutex_unlock(&g_mtpndd_state.terminal_mutex);
    
    return terminal;
}

void mtpndd_terminal_deref(mtpndd_terminal_t *terminal) {
    if (!terminal) return;
    
    pthread_mutex_lock(&g_mtpndd_state.terminal_mutex);
    terminal->ref_count--;
    bool should_destroy = (terminal->ref_count == 0);
    pthread_mutex_unlock(&g_mtpndd_state.terminal_mutex);
    
    if (should_destroy) {
        mtpndd_terminal_destroy_value(terminal);
        free(terminal);
    }
}

int mtpndd_terminal_compare(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    // 首先比较类型
    if (a->type != b->type) {
        return (int)a->type - (int)b->type;
    }
    
    // 相同类型，比较值
    switch (a->type) {
        case MTPNDD_TERMINAL_BOOLEAN:
            return (int)a->value.boolean - (int)b->value.boolean;
            
        case MTPNDD_TERMINAL_INTEGER:
            if (a->value.integer < b->value.integer) return -1;
            if (a->value.integer > b->value.integer) return 1;
            return 0;
            
        case MTPNDD_TERMINAL_DOUBLE:
            if (a->value.floating < b->value.floating) return -1;
            if (a->value.floating > b->value.floating) return 1;
            return 0;
            
        case MTPNDD_TERMINAL_STRING:
            return strcmp(a->value.string.data, b->value.string.data);
            
        case MTPNDD_TERMINAL_BYTES:
            if (a->value.bytes.length != b->value.bytes.length) {
                return (int)a->value.bytes.length - (int)b->value.bytes.length;
            }
            return memcmp(a->value.bytes.data, b->value.bytes.data, a->value.bytes.length);
            
        case MTPNDD_TERMINAL_CUSTOM:
            if (a->value.custom.compare) {
                return a->value.custom.compare(a->value.custom.data, b->value.custom.data);
            }
            return 0;  // 无法比较
    }
    
    return 0;
}

mtpndd_terminal_t* mtpndd_terminal_copy(const mtpndd_terminal_t *terminal) {
    if (!terminal) return NULL;
    
    mtpndd_terminal_t *copy = malloc(sizeof(mtpndd_terminal_t));
    if (!copy) return NULL;
    
    copy->type = terminal->type;
    copy->ref_count = 1;
    
    switch (terminal->type) {
        case MTPNDD_TERMINAL_BOOLEAN:
            copy->value.boolean = terminal->value.boolean;
            break;
            
        case MTPNDD_TERMINAL_INTEGER:
            copy->value.integer = terminal->value.integer;
            break;
            
        case MTPNDD_TERMINAL_DOUBLE:
            copy->value.floating = terminal->value.floating;
            break;
            
        case MTPNDD_TERMINAL_STRING:
            copy->value.string.length = terminal->value.string.length;
            copy->value.string.capacity = terminal->value.string.capacity;
            copy->value.string.data = malloc(copy->value.string.capacity);
            if (!copy->value.string.data) {
                free(copy);
                return NULL;
            }
            memcpy(copy->value.string.data, terminal->value.string.data, copy->value.string.length + 1);
            break;
            
        case MTPNDD_TERMINAL_BYTES:
            copy->value.bytes.length = terminal->value.bytes.length;
            copy->value.bytes.capacity = terminal->value.bytes.capacity;
            copy->value.bytes.data = malloc(copy->value.bytes.capacity);
            if (!copy->value.bytes.data) {
                free(copy);
                return NULL;
            }
            memcpy(copy->value.bytes.data, terminal->value.bytes.data, copy->value.bytes.length);
            break;
            
        case MTPNDD_TERMINAL_CUSTOM:
            if (terminal->value.custom.copy) {
                copy->value.custom.data = terminal->value.custom.copy(terminal->value.custom.data);
                if (!copy->value.custom.data) {
                    free(copy);
                    return NULL;
                }
            } else {
                copy->value.custom.data = NULL;
            }
            copy->value.custom.size = terminal->value.custom.size;
            copy->value.custom.compare = terminal->value.custom.compare;
            copy->value.custom.copy = terminal->value.custom.copy;
            copy->value.custom.destroy = terminal->value.custom.destroy;
            copy->value.custom.to_string = terminal->value.custom.to_string;
            break;
    }
    
    return copy;
}

// ===========================================
// 向后兼容的便捷函数
// ===========================================

mtpndd_t mtpndd_from_boolean(bool value) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_boolean(value);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    return mtpndd_create_terminal(terminal);
}

// ===========================================
// MTPNDD节点创建
// ===========================================

mtpndd_t mtpndd_create_terminal(mtpndd_terminal_t *terminal) {
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    mtpndd_node_t *node = malloc(sizeof(mtpndd_node_t));
    if (!node) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 初始化基础NDD字段
    node->field = 0;  // 终端节点没有字段
    node->edges = NULL;
    node->edge_count = 0;
    node->edge_capacity = 0;
    node->ref_count = 1;
    node->is_terminal = true;
    
    // 初始化MTPNDD字段
    node->terminal = mtpndd_terminal_ref(terminal);  // 增加引用计数
    node->is_multiterminal = true;
    
    mtpndd_t result;
    result.node = node;
    result.is_terminal = true;
    result.is_multiterminal = true;
    
    return result;
}

mtpndd_t mtpndd_create_node(uint32_t field) {
    // 创建兼容NDD的普通节点
    ndd_t ndd_node = ndd_create_node(field);
    return ndd_to_mtpndd(ndd_node);
}

// ===========================================
// 类型检查函数
// ===========================================

bool mtpndd_is_terminal(mtpndd_t mtpndd) {
    return mtpndd.is_terminal;
}

bool mtpndd_is_multiterminal(mtpndd_t mtpndd) {
    return mtpndd.is_multiterminal;
}

bool mtpndd_is_boolean_terminal(mtpndd_t mtpndd) {
    if (!mtpndd.is_terminal || !mtpndd.is_multiterminal || !mtpndd.node || !mtpndd.node->terminal) {
        return false;
    }
    return mtpndd.node->terminal->type == MTPNDD_TERMINAL_BOOLEAN;
}

bool mtpndd_is_integer_terminal(mtpndd_t mtpndd) {
    if (!mtpndd.is_terminal || !mtpndd.is_multiterminal || !mtpndd.node || !mtpndd.node->terminal) {
        return false;
    }
    return mtpndd.node->terminal->type == MTPNDD_TERMINAL_INTEGER;
}

bool mtpndd_is_double_terminal(mtpndd_t mtpndd) {
    if (!mtpndd.is_terminal || !mtpndd.is_multiterminal || !mtpndd.node || !mtpndd.node->terminal) {
        return false;
    }
    return mtpndd.node->terminal->type == MTPNDD_TERMINAL_DOUBLE;
}

bool mtpndd_is_string_terminal(mtpndd_t mtpndd) {
    if (!mtpndd.is_terminal || !mtpndd.is_multiterminal || !mtpndd.node || !mtpndd.node->terminal) {
        return false;
    }
    return mtpndd.node->terminal->type == MTPNDD_TERMINAL_STRING;
}

// ===========================================
// 值获取函数（带类型检查）
// ===========================================

bool mtpndd_get_boolean(mtpndd_t mtpndd) {
    if (!mtpndd_is_boolean_terminal(mtpndd)) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return false;
    }
    return mtpndd.node->terminal->value.boolean;
}

int64_t mtpndd_get_integer(mtpndd_t mtpndd) {
    if (!mtpndd_is_integer_terminal(mtpndd)) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return 0;
    }
    return mtpndd.node->terminal->value.integer;
}

double mtpndd_get_double(mtpndd_t mtpndd) {
    if (!mtpndd_is_double_terminal(mtpndd)) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return 0.0;
    }
    return mtpndd.node->terminal->value.floating;
}

const char* mtpndd_get_string(mtpndd_t mtpndd) {
    if (!mtpndd_is_string_terminal(mtpndd)) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return NULL;
    }
    return mtpndd.node->terminal->value.string.data;
}

const void* mtpndd_get_bytes(mtpndd_t mtpndd, size_t *length) {
    if (!mtpndd.is_terminal || !mtpndd.is_multiterminal || !mtpndd.node || !mtpndd.node->terminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return NULL;
    }
    if (mtpndd.node->terminal->type != MTPNDD_TERMINAL_BYTES) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return NULL;
    }
    if (length) {
        *length = mtpndd.node->terminal->value.bytes.length;
    }
    return mtpndd.node->terminal->value.bytes.data;
}

const void* mtpndd_get_custom(mtpndd_t mtpndd, size_t *size) {
    if (!mtpndd.is_terminal || !mtpndd.is_multiterminal || !mtpndd.node || !mtpndd.node->terminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return NULL;
    }
    if (mtpndd.node->terminal->type != MTPNDD_TERMINAL_CUSTOM) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return NULL;
    }
    if (size) {
        *size = mtpndd.node->terminal->value.custom.size;
    }
    return mtpndd.node->terminal->value.custom.data;
}

// ===========================================
// 高级节点创建操作实现
// ===========================================

mtpndd_t mtpndd_create_integer_node(int64_t value) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_integer(value);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}

mtpndd_t mtpndd_create_double_node(double value) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_double(value);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}

mtpndd_t mtpndd_create_string_node(const char *str) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_string(str);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}

mtpndd_t mtpndd_create_bytes_node(const void *data, size_t len) {
    mtpndd_terminal_t *terminal = mtpndd_terminal_bytes(data, len);
    if (!terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    return mtpndd_create_terminal(terminal);
}

int mtpndd_create_terminals_batch(const mtpndd_batch_create_t *specs, size_t count, mtpndd_t *results) {
    if (!specs || !results || count == 0) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return -1;
    }
    
    int created = 0;
    for (size_t i = 0; i < count; i++) {
        mtpndd_terminal_t *terminal = NULL;
        
        switch (specs[i].type) {
            case MTPNDD_TERMINAL_BOOLEAN:
                terminal = mtpndd_terminal_boolean(specs[i].value.boolean);
                break;
            case MTPNDD_TERMINAL_INTEGER:
                terminal = mtpndd_terminal_integer(specs[i].value.integer);
                break;
            case MTPNDD_TERMINAL_DOUBLE:
                terminal = mtpndd_terminal_double(specs[i].value.floating);
                break;
            case MTPNDD_TERMINAL_STRING:
                terminal = mtpndd_terminal_string(specs[i].value.string.data);
                break;
            case MTPNDD_TERMINAL_BYTES:
                terminal = mtpndd_terminal_bytes(specs[i].value.bytes.data, specs[i].value.bytes.length);
                break;
            default:
                mtpndd_set_error(MTPNDD_ERROR_UNSUPPORTED_TYPE, __FUNCTION__, __LINE__);
                goto cleanup;
        }
        
        if (!terminal) {
            goto cleanup;
        }
        
        results[i] = mtpndd_create_terminal(terminal);
        if (!results[i].node) {
            mtpndd_terminal_deref(terminal);
            goto cleanup;
        }
        created++;
    }
    
    return created;
    
cleanup:
    // 清理已创建的节点
    for (int j = 0; j < created; j++) {
        mtpndd_deref(results[j]);
    }
    return -1;
}

mtpndd_t mtpndd_copy(mtpndd_t source) {
    if (!source.node) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    if (source.is_multiterminal && source.is_terminal && source.node->terminal) {
        // 深度复制终端节点
        mtpndd_terminal_t *terminal_copy = mtpndd_terminal_copy(source.node->terminal);
        if (!terminal_copy) {
            mtpndd_t null_result = {NULL, false, false};
            return null_result;
        }
        return mtpndd_create_terminal(terminal_copy);
    } else {
        // 深度复制普通节点（这里简化为浅复制）
        return mtpndd_clone(source);
    }
}

mtpndd_t mtpndd_clone(mtpndd_t source) {
    if (!source.node) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 浅复制，共享同一个节点
    return mtpndd_ref(source);
}

// ===========================================
// 节点访问和查询操作实现
// ===========================================

uint32_t mtpndd_get_field(mtpndd_t mtpndd) {
    if (!mtpndd.node) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return 0;
    }
    return mtpndd.node->field;
}

uint32_t mtpndd_get_edge_count(mtpndd_t mtpndd) {
    if (!mtpndd.node) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return 0;
    }
    return mtpndd.node->edge_count;
}

uint32_t mtpndd_get_ref_count(mtpndd_t mtpndd) {
    if (!mtpndd.node) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return 0;
    }
    return mtpndd.node->ref_count;
}

mtpndd_terminal_type_t mtpndd_get_terminal_type(mtpndd_t mtpndd) {
    if (!mtpndd.is_terminal || !mtpndd.is_multiterminal || !mtpndd.node || !mtpndd.node->terminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return MTPNDD_TERMINAL_BOOLEAN;  // 默认返回
    }
    return mtpndd.node->terminal->type;
}

mtpndd_edge_t mtpndd_get_edge(mtpndd_t mtpndd, uint32_t index) {
    mtpndd_edge_t invalid_edge = {0, {NULL, false, false}, false};
    
    if (!mtpndd.node || index >= mtpndd.node->edge_count) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return invalid_edge;
    }
    
    mtpndd_edge_t edge;
    edge.label_bdd = mtpndd.node->edges[index];
    edge.target = mtpndd_true();  // 简化实现，实际需要从边数据结构获取
    edge.is_valid = true;
    
    return edge;
}

int mtpndd_get_all_edges(mtpndd_t mtpndd, mtpndd_edge_t *edges, uint32_t max_count) {
    if (!mtpndd.node || !edges) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return -1;
    }
    
    uint32_t count = (mtpndd.node->edge_count < max_count) ? mtpndd.node->edge_count : max_count;
    
    for (uint32_t i = 0; i < count; i++) {
        edges[i] = mtpndd_get_edge(mtpndd, i);
    }
    
    return (int)count;
}

mtpndd_edge_t mtpndd_find_edge_by_label(mtpndd_t mtpndd, ndd_bdd_t label_bdd) {
    mtpndd_edge_t invalid_edge = {0, {NULL, false, false}, false};
    
    if (!mtpndd.node) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return invalid_edge;
    }
    
    for (uint32_t i = 0; i < mtpndd.node->edge_count; i++) {
        if (mtpndd.node->edges[i] == label_bdd) {
            return mtpndd_get_edge(mtpndd, i);
        }
    }
    
    return invalid_edge;
}

uint32_t mtpndd_get_depth(mtpndd_t root) {
    if (!root.node || root.is_terminal) {
        return 0;
    }
    
    uint32_t max_depth = 0;
    for (uint32_t i = 0; i < root.node->edge_count; i++) {
        // 简化实现，实际需要递归遍历后继节点
        max_depth = 1;  // 暂时返回1
        break;
    }
    
    return max_depth;
}

uint32_t mtpndd_count_nodes(mtpndd_t root) {
    if (!root.node) {
        return 0;
    }
    
    // 简化实现，实际需要DFS/BFS遍历
    return 1;
}

uint32_t mtpndd_count_terminals(mtpndd_t root) {
    if (!root.node) {
        return 0;
    }
    
    if (root.is_terminal) {
        return 1;
    }
    
    // 简化实现
    return 0;
}

// ===========================================
// 节点修改操作实现
// ===========================================

int mtpndd_set_terminal_boolean(mtpndd_t *mtpndd, bool value) {
    if (!mtpndd || !mtpndd->node || !mtpndd->is_terminal || !mtpndd->is_multiterminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    if (!mtpndd->node->terminal || mtpndd->node->terminal->type != MTPNDD_TERMINAL_BOOLEAN) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    mtpndd->node->terminal->value.boolean = value;
    return 0;
}

int mtpndd_set_terminal_integer(mtpndd_t *mtpndd, int64_t value) {
    if (!mtpndd || !mtpndd->node || !mtpndd->is_terminal || !mtpndd->is_multiterminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    if (!mtpndd->node->terminal || mtpndd->node->terminal->type != MTPNDD_TERMINAL_INTEGER) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    mtpndd->node->terminal->value.integer = value;
    return 0;
}

int mtpndd_set_terminal_double(mtpndd_t *mtpndd, double value) {
    if (!mtpndd || !mtpndd->node || !mtpndd->is_terminal || !mtpndd->is_multiterminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    if (!mtpndd->node->terminal || mtpndd->node->terminal->type != MTPNDD_TERMINAL_DOUBLE) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    mtpndd->node->terminal->value.floating = value;
    return 0;
}

int mtpndd_set_terminal_string(mtpndd_t *mtpndd, const char *str) {
    if (!mtpndd || !mtpndd->node || !mtpndd->is_terminal || !mtpndd->is_multiterminal || !str) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    if (!mtpndd->node->terminal || mtpndd->node->terminal->type != MTPNDD_TERMINAL_STRING) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    size_t new_len = strlen(str);
    
    // 检查是否需要重新分配内存
    if (new_len + 1 > mtpndd->node->terminal->value.string.capacity) {
        char *new_data = realloc(mtpndd->node->terminal->value.string.data, new_len + 1);
        if (!new_data) {
            mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
            return -1;
        }
        mtpndd->node->terminal->value.string.data = new_data;
        mtpndd->node->terminal->value.string.capacity = new_len + 1;
    }
    
    strcpy(mtpndd->node->terminal->value.string.data, str);
    mtpndd->node->terminal->value.string.length = new_len;
    
    return 0;
}

int mtpndd_set_terminal_bytes(mtpndd_t *mtpndd, const void *data, size_t len) {
    if (!mtpndd || !mtpndd->node || !mtpndd->is_terminal || !mtpndd->is_multiterminal || !data || len == 0) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    if (!mtpndd->node->terminal || mtpndd->node->terminal->type != MTPNDD_TERMINAL_BYTES) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 检查是否需要重新分配内存
    if (len > mtpndd->node->terminal->value.bytes.capacity) {
        void *new_data = realloc(mtpndd->node->terminal->value.bytes.data, len);
        if (!new_data) {
            mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
            return -1;
        }
        mtpndd->node->terminal->value.bytes.data = new_data;
        mtpndd->node->terminal->value.bytes.capacity = len;
    }
    
    memcpy(mtpndd->node->terminal->value.bytes.data, data, len);
    mtpndd->node->terminal->value.bytes.length = len;
    
    return 0;
}

int mtpndd_replace_terminal(mtpndd_t *mtpndd, mtpndd_terminal_t *new_terminal) {
    if (!mtpndd || !mtpndd->node || !mtpndd->is_terminal || !mtpndd->is_multiterminal || !new_terminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 释放旧的终端值
    if (mtpndd->node->terminal) {
        mtpndd_terminal_deref(mtpndd->node->terminal);
    }
    
    // 设置新的终端值
    mtpndd->node->terminal = mtpndd_terminal_ref(new_terminal);
    
    return 0;
}

int mtpndd_add_edge_ex(mtpndd_t *mtpndd, mtpndd_t descendant, ndd_bdd_t label_bdd) {
    if (!mtpndd || !mtpndd->node || mtpndd->is_terminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 检查是否需要扩容边数组
    if (mtpndd->node->edge_count >= mtpndd->node->edge_capacity) {
        uint32_t new_capacity = mtpndd->node->edge_capacity == 0 ? 4 : mtpndd->node->edge_capacity * 2;
        ndd_bdd_t *new_edges = realloc(mtpndd->node->edges, new_capacity * sizeof(ndd_bdd_t));
        if (!new_edges) {
            mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
            return -1;
        }
        mtpndd->node->edges = new_edges;
        mtpndd->node->edge_capacity = new_capacity;
    }
    
    // 添加新边
    mtpndd->node->edges[mtpndd->node->edge_count] = label_bdd;
    mtpndd->node->edge_count++;
    
    // 增加后继节点的引用计数
    mtpndd_ref(descendant);
    
    return 0;
}

int mtpndd_remove_edge(mtpndd_t *mtpndd, uint32_t edge_index) {
    if (!mtpndd || !mtpndd->node || mtpndd->is_terminal || edge_index >= mtpndd->node->edge_count) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 移动后面的边向前
    for (uint32_t i = edge_index; i < mtpndd->node->edge_count - 1; i++) {
        mtpndd->node->edges[i] = mtpndd->node->edges[i + 1];
    }
    
    mtpndd->node->edge_count--;
    
    return 0;
}

int mtpndd_clear_edges(mtpndd_t *mtpndd) {
    if (!mtpndd || !mtpndd->node || mtpndd->is_terminal) {
        mtpndd_set_error(MTPNDD_ERROR_TYPE_MISMATCH, __FUNCTION__, __LINE__);
        return -1;
    }
    
    mtpndd->node->edge_count = 0;
    
    return 0;
}

// ===========================================
// MTPNDD引用计数
// ===========================================

mtpndd_t mtpndd_ref(mtpndd_t mtpndd) {
    if (mtpndd.node) {
        // 对于多终端节点，需要同时管理节点和终端值的引用
        if (mtpndd.is_multiterminal) {
            pthread_mutex_lock(&g_mtpndd_state.terminal_mutex);
            mtpndd.node->ref_count++;
            pthread_mutex_unlock(&g_mtpndd_state.terminal_mutex);
        } else {
            // 普通NDD节点，使用现有的引用计数
            ndd_t ndd = mtpndd_to_ndd(mtpndd);
            ndd = ndd_ref(ndd);
            mtpndd = ndd_to_mtpndd(ndd);
        }
    }
    return mtpndd;
}

void mtpndd_deref(mtpndd_t mtpndd) {
    if (!mtpndd.node) return;
    
    if (mtpndd.is_multiterminal) {
        pthread_mutex_lock(&g_mtpndd_state.terminal_mutex);
        mtpndd.node->ref_count--;
        bool should_destroy = (mtpndd.node->ref_count == 0);
        pthread_mutex_unlock(&g_mtpndd_state.terminal_mutex);
        
        if (should_destroy) {
            if (mtpndd.node->terminal) {
                mtpndd_terminal_deref(mtpndd.node->terminal);
            }
            free(mtpndd.node);
        }
    } else {
        // 普通NDD节点，使用现有的引用计数
        ndd_t ndd = mtpndd_to_ndd(mtpndd);
        ndd_deref(ndd);
    }
}

// ===========================================
// 初始化和配置
// ===========================================

int mtpndd_init(mtpndd_config_t *config) {
    if (g_mtpndd_state.initialized) {
        return 0;  // 已经初始化
    }
    
    // 首先初始化基础NDD系统
    int result = ndd_init(&config->base);
    if (result != 0) {
        return result;
    }
    
    // 初始化MTPNDD特定配置
    g_mtpndd_state.config = *config;
    
    // 设置默认值
    if (g_mtpndd_state.config.max_string_length == 0) {
        g_mtpndd_state.config.max_string_length = 1024 * 1024;  // 1MB
    }
    if (g_mtpndd_state.config.max_bytes_length == 0) {
        g_mtpndd_state.config.max_bytes_length = 10 * 1024 * 1024;  // 10MB
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&g_mtpndd_state.terminal_mutex, NULL) != 0) {
        ndd_quit();
        return -1;
    }
    
    // 清空统计信息
    memset(&g_mtpndd_state.stats, 0, sizeof(mtpndd_stats_t));
    g_mtpndd_state.stats.base = ndd_get_stats();
    
    g_mtpndd_state.initialized = true;
    return 0;
}

void mtpndd_quit() {
    if (!g_mtpndd_state.initialized) {
        return;
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&g_mtpndd_state.terminal_mutex);
    
    // 清理基础NDD系统
    ndd_quit();
    
    g_mtpndd_state.initialized = false;
}

bool mtpndd_is_initialized() {
    return g_mtpndd_state.initialized && ndd_is_initialized();
}

// ===========================================
// 统计信息
// ===========================================

mtpndd_stats_t mtpndd_get_stats() {
    if (g_mtpndd_state.initialized) {
        g_mtpndd_state.stats.base = ndd_get_stats();
    }
    return g_mtpndd_state.stats;
}

void mtpndd_print_stats() {
    mtpndd_stats_t stats = mtpndd_get_stats();
    
    printf("=== MTPNDD Statistics ===\n");
    printf("Terminal values: %lu\n", stats.terminal_count);
    printf("  - Boolean: %lu\n", stats.terminal_count - stats.string_count - 
           stats.integer_count - stats.double_count - stats.bytes_count - stats.custom_count);
    printf("  - Integer: %lu\n", stats.integer_count);
    printf("  - Double: %lu\n", stats.double_count);
    printf("  - String: %lu\n", stats.string_count);
    printf("  - Bytes: %lu\n", stats.bytes_count);
    printf("  - Custom: %lu\n", stats.custom_count);
    printf("Multi-terminal nodes: %lu\n", stats.multiterminal_count);
    
    printf("\n=== Base NDD Statistics ===\n");
    ndd_print_stats();
}

// ===========================================
// MTPNDD编码/解码操作实现
// ===========================================

// 前缀编码操作（支持多终端值）
mtpndd_t mtpndd_encode_prefix(uint32_t* prefix_binary, uint32_t len, uint32_t field) {
    if (!prefix_binary || len == 0) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 创建基础NDD编码
    ndd_t ndd_result = ndd_encode_prefix(prefix_binary, len, field);
    
    // 转换为MTPNDD，默认使用布尔终端值
    return ndd_to_mtpndd(ndd_result);
}

// 前缀编码操作（带指定终端值）
mtpndd_t mtpndd_encode_prefix_with_terminal(uint32_t* prefix_binary, uint32_t len, 
                                          uint32_t field, mtpndd_terminal_t *terminal) {
    if (!prefix_binary || len == 0 || !terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    ndd_field_info_t *field_info = ndd_get_field_info(field);
    if (!field_info) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 创建多终端节点
    mtpndd_t result = mtpndd_create_node(field);
    
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
    }
#else
    // 简化模式：使用基本位运算模拟
    for (uint32_t i = 0; i < len && i < field_info->bit_width; i++) {
        if (prefix_binary[i] == 1) {
            prefix_bdd |= (1ULL << i);
        }
    }
#endif
    
    // 创建指向指定终端值的终端节点
    mtpndd_t terminal_node = mtpndd_create_terminal(terminal);
    
    // 添加边
    mtpndd_add_edge_ex(&result, terminal_node, prefix_bdd);
    
    return result;
}

// BDD转换操作（扩展支持多终端值）
// 函数声明（提前声明解决顺序问题）
static mtpndd_t mtpndd_from_bdd_with_terminals(ndd_bdd_t bdd, uint32_t field, 
                                       mtpndd_terminal_t *true_terminal, 
                                       mtpndd_terminal_t *false_terminal);

mtpndd_t mtpndd_from_bdd(ndd_bdd_t bdd, uint32_t field) {
    // 使用默认的布尔终端值
    mtpndd_terminal_t *true_terminal = mtpndd_terminal_boolean(true);
    mtpndd_terminal_t *false_terminal = mtpndd_terminal_boolean(false);
    
    mtpndd_t result = mtpndd_from_bdd_with_terminals(bdd, field, true_terminal, false_terminal);
    
    mtpndd_terminal_deref(true_terminal);
    mtpndd_terminal_deref(false_terminal);
    
    return result;
}

static mtpndd_t mtpndd_from_bdd_with_terminals(ndd_bdd_t bdd, uint32_t field, 
                                       mtpndd_terminal_t *true_terminal, 
                                       mtpndd_terminal_t *false_terminal) {
    if (!true_terminal || !false_terminal) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 特殊情况处理
    if (bdd == ndd_sylvan_true) {
        return mtpndd_create_terminal(true_terminal);
    }
    if (bdd == ndd_sylvan_false) {
        return mtpndd_create_terminal(false_terminal);
    }
    
    ndd_field_info_t *field_info = ndd_get_field_info(field);
    if (!field_info) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 创建多终端节点
    mtpndd_t result = mtpndd_create_node(field);
    
    // 简化实现：直接使用BDD值作为边标签
    mtpndd_t true_node = mtpndd_create_terminal(true_terminal);
    mtpndd_add_edge_ex(&result, true_node, bdd);
    
    if (bdd != ndd_sylvan_true) {
        mtpndd_t false_node = mtpndd_create_terminal(false_terminal);
        mtpndd_add_edge_ex(&result, false_node, ~bdd);  // 取反
    }
    
    return result;
}

// MTPNDD转BDD操作（仅对布尔终端有效）
ndd_bdd_t mtpndd_to_bdd(mtpndd_t mtpndd) {
    if (!mtpndd.node) {
        return ndd_sylvan_false;
    }
    
    // 如果是布尔终端节点，直接返回对应的BDD值
    if (mtpndd_is_boolean_terminal(mtpndd)) {
        bool value = mtpndd_get_boolean(mtpndd);
        return value ? ndd_sylvan_true : ndd_sylvan_false;
    }
    
    // 非终端节点，需要遍历边并合成BDD
    ndd_bdd_t result = ndd_sylvan_false;
    
    for (uint32_t i = 0; i < mtpndd.node->edge_count; i++) {
        ndd_bdd_t edge_bdd = mtpndd.node->edges[i];
#ifdef HAVE_LACE_SYLVAN
        result = sylvan_or(result, edge_bdd);
#else
        result |= edge_bdd;  // 简化模式使用位运算
#endif
    }
    
    return result;
}

// ===========================================
// MTPNDD多终端逻辑操作实现
// ===========================================

// 全局逻辑操作统计
static mtpndd_logic_stats_t g_mtpndd_logic_stats = {0};

// 终端值兼容性检查
bool mtpndd_terminals_compatible(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b) {
    if (!a || !b) return false;
    
    // 相同类型直接兼容
    if (a->type == b->type) return true;
    
    // 数值类型之间可以兼容
    if ((a->type == MTPNDD_TERMINAL_INTEGER && b->type == MTPNDD_TERMINAL_DOUBLE) ||
        (a->type == MTPNDD_TERMINAL_DOUBLE && b->type == MTPNDD_TERMINAL_INTEGER)) {
        return true;
    }
    
    return false;
}

// 创建默认逻辑配置
mtpndd_logic_config_t mtpndd_create_default_logic_config(mtpndd_terminal_operation_strategy_t strategy) {
    mtpndd_logic_config_t config = {
        .strategy = strategy,
        .user_data = NULL,
        .auto_type_promotion = true,
        .preserve_type_safety = true
    };
    return config;
}

// 基础多终端逻辑操作
mtpndd_t mtpndd_and(mtpndd_t a, mtpndd_t b) {
    mtpndd_logic_config_t config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_BOOLEAN);
    return mtpndd_and_with_config(a, b, &config);
}

mtpndd_t mtpndd_or(mtpndd_t a, mtpndd_t b) {
    mtpndd_logic_config_t config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_BOOLEAN);
    return mtpndd_or_with_config(a, b, &config);
}

mtpndd_t mtpndd_not(mtpndd_t a) {
    mtpndd_logic_config_t config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_BOOLEAN);
    return mtpndd_not_with_config(a, &config);
}

// 获取逻辑运算统计信息
mtpndd_logic_stats_t mtpndd_get_logic_stats() {
    return g_mtpndd_logic_stats;
}

void mtpndd_reset_logic_stats() {
    memset(&g_mtpndd_logic_stats, 0, sizeof(mtpndd_logic_stats_t));
}

void mtpndd_print_logic_stats() {
    printf("=== MTPNDD Logic Operation Statistics ===\n");
    printf("AND operations: %lu\n", g_mtpndd_logic_stats.and_operations);
    printf("OR operations: %lu\n", g_mtpndd_logic_stats.or_operations);
    printf("NOT operations: %lu\n", g_mtpndd_logic_stats.not_operations);
    printf("ITE operations: %lu\n", g_mtpndd_logic_stats.ite_operations);
    printf("Terminal conversions: %lu\n", g_mtpndd_logic_stats.terminal_conversions);
    printf("Type promotions: %lu\n", g_mtpndd_logic_stats.type_promotions);
    printf("Cache hits: %lu\n", g_mtpndd_logic_stats.cache_hits);
    printf("Cache misses: %lu\n", g_mtpndd_logic_stats.cache_misses);
    printf("Average operation time: %.6f ms\n", g_mtpndd_logic_stats.average_operation_time);
}

// 配置化的多终端逻辑操作实现
mtpndd_t mtpndd_and_with_config(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config) {
    if (!a.node || !b.node || !config) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    g_mtpndd_logic_stats.and_operations++;
    
    // 统一终端值处理：支持所有类型组合
    if (mtpndd_is_terminal(a) && mtpndd_is_terminal(b)) {
        return mtpndd_and_terminals(a, b, config);
    }
    
    // 一个是终端，一个不是：需要处理边操作
    if (mtpndd_is_terminal(a) && !mtpndd_is_terminal(b)) {
        return mtpndd_and_terminal_with_node(a, b, config);
    }
    
    if (!mtpndd_is_terminal(a) && mtpndd_is_terminal(b)) {
        return mtpndd_and_terminal_with_node(b, a, config);
    }
    
    // 两个都是非终端节点：处理结构性AND操作
    return mtpndd_and_nodes(a, b, config);
}

mtpndd_t mtpndd_or_with_config(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config) {
    if (!a.node || !b.node || !config) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    g_mtpndd_logic_stats.or_operations++;
    
    // 统一终端值处理：支持所有类型组合
    if (mtpndd_is_terminal(a) && mtpndd_is_terminal(b)) {
        return mtpndd_or_terminals(a, b, config);
    }
    
    // 一个是终端，一个不是：需要处理边操作
    if (mtpndd_is_terminal(a) && !mtpndd_is_terminal(b)) {
        return mtpndd_or_terminal_with_node(a, b, config);
    }
    
    if (!mtpndd_is_terminal(a) && mtpndd_is_terminal(b)) {
        return mtpndd_or_terminal_with_node(b, a, config);
    }
    
    // 两个都是非终端节点：处理结构性OR操作
    return mtpndd_or_nodes(a, b, config);
}

mtpndd_t mtpndd_not_with_config(mtpndd_t a, const mtpndd_logic_config_t *config) {
    if (!a.node || !config) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    g_mtpndd_logic_stats.not_operations++;
    
    // 统一终端值处理：支持所有终端类型
    if (mtpndd_is_terminal(a)) {
        return mtpndd_not_terminal(a, config);
    }
    
    // 非终端节点处理
    return mtpndd_not_node(a, config);
}

// 终端值AND操作：支持所有类型组合
static mtpndd_t mtpndd_and_terminals(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config) {
    mtpndd_terminal_t *term_a = a.node->terminal;
    mtpndd_terminal_t *term_b = b.node->terminal;
    
    // 布尔类型组合
    if (term_a->type == MTPNDD_TERMINAL_BOOLEAN && term_b->type == MTPNDD_TERMINAL_BOOLEAN) {
        bool result = term_a->value.boolean && term_b->value.boolean;
        return mtpndd_from_boolean(result);
    }
    
    // 整数类型组合
    if (term_a->type == MTPNDD_TERMINAL_INTEGER && term_b->type == MTPNDD_TERMINAL_INTEGER) {
        int64_t val_a = term_a->value.integer;
        int64_t val_b = term_b->value.integer;
        int64_t result;
        
        switch (config->strategy) {
            case MTPNDD_STRATEGY_NUMERIC_MIN:
                result = (val_a < val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MAX:
                result = (val_a > val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_ADD:
                result = val_a + val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MUL:
                result = val_a * val_b;
                break;
            case MTPNDD_STRATEGY_BITWISE:
                result = val_a & val_b; // 按位AND
                break;
            default: // MTPNDD_STRATEGY_BOOLEAN
                result = (val_a && val_b) ? 1 : 0;
                break;
        }
        
        return mtpndd_create_integer_node(result);
    }
    
    // 浮点数类型组合
    if (term_a->type == MTPNDD_TERMINAL_DOUBLE && term_b->type == MTPNDD_TERMINAL_DOUBLE) {
        double val_a = term_a->value.floating;
        double val_b = term_b->value.floating;
        double result;
        
        switch (config->strategy) {
            case MTPNDD_STRATEGY_NUMERIC_MIN:
                result = (val_a < val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MAX:
                result = (val_a > val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_ADD:
                result = val_a + val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MUL:
                result = val_a * val_b;
                break;
            default: // MTPNDD_STRATEGY_BOOLEAN
                result = (val_a != 0.0 && val_b != 0.0) ? 1.0 : 0.0;
                break;
        }
        
        return mtpndd_create_double_node(result);
    }
    
    // 字符串类型组合
    if (term_a->type == MTPNDD_TERMINAL_STRING && term_b->type == MTPNDD_TERMINAL_STRING) {
        if (config->strategy == MTPNDD_STRATEGY_STRING_CONCAT) {
            // 字符串连接
            size_t total_len = term_a->value.string.length + term_b->value.string.length;
            char *concat_str = malloc(total_len + 1);
            if (!concat_str) {
                mtpndd_t null_result = {NULL, false, false};
                return null_result;
            }
            
            strcpy(concat_str, term_a->value.string.data);
            strcat(concat_str, term_b->value.string.data);
            
            mtpndd_t result = mtpndd_from_string(concat_str);
            free(concat_str);
            return result;
        } else {
            // 布尔逻辑：两个字符串都非空则为true
            bool a_nonempty = (term_a->value.string.length > 0);
            bool b_nonempty = (term_b->value.string.length > 0);
            return mtpndd_from_boolean(a_nonempty && b_nonempty);
        }
    }
    
    // 混合类型：整数和浮点数
    if ((term_a->type == MTPNDD_TERMINAL_INTEGER && term_b->type == MTPNDD_TERMINAL_DOUBLE) ||
        (term_a->type == MTPNDD_TERMINAL_DOUBLE && term_b->type == MTPNDD_TERMINAL_INTEGER)) {
        
        double val_a = (term_a->type == MTPNDD_TERMINAL_INTEGER) ? 
                       (double)term_a->value.integer : term_a->value.floating;
        double val_b = (term_b->type == MTPNDD_TERMINAL_INTEGER) ? 
                       (double)term_b->value.integer : term_b->value.floating;
        
        double result;
        switch (config->strategy) {
            case MTPNDD_STRATEGY_NUMERIC_MIN:
                result = (val_a < val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MAX:
                result = (val_a > val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_ADD:
                result = val_a + val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MUL:
                result = val_a * val_b;
                break;
            default:
                result = (val_a != 0.0 && val_b != 0.0) ? 1.0 : 0.0;
                break;
        }
        
        return mtpndd_create_double_node(result);
    }
    
    // 与布尔值的混合类型
    if (term_a->type == MTPNDD_TERMINAL_BOOLEAN || term_b->type == MTPNDD_TERMINAL_BOOLEAN) {
        bool bool_val, other_bool;
        
        if (term_a->type == MTPNDD_TERMINAL_BOOLEAN) {
            bool_val = term_a->value.boolean;
            other_bool = mtpndd_terminal_to_bool(term_b);
        } else {
            bool_val = term_b->value.boolean;
            other_bool = mtpndd_terminal_to_bool(term_a);
        }
        
        return mtpndd_from_boolean(bool_val && other_bool);
    }
    
    // 默认：按布尔逻辑处理其他类型组合
    bool a_bool = mtpndd_terminal_to_bool(term_a);
    bool b_bool = mtpndd_terminal_to_bool(term_b);
    return mtpndd_from_boolean(a_bool && b_bool);
}

// 终端值与非终端节点的AND操作
static mtpndd_t mtpndd_and_terminal_with_node(mtpndd_t terminal, mtpndd_t node, const mtpndd_logic_config_t *config) {
    if (!mtpndd_is_terminal(terminal)) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 如果终端值是false/0，结果是false
    if (!mtpndd_terminal_to_bool(terminal.node->terminal)) {
        return mtpndd_from_boolean(false);
    }
    
    // 如果终端值是true/非0，结果是原节点
    return mtpndd_copy(node);
}

// 两个非终端节点的AND操作
static mtpndd_t mtpndd_and_nodes(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config) {
    // 如果是普通NDD节点，使用NDD逻辑
    if (!a.is_multiterminal && !b.is_multiterminal) {
        ndd_t ndd_a = mtpndd_to_ndd(a);
        ndd_t ndd_b = mtpndd_to_ndd(b);
        ndd_t result_ndd = ndd_and(ndd_a, ndd_b);
        return ndd_to_mtpndd(result_ndd);
    }
    
    // 对于MTNDD节点，需要处理每个边的组合
    // 这里先实现简化版本，返回第一个节点
    // TODO: 实现完整的多终端节点组合逻辑
    return mtpndd_copy(a);
}

// 终端值OR操作：支持所有类型组合
static mtpndd_t mtpndd_or_terminals(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config) {
    mtpndd_terminal_t *term_a = a.node->terminal;
    mtpndd_terminal_t *term_b = b.node->terminal;
    
    // 布尔类型组合
    if (term_a->type == MTPNDD_TERMINAL_BOOLEAN && term_b->type == MTPNDD_TERMINAL_BOOLEAN) {
        bool result = term_a->value.boolean || term_b->value.boolean;
        return mtpndd_from_boolean(result);
    }
    
    // 整数类型组合
    if (term_a->type == MTPNDD_TERMINAL_INTEGER && term_b->type == MTPNDD_TERMINAL_INTEGER) {
        int64_t val_a = term_a->value.integer;
        int64_t val_b = term_b->value.integer;
        int64_t result;
        
        switch (config->strategy) {
            case MTPNDD_STRATEGY_NUMERIC_MIN:
                result = (val_a < val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MAX:
                result = (val_a > val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_ADD:
                result = val_a + val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MUL:
                result = val_a * val_b;
                break;
            case MTPNDD_STRATEGY_BITWISE:
                result = val_a | val_b; // 按位OR
                break;
            default: // MTPNDD_STRATEGY_BOOLEAN
                result = (val_a || val_b) ? 1 : 0;
                break;
        }
        
        return mtpndd_create_integer_node(result);
    }
    
    // 浮点数类型组合
    if (term_a->type == MTPNDD_TERMINAL_DOUBLE && term_b->type == MTPNDD_TERMINAL_DOUBLE) {
        double val_a = term_a->value.floating;
        double val_b = term_b->value.floating;
        double result;
        
        switch (config->strategy) {
            case MTPNDD_STRATEGY_NUMERIC_MIN:
                result = (val_a < val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MAX:
                result = (val_a > val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_ADD:
                result = val_a + val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MUL:
                result = val_a * val_b;
                break;
            default: // MTPNDD_STRATEGY_BOOLEAN
                result = (val_a != 0.0 || val_b != 0.0) ? 1.0 : 0.0;
                break;
        }
        
        return mtpndd_create_double_node(result);
    }
    
    // 字符串类型组合
    if (term_a->type == MTPNDD_TERMINAL_STRING && term_b->type == MTPNDD_TERMINAL_STRING) {
        if (config->strategy == MTPNDD_STRATEGY_STRING_CONCAT) {
            // 字符串连接
            size_t total_len = term_a->value.string.length + term_b->value.string.length;
            char *concat_str = malloc(total_len + 1);
            if (!concat_str) {
                mtpndd_t null_result = {NULL, false, false};
                return null_result;
            }
            
            strcpy(concat_str, term_a->value.string.data);
            strcat(concat_str, term_b->value.string.data);
            
            mtpndd_t result = mtpndd_from_string(concat_str);
            free(concat_str);
            return result;
        } else {
            // 布尔逻辑：任意一个字符串非空则为true
            bool a_nonempty = (term_a->value.string.length > 0);
            bool b_nonempty = (term_b->value.string.length > 0);
            return mtpndd_from_boolean(a_nonempty || b_nonempty);
        }
    }
    
    // 混合类型：整数和浮点数
    if ((term_a->type == MTPNDD_TERMINAL_INTEGER && term_b->type == MTPNDD_TERMINAL_DOUBLE) ||
        (term_a->type == MTPNDD_TERMINAL_DOUBLE && term_b->type == MTPNDD_TERMINAL_INTEGER)) {
        
        double val_a = (term_a->type == MTPNDD_TERMINAL_INTEGER) ? 
                       (double)term_a->value.integer : term_a->value.floating;
        double val_b = (term_b->type == MTPNDD_TERMINAL_INTEGER) ? 
                       (double)term_b->value.integer : term_b->value.floating;
        
        double result;
        switch (config->strategy) {
            case MTPNDD_STRATEGY_NUMERIC_MIN:
                result = (val_a < val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MAX:
                result = (val_a > val_b) ? val_a : val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_ADD:
                result = val_a + val_b;
                break;
            case MTPNDD_STRATEGY_NUMERIC_MUL:
                result = val_a * val_b;
                break;
            default:
                result = (val_a != 0.0 || val_b != 0.0) ? 1.0 : 0.0;
                break;
        }
        
        return mtpndd_create_double_node(result);
    }
    
    // 与布尔值的混合类型
    if (term_a->type == MTPNDD_TERMINAL_BOOLEAN || term_b->type == MTPNDD_TERMINAL_BOOLEAN) {
        bool bool_val, other_bool;
        
        if (term_a->type == MTPNDD_TERMINAL_BOOLEAN) {
            bool_val = term_a->value.boolean;
            other_bool = mtpndd_terminal_to_bool(term_b);
        } else {
            bool_val = term_b->value.boolean;
            other_bool = mtpndd_terminal_to_bool(term_a);
        }
        
        return mtpndd_from_boolean(bool_val || other_bool);
    }
    
    // 默认：按布尔逻辑处理其他类型组合
    bool a_bool = mtpndd_terminal_to_bool(term_a);
    bool b_bool = mtpndd_terminal_to_bool(term_b);
    return mtpndd_from_boolean(a_bool || b_bool);
}

// 终端值与非终端节点的OR操作
static mtpndd_t mtpndd_or_terminal_with_node(mtpndd_t terminal, mtpndd_t node, const mtpndd_logic_config_t *config) {
    (void)config; // 暂时不使用
    
    if (!mtpndd_is_terminal(terminal)) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    // 如果终端值为true/非0，结果为true
    if (mtpndd_terminal_to_bool(terminal.node->terminal)) {
        return mtpndd_from_boolean(true);
    }
    
    // 如果终端值为false/0，结果是原节点
    return mtpndd_copy(node);
}

// 两个非终端节点的OR操作
static mtpndd_t mtpndd_or_nodes(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config) {
    (void)config; // 暂时不使用
    
    // 如果是普通NDD节点，使用NDD逻辑
    if (!a.is_multiterminal && !b.is_multiterminal) {
        ndd_t ndd_a = mtpndd_to_ndd(a);
        ndd_t ndd_b = mtpndd_to_ndd(b);
        ndd_t result_ndd = ndd_or(ndd_a, ndd_b);
        return ndd_to_mtpndd(result_ndd);
    }
    
    // 对于MTNDD节点，需要处理每个边的组合
    // 这里先实现简化版本，返回第一个节点
    // TODO: 实现完整的多终端节点组合逻辑
    return mtpndd_copy(a);
}

// 终端值NOT操作：支持所有终端类型
static mtpndd_t mtpndd_not_terminal(mtpndd_t a, const mtpndd_logic_config_t *config) {
    mtpndd_terminal_t *term = a.node->terminal;
    
    switch (term->type) {
        case MTPNDD_TERMINAL_BOOLEAN: {
            // 布尔逻辑非
            bool result = !term->value.boolean;
            return mtpndd_from_boolean(result);
        }
        
        case MTPNDD_TERMINAL_INTEGER: {
            int64_t val = term->value.integer;
            int64_t result;
            
            switch (config->strategy) {
                case MTPNDD_STRATEGY_BITWISE:
                    result = ~val; // 按位取反
                    break;
                case MTPNDD_STRATEGY_NUMERIC_MUL:
                    result = -val; // 数值取负
                    break;
                default: // MTPNDD_STRATEGY_BOOLEAN
                    result = (val == 0) ? 1 : 0; // 布尔逻辑非
                    break;
            }
            
            return mtpndd_create_integer_node(result);
        }
        
        case MTPNDD_TERMINAL_DOUBLE: {
            double val = term->value.floating;
            double result;
            
            switch (config->strategy) {
                case MTPNDD_STRATEGY_NUMERIC_MUL:
                    result = -val; // 数值取负
                    break;
                default: // MTPNDD_STRATEGY_BOOLEAN
                    result = (val == 0.0) ? 1.0 : 0.0; // 布尔逻辑非
                    break;
            }
            
            return mtpndd_create_double_node(result);
        }
        
        case MTPNDD_TERMINAL_STRING: {
            // 字符串逻辑非：空字符串→"!empty"，非空→空字符串
            if (term->value.string.length == 0) {
                return mtpndd_from_string("!empty");
            } else {
                return mtpndd_from_string("");
            }
        }
        
        case MTPNDD_TERMINAL_BYTES: {
            // 二进制数据按位取反
            size_t len = term->value.bytes.length;
            void *new_data = malloc(len);
            if (!new_data) {
                mtpndd_t null_result = {NULL, false, false};
                return null_result;
            }
            
            uint8_t *src = (uint8_t*)term->value.bytes.data;
            uint8_t *dst = (uint8_t*)new_data;
            
            for (size_t i = 0; i < len; i++) {
                dst[i] = ~src[i];
            }
            
            mtpndd_t result = mtpndd_from_bytes(new_data, len);
            free(new_data);
            return result;
        }
        
        case MTPNDD_TERMINAL_CUSTOM: {
            // 自定义类型：转换为布尔值再取反
            bool val = mtpndd_terminal_to_bool(term);
            return mtpndd_from_boolean(!val);
        }
        
        default: {
            // 未知类型：返回布尔false
            return mtpndd_from_boolean(false);
        }
    }
}

// 非终端节点NOT操作
static mtpndd_t mtpndd_not_node(mtpndd_t a, const mtpndd_logic_config_t *config) {
    (void)config; // 暂时不使用
    
    // 如果是普通NDD节点，使用NDD逻辑
    if (!a.is_multiterminal) {
        ndd_t ndd_a = mtpndd_to_ndd(a);
        ndd_t result_ndd = ndd_not(ndd_a);
        return ndd_to_mtpndd(result_ndd);
    }
    
    // 对于MTNDD节点，需要对每个终端值取反
    // 这里先实现简化版本，返回布尔false
    // TODO: 实现完整的多终端节点NOT操作
    return mtpndd_from_boolean(false);
}

// 辅助函数：将终端值转换为布尔值
static bool mtpndd_terminal_to_bool(const mtpndd_terminal_t *terminal) {
    if (!terminal) return false;
    
    switch (terminal->type) {
        case MTPNDD_TERMINAL_BOOLEAN:
            return terminal->value.boolean;
        case MTPNDD_TERMINAL_INTEGER:
            return terminal->value.integer != 0;
        case MTPNDD_TERMINAL_DOUBLE:
            return terminal->value.floating != 0.0;
        case MTPNDD_TERMINAL_STRING:
            return terminal->value.string.length > 0;
        case MTPNDD_TERMINAL_BYTES:
            return terminal->value.bytes.length > 0;
        case MTPNDD_TERMINAL_CUSTOM:
            // 默认自定义类型非空时为true
            return terminal->value.custom.data != NULL;
        default:
            return false;
    }
}


mtpndd_t mtpndd_ite(mtpndd_t condition, mtpndd_t then_branch, mtpndd_t else_branch) {
    mtpndd_logic_config_t config = mtpndd_create_default_logic_config(MTPNDD_STRATEGY_BOOLEAN);
    return mtpndd_ite_with_config(condition, then_branch, else_branch, &config);
}

mtpndd_t mtpndd_ite_with_config(mtpndd_t condition, mtpndd_t then_branch, mtpndd_t else_branch,
                               const mtpndd_logic_config_t *config) {
    if (!condition.node || !then_branch.node || !else_branch.node || !config) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    g_mtpndd_logic_stats.ite_operations++;
    
    // 布尔条件的ITE操作
    if (mtpndd_is_boolean_terminal(condition)) {
        bool cond_value = mtpndd_get_boolean(condition);
        return cond_value ? then_branch : else_branch;
    }
    
    // 数值条件的ITE操作（非零为true）
    if (mtpndd_is_integer_terminal(condition)) {
        int64_t cond_value = mtpndd_get_integer(condition);
        return (cond_value != 0) ? then_branch : else_branch;
    }
    
    // 复杂情况：简化返回 then_branch
    return mtpndd_copy(then_branch);
}

// 值编码器：将特定类型的值编码为MTPNDD
mtpndd_t mtpndd_encode_integer_range(uint32_t field, int64_t min_value, int64_t max_value) {
    if (min_value > max_value) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    mtpndd_t result = mtpndd_create_node(field);
    
    // 为范围内的每个整数创建终端节点（限制数量避免过大）
    int64_t count = 0;
    for (int64_t value = min_value; value <= max_value && count < 100; value++, count++) {
        mtpndd_terminal_t *terminal = mtpndd_terminal_integer(value);
        if (terminal) {
            mtpndd_t terminal_node = mtpndd_create_terminal(terminal);
            
            // 创建表示该值的BDD约束（简化）
            ndd_bdd_t value_bdd = ndd_sylvan_true;
            mtpndd_add_edge_ex(&result, terminal_node, value_bdd);
            
            mtpndd_terminal_deref(terminal);
        }
    }
    
    return result;
}

mtpndd_t mtpndd_encode_string_set(uint32_t field, const char **strings, uint32_t string_count) {
    if (!strings || string_count == 0) {
        mtpndd_t null_result = {NULL, false, false};
        return null_result;
    }
    
    mtpndd_t result = mtpndd_create_node(field);
    
    // 为每个字符串创建终端节点
    for (uint32_t i = 0; i < string_count; i++) {
        if (strings[i]) {
            mtpndd_terminal_t *terminal = mtpndd_terminal_string(strings[i]);
            if (terminal) {
                mtpndd_t terminal_node = mtpndd_create_terminal(terminal);
                mtpndd_add_edge_ex(&result, terminal_node, ndd_sylvan_true);
                mtpndd_terminal_deref(terminal);
            }
        }
    }
    
    return result;
}

// 值解码器：从MTPNDD中提取特定类型的值集合
mtpndd_value_set_t* mtpndd_extract_values(mtpndd_t mtpndd, mtpndd_terminal_type_t type) {
    if (!mtpndd.node) {
        return NULL;
    }
    
    mtpndd_value_set_t *value_set = malloc(sizeof(mtpndd_value_set_t));
    if (!value_set) {
        return NULL;
    }
    
    value_set->type = type;
    value_set->count = 0;
    
    // 简化实现：只处理终端节点
    if (mtpndd.is_terminal && mtpndd.is_multiterminal && mtpndd.node->terminal) {
        if (mtpndd.node->terminal->type == type) {
            value_set->count = 1;
            
            switch (type) {
                case MTPNDD_TERMINAL_BOOLEAN:
                    value_set->values.booleans = malloc(sizeof(bool));
                    if (value_set->values.booleans) {
                        value_set->values.booleans[0] = mtpndd.node->terminal->value.boolean;
                    }
                    break;
                    
                case MTPNDD_TERMINAL_INTEGER:
                    value_set->values.integers = malloc(sizeof(int64_t));
                    if (value_set->values.integers) {
                        value_set->values.integers[0] = mtpndd.node->terminal->value.integer;
                    }
                    break;
                    
                case MTPNDD_TERMINAL_STRING:
                    value_set->values.strings = malloc(sizeof(char*));
                    if (value_set->values.strings) {
                        value_set->values.strings[0] = strdup(mtpndd.node->terminal->value.string.data);
                    }
                    break;
                    
                default:
                    value_set->count = 0;
                    break;
            }
        }
    }
    
    return value_set;
}

void mtpndd_value_set_destroy(mtpndd_value_set_t *value_set) {
    if (!value_set) return;
    
    if (value_set->count > 0) {
        switch (value_set->type) {
            case MTPNDD_TERMINAL_STRING:
                for (uint32_t i = 0; i < value_set->count; i++) {
                    free(value_set->values.strings[i]);
                }
                free(value_set->values.strings);
                break;
            default:
                // 其他类型直接释放数组
                free(value_set->values.booleans);  // 共用联合体
                break;
        }
    }
    
    free(value_set);
}

// ===========================================
// MTPNDD序列化和反序列化操作实现
// ===========================================

// 序列化流结构体
struct mtpndd_serialization_stream_s {
    mtpndd_t *nodes;
    uint32_t count;
    uint32_t capacity;
    mtpndd_serialization_options_t options;
    bool finalized;
};

// 默认序列化选项
static const mtpndd_serialization_options_t DEFAULT_SERIALIZATION_OPTIONS = {
    .format = MTPNDD_FORMAT_BINARY,
    .compress_data = false,
    .include_metadata = true,
    .validate_integrity = true,
    .version = 1,
    .encoding = "UTF-8"
};

// 计算简单校验和
static uint32_t mtpndd_calculate_checksum(const void *data, size_t size) {
    if (!data || size == 0) return 0;
    
    uint32_t checksum = 0;
    const uint8_t *bytes = (const uint8_t*)data;
    
    for (size_t i = 0; i < size; i++) {
        checksum = checksum * 31 + bytes[i];
    }
    
    return checksum;
}

// 序列化单个MTPNDD节点
mtpndd_serialized_data_t* mtpndd_serialize_node(mtpndd_t mtpndd, 
                                               const mtpndd_serialization_options_t *options) {
    if (!mtpndd.node) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return NULL;
    }
    
    if (!options) {
        options = &DEFAULT_SERIALIZATION_OPTIONS;
    }
    
    mtpndd_serialized_data_t *result = malloc(sizeof(mtpndd_serialized_data_t));
    if (!result) {
        return NULL;
    }
    
    memset(result, 0, sizeof(mtpndd_serialized_data_t));
    result->format = options->format;
    
    if (options->format == MTPNDD_FORMAT_JSON) {
        // JSON格式序列化（简化实现）
        char *json_buffer = malloc(1024);
        if (!json_buffer) {
            free(result);
            return NULL;
        }
        
        size_t json_len = 0;
        
        if (mtpndd_is_boolean_terminal(mtpndd)) {
            json_len = snprintf(json_buffer, 1024,
                "{\"type\":\"terminal\",\"terminal_type\":\"boolean\",\"value\":%s}",
                mtpndd_get_boolean(mtpndd) ? "true" : "false");
        } else if (mtpndd_is_integer_terminal(mtpndd)) {
            json_len = snprintf(json_buffer, 1024,
                "{\"type\":\"terminal\",\"terminal_type\":\"integer\",\"value\":%ld}",
                mtpndd_get_integer(mtpndd));
        } else if (mtpndd_is_string_terminal(mtpndd)) {
            json_len = snprintf(json_buffer, 1024,
                "{\"type\":\"terminal\",\"terminal_type\":\"string\",\"value\":\"%s\"}",
                mtpndd_get_string(mtpndd));
        } else {
            json_len = snprintf(json_buffer, 1024,
                "{\"type\":\"node\",\"field\":%u,\"edge_count\":%u}",
                mtpndd_get_field(mtpndd), mtpndd_get_edge_count(mtpndd));
        }
        
        result->data = json_buffer;
        result->size = json_len;
        result->checksum = mtpndd_calculate_checksum(json_buffer, json_len);
    } else {
        // 不支持的格式
        free(result);
        mtpndd_set_error(MTPNDD_ERROR_UNSUPPORTED_TYPE, __FUNCTION__, __LINE__);
        return NULL;
    }
    
    return result;
}

// 反序列化MTPNDD节点
mtpndd_deserialized_result_t* mtpndd_deserialize(const mtpndd_serialized_data_t *data) {
    if (!data || !data->data || data->size == 0) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return NULL;
    }
    
    mtpndd_deserialized_result_t *result = malloc(sizeof(mtpndd_deserialized_result_t));
    if (!result) {
        return NULL;
    }
    
    memset(result, 0, sizeof(mtpndd_deserialized_result_t));
    
    // 验证数据完整性
    uint32_t calculated_checksum = mtpndd_calculate_checksum(data->data, data->size);
    result->integrity_verified = (calculated_checksum == data->checksum);
    
    // 简化实现：创建一个默认节点
    result->nodes = malloc(sizeof(mtpndd_t));
    if (!result->nodes) {
        free(result);
        return NULL;
    }
    
    result->node_count = 1;
    result->nodes[0] = mtpndd_false();  // 默认值
    
    return result;
}

// 从文件序列化
int mtpndd_serialize_to_file(mtpndd_t mtpndd, const char *filename,
                           const mtpndd_serialization_options_t *options) {
    if (!filename) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return -1;
    }
    
    mtpndd_serialized_data_t *data = mtpndd_serialize_node(mtpndd, options);
    if (!data) {
        return -1;
    }
    
    FILE *file = fopen(filename, "wb");
    if (!file) {
        mtpndd_destroy_serialized_data(data);
        return -1;
    }
    
    size_t written = fwrite(data->data, 1, data->size, file);
    fclose(file);
    
    int result = (written == data->size) ? 0 : -1;
    mtpndd_destroy_serialized_data(data);
    
    return result;
}

// 从文件反序列化
mtpndd_deserialized_result_t* mtpndd_deserialize_from_file(const char *filename) {
    if (!filename) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_TERMINAL, __FUNCTION__, __LINE__);
        return NULL;
    }
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return NULL;
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 10 * 1024 * 1024) {  // 限制10MB
        fclose(file);
        return NULL;
    }
    
    // 读取文件内容
    void *buffer = malloc(file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read_size != (size_t)file_size) {
        free(buffer);
        return NULL;
    }
    
    // 创建序列化数据结构
    mtpndd_serialized_data_t data;
    data.data = buffer;
    data.size = file_size;
    data.format = MTPNDD_FORMAT_BINARY;  // 假设为二进制格式
    data.checksum = mtpndd_calculate_checksum(buffer, file_size);
    data.error_message = NULL;
    
    // 反序列化
    mtpndd_deserialized_result_t *result = mtpndd_deserialize(&data);
    
    free(buffer);
    return result;
}

// 验证序列化数据
bool mtpndd_verify_serialized_data(const mtpndd_serialized_data_t *data) {
    if (!data || !data->data || data->size == 0) {
        return false;
    }
    
    uint32_t calculated_checksum = mtpndd_calculate_checksum(data->data, data->size);
    return (calculated_checksum == data->checksum);
}

// 获取序列化统计信息
mtpndd_serialization_stats_t mtpndd_get_serialization_stats(const mtpndd_serialized_data_t *data) {
    mtpndd_serialization_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    
    if (data) {
        stats.total_nodes = 1;  // 简化实现
        stats.uncompressed_size = data->size;
        stats.compressed_size = data->size;
        stats.compression_ratio = 1.0;
    }
    
    return stats;
}

// 内存清理函数
void mtpndd_destroy_serialized_data(mtpndd_serialized_data_t *data) {
    if (!data) return;
    
    if (data->data) {
        free(data->data);
    }
    if (data->error_message) {
        free(data->error_message);
    }
    free(data);
}

void mtpndd_destroy_deserialized_result(mtpndd_deserialized_result_t *result) {
    if (!result) return;
    
    if (result->nodes) {
        for (uint32_t i = 0; i < result->node_count; i++) {
            mtpndd_deref(result->nodes[i]);
        }
        free(result->nodes);
    }
    if (result->warning_message) {
        free(result->warning_message);
    }
    free(result);
}

// ===========================================
// MTPNDD自定义终端值运算系统实现
// ===========================================

// 全局运算管理器
static mtpndd_operation_manager_t g_operation_manager = {0};
static mtpndd_operation_cache_manager_t g_operation_cache = {0};
static mtpndd_operation_stats_t g_operation_stats = {0};
static bool g_operation_system_initialized = false;

// 缓存查找函数
static mtpndd_terminal_t* mtpndd_check_operation_cache(
    uint32_t operation_id,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
) {
    if (!g_operation_cache.enable_cache || !g_operation_cache.cache) {
        return NULL;
    }
    
    // 简化的缓存查找：线性扫描
    for (uint32_t i = 0; i < g_operation_cache.cache_used; i++) {
        mtpndd_operation_cache_entry_t *entry = &g_operation_cache.cache[i];
        if (entry->is_valid && 
            entry->operation_id == operation_id &&
            entry->operand1 == operand1 &&
            entry->operand2 == operand2) {
            entry->access_count++;
            return entry->result;
        }
    }
    
    return NULL;
}

// 缓存存储函数
static void mtpndd_store_operation_cache(
    uint32_t operation_id,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2,
    mtpndd_terminal_t *result
) {
    if (!g_operation_cache.enable_cache || !g_operation_cache.cache || !result) {
        return;
    }
    
    // 简化：如果缓存已满，不存储
    if (g_operation_cache.cache_used >= g_operation_cache.cache_size) {
        return;
    }
    
    mtpndd_operation_cache_entry_t *entry = &g_operation_cache.cache[g_operation_cache.cache_used];
    entry->operation_id = operation_id;
    entry->operand1 = operand1;
    entry->operand2 = operand2;
    entry->result = mtpndd_terminal_ref(result);
    entry->access_count = 1;
    entry->is_valid = true;
    
    g_operation_cache.cache_used++;
}

// 初始化运算管理器
int mtpndd_init_operation_manager() {
    if (g_operation_system_initialized) {
        return 0;
    }
    
    g_operation_manager.operation_capacity = 32;
    g_operation_manager.operations = calloc(g_operation_manager.operation_capacity, 
                                          sizeof(mtpndd_operation_registry_t));
    if (!g_operation_manager.operations) {
        return -1;
    }
    
    g_operation_manager.operation_count = 0;
    g_operation_manager.next_operation_id = 1;
    
    g_operation_cache.cache_size = 256;
    g_operation_cache.cache = calloc(g_operation_cache.cache_size, 
                                   sizeof(mtpndd_operation_cache_entry_t));
    if (!g_operation_cache.cache) {
        free(g_operation_manager.operations);
        return -1;
    }
    
    g_operation_cache.enable_cache = true;
    memset(&g_operation_stats, 0, sizeof(mtpndd_operation_stats_t));
    
    g_operation_system_initialized = true;
    return 0;
}

void mtpndd_cleanup_operation_manager() {
    if (!g_operation_system_initialized) {
        return;
    }
    
    if (g_operation_manager.operations) {
        for (uint32_t i = 0; i < g_operation_manager.operation_count; i++) {
            free((void*)g_operation_manager.operations[i].metadata.name);
            free((void*)g_operation_manager.operations[i].metadata.description);
        }
        free(g_operation_manager.operations);
    }
    
    if (g_operation_cache.cache) {
        for (uint32_t i = 0; i < g_operation_cache.cache_used; i++) {
            if (g_operation_cache.cache[i].is_valid && g_operation_cache.cache[i].result) {
                mtpndd_terminal_deref(g_operation_cache.cache[i].result);
            }
        }
        free(g_operation_cache.cache);
    }
    
    g_operation_system_initialized = false;
}

// 注册二元运算
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
) {
    if (!g_operation_system_initialized || !name || !operation_fn) {
        return 0;
    }
    
    if (g_operation_manager.operation_count >= g_operation_manager.operation_capacity) {
        uint32_t new_capacity = g_operation_manager.operation_capacity * 2;
        mtpndd_operation_registry_t *new_operations = realloc(
            g_operation_manager.operations, 
            new_capacity * sizeof(mtpndd_operation_registry_t)
        );
        if (!new_operations) {
            return 0;
        }
        g_operation_manager.operations = new_operations;
        g_operation_manager.operation_capacity = new_capacity;
    }
    
    mtpndd_operation_registry_t *registry = 
        &g_operation_manager.operations[g_operation_manager.operation_count];
    
    registry->metadata.name = strdup(name);
    registry->metadata.description = description ? strdup(description) : NULL;
    registry->metadata.input_type1 = input_type1;
    registry->metadata.input_type2 = input_type2;
    registry->metadata.output_type = output_type;
    registry->metadata.is_commutative = is_commutative;
    registry->metadata.is_associative = is_associative;
    registry->metadata.is_unary = false;
    registry->metadata.operation_id = g_operation_manager.next_operation_id;
    
    registry->binary_fn = operation_fn;
    registry->unary_fn = NULL;
    registry->user_data = user_data;
    registry->is_active = true;
    
    g_operation_manager.operation_count++;
    g_operation_stats.registered_operations++;
    g_operation_stats.active_operations++;
    
    return g_operation_manager.next_operation_id++;
}

// 注册一元运算
uint32_t mtpndd_register_unary_operation(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t input_type,
    mtpndd_terminal_type_t output_type,
    mtpndd_unary_operation_fn operation_fn,
    void *user_data
) {
    if (!g_operation_system_initialized || !name || !operation_fn) {
        return 0;
    }
    
    uint32_t op_id = mtpndd_register_binary_operation(
        name, description, input_type, MTPNDD_TERMINAL_BOOLEAN, output_type,
        NULL, user_data, false, false
    );
    
    if (op_id > 0) {
        for (uint32_t i = 0; i < g_operation_manager.operation_count; i++) {
            if (g_operation_manager.operations[i].metadata.operation_id == op_id) {
                g_operation_manager.operations[i].metadata.is_unary = true;
                g_operation_manager.operations[i].unary_fn = operation_fn;
                g_operation_manager.operations[i].binary_fn = NULL;
                break;
            }
        }
    }
    
    return op_id;
}

// 查找运算
uint32_t mtpndd_find_operation_by_name(const char *name) {
    if (!g_operation_system_initialized || !name) {
        return 0;
    }
    
    for (uint32_t i = 0; i < g_operation_manager.operation_count; i++) {
        if (g_operation_manager.operations[i].is_active &&
            g_operation_manager.operations[i].metadata.name &&
            strcmp(g_operation_manager.operations[i].metadata.name, name) == 0) {
            return g_operation_manager.operations[i].metadata.operation_id;
        }
    }
    
    return 0;
}

// 执行自定义运算
mtpndd_terminal_t* mtpndd_execute_custom_operation(
    uint32_t operation_id,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
) {
    if (!g_operation_system_initialized || operation_id == 0 || !operand1) {
        return NULL;
    }
    
    mtpndd_operation_registry_t *registry = NULL;
    for (uint32_t i = 0; i < g_operation_manager.operation_count; i++) {
        if (g_operation_manager.operations[i].metadata.operation_id == operation_id &&
            g_operation_manager.operations[i].is_active) {
            registry = &g_operation_manager.operations[i];
            break;
        }
    }
    
    if (!registry) {
        g_operation_stats.failed_operations++;
        return NULL;
    }
    
    if (g_operation_cache.enable_cache) {
        mtpndd_terminal_t *cached_result = mtpndd_check_operation_cache(
            operation_id, operand1, operand2
        );
        if (cached_result) {
            g_operation_cache.cache_hits++;
            g_operation_stats.cached_operations++;
            return mtpndd_terminal_ref(cached_result);
        }
        g_operation_cache.cache_misses++;
    }
    
    mtpndd_terminal_t *result = NULL;
    
    if (registry->metadata.is_unary) {
        if (registry->unary_fn) {
            result = registry->unary_fn(operand1, registry->user_data);
        }
    } else {
        if (registry->binary_fn && operand2) {
            result = registry->binary_fn(operand1, operand2, registry->user_data);
        }
    }
    
    if (result) {
        if (g_operation_cache.enable_cache) {
            mtpndd_store_operation_cache(operation_id, operand1, operand2, result);
        }
        g_operation_stats.total_operations++;
    } else {
        g_operation_stats.failed_operations++;
    }
    
    return result;
}

// 获取运算统计信息
mtpndd_operation_stats_t mtpndd_get_operation_stats() {
    return g_operation_stats;
}

void mtpndd_reset_operation_stats() {
    memset(&g_operation_stats, 0, sizeof(mtpndd_operation_stats_t));
}

void mtpndd_print_operation_stats() {
    printf("=== MTPNDD Custom Operation Statistics ===\n");
    printf("Total operations: %lu\n", g_operation_stats.total_operations);
    printf("Cached operations: %lu\n", g_operation_stats.cached_operations);
    printf("Failed operations: %lu\n", g_operation_stats.failed_operations);
    printf("Registered operations: %lu\n", g_operation_stats.registered_operations);
    printf("Active operations: %lu\n", g_operation_stats.active_operations);
    printf("Average operation time: %.6f ms\n", g_operation_stats.average_operation_time);
    printf("Cache hit ratio: %.2f%%\n", g_operation_stats.cache_hit_ratio * 100.0);
    printf("Memory usage: %lu bytes\n", g_operation_stats.memory_usage);
}

// ===========================================
// 预定义运算实现
// ===========================================

// 数值加法运算
static mtpndd_terminal_t* mtpndd_operation_add(
    const mtpndd_terminal_t *a, 
    const mtpndd_terminal_t *b, 
    void *user_data
) {
    (void)user_data; // 未使用
    
    if (!a || !b) return NULL;
    
    if (a->type == MTPNDD_TERMINAL_INTEGER && b->type == MTPNDD_TERMINAL_INTEGER) {
        int64_t result = a->value.integer + b->value.integer;
        return mtpndd_terminal_integer(result);
    }
    
    if (a->type == MTPNDD_TERMINAL_DOUBLE && b->type == MTPNDD_TERMINAL_DOUBLE) {
        double result = a->value.floating + b->value.floating;
        return mtpndd_terminal_double(result);
    }
    
    // 混合类型：整数+浮点
    if (a->type == MTPNDD_TERMINAL_INTEGER && b->type == MTPNDD_TERMINAL_DOUBLE) {
        double result = (double)a->value.integer + b->value.floating;
        return mtpndd_terminal_double(result);
    }
    
    if (a->type == MTPNDD_TERMINAL_DOUBLE && b->type == MTPNDD_TERMINAL_INTEGER) {
        double result = a->value.floating + (double)b->value.integer;
        return mtpndd_terminal_double(result);
    }
    
    return NULL;
}

// 字符串连接运算
static mtpndd_terminal_t* mtpndd_operation_concat(
    const mtpndd_terminal_t *a,
    const mtpndd_terminal_t *b,
    void *user_data
) {
    (void)user_data;
    
    if (!a || !b) return NULL;
    
    if (a->type == MTPNDD_TERMINAL_STRING && b->type == MTPNDD_TERMINAL_STRING) {
        size_t total_len = a->value.string.length + b->value.string.length;
        char *concat_str = malloc(total_len + 1);
        if (!concat_str) return NULL;
        
        strcpy(concat_str, a->value.string.data);
        strcat(concat_str, b->value.string.data);
        
        mtpndd_terminal_t *result = mtpndd_terminal_string(concat_str);
        free(concat_str);
        return result;
    }
    
    return NULL;
}

// 绝对值运算（一元）
static mtpndd_terminal_t* mtpndd_operation_abs(
    const mtpndd_terminal_t *a,
    void *user_data
) {
    (void)user_data;
    
    if (!a) return NULL;
    
    if (a->type == MTPNDD_TERMINAL_INTEGER) {
        int64_t result = (a->value.integer < 0) ? -a->value.integer : a->value.integer;
        return mtpndd_terminal_integer(result);
    }
    
    if (a->type == MTPNDD_TERMINAL_DOUBLE) {
        double result = (a->value.floating < 0.0) ? -a->value.floating : a->value.floating;
        return mtpndd_terminal_double(result);
    }
    
    return NULL;
}

// 注册预定义运算
static void mtpndd_register_predefined_operations() {
    // 数值运算
    mtpndd_register_binary_operation(
        "add", "Addition operation",
        MTPNDD_TERMINAL_INTEGER, MTPNDD_TERMINAL_INTEGER, MTPNDD_TERMINAL_INTEGER,
        mtpndd_operation_add, NULL, true, true
    );
    
    mtpndd_register_binary_operation(
        "add_double", "Double addition operation",
        MTPNDD_TERMINAL_DOUBLE, MTPNDD_TERMINAL_DOUBLE, MTPNDD_TERMINAL_DOUBLE,
        mtpndd_operation_add, NULL, true, true
    );
    
    // 字符串运算
    mtpndd_register_binary_operation(
        "concat", "String concatenation",
        MTPNDD_TERMINAL_STRING, MTPNDD_TERMINAL_STRING, MTPNDD_TERMINAL_STRING,
        mtpndd_operation_concat, NULL, false, false
    );
    
    // 一元运算
    mtpndd_register_unary_operation(
        "abs", "Absolute value",
        MTPNDD_TERMINAL_INTEGER, MTPNDD_TERMINAL_INTEGER,
        mtpndd_operation_abs, NULL
    );
    
    mtpndd_register_unary_operation(
        "abs_double", "Absolute value for double",
        MTPNDD_TERMINAL_DOUBLE, MTPNDD_TERMINAL_DOUBLE,
        mtpndd_operation_abs, NULL
    );
}

// 预定义运算快捷函数
mtpndd_terminal_t* mtpndd_add(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b) {
    return mtpndd_operation_add(a, b, NULL);
}

mtpndd_terminal_t* mtpndd_concat(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b) {
    return mtpndd_operation_concat(a, b, NULL);
}

mtpndd_terminal_t* mtpndd_abs(const mtpndd_terminal_t *a) {
    return mtpndd_operation_abs(a, NULL);
}

// ===========================================
// 终端值约束和验证机制实现
// ===========================================

// 全局约束管理器
static mtpndd_constraint_manager_t g_constraint_manager = {0};

// 全局约束统计信息
static mtpndd_constraint_stats_t g_constraint_stats = {0};

// 初始化约束管理器
int mtpndd_init_constraint_manager() {
    if (g_constraint_manager.constraints) {
        return 0; // 已经初始化
    }
    
    g_constraint_manager.constraint_capacity = 32;
    g_constraint_manager.constraints = calloc(
        g_constraint_manager.constraint_capacity, 
        sizeof(mtpndd_constraint_t)
    );
    
    if (!g_constraint_manager.constraints) {
        return -1;
    }
    
    g_constraint_manager.constraint_count = 0;
    g_constraint_manager.next_constraint_id = 1;
    g_constraint_manager.global_validation_enabled = true;
    g_constraint_manager.strict_mode = false;
    
    // 重置统计信息
    memset(&g_constraint_stats, 0, sizeof(mtpndd_constraint_stats_t));
    
    printf("⚙️  MTPNDD constraint manager initialized\n");
    return 0;
}

void mtpndd_cleanup_constraint_manager() {
    if (!g_constraint_manager.constraints) {
        return;
    }
    
    // 清理所有约束
    for (uint32_t i = 0; i < g_constraint_manager.constraint_count; i++) {
        mtpndd_constraint_t *constraint = &g_constraint_manager.constraints[i];
        
        // 清理约束特定数据
        if (constraint->type == MTPNDD_CONSTRAINT_SET && constraint->value.set.values) {
            free(constraint->value.set.values);
        }
        if (constraint->type == MTPNDD_CONSTRAINT_PATTERN && constraint->value.pattern.pattern) {
            free(constraint->value.pattern.pattern);
        }
    }
    
    free(g_constraint_manager.constraints);
    memset(&g_constraint_manager, 0, sizeof(mtpndd_constraint_manager_t));
    
    printf("⚙️  MTPNDD constraint manager cleaned up\n");
}

// 启用/禁用全局验证
void mtpndd_enable_global_validation(bool enable) {
    g_constraint_manager.global_validation_enabled = enable;
    printf("⚙️  Global validation %s\n", enable ? "enabled" : "disabled");
}

void mtpndd_set_strict_mode(bool strict) {
    g_constraint_manager.strict_mode = strict;
    printf("⚙️  Strict mode %s\n", strict ? "enabled" : "disabled");
}

// 约束注册与管理
uint32_t mtpndd_register_range_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    double min_value,
    double max_value,
    bool min_inclusive,
    bool max_inclusive
) {
    if (!g_constraint_manager.constraints || !name) {
        return 0;
    }
    
    // 检查容量是否需要扩容
    if (g_constraint_manager.constraint_count >= g_constraint_manager.constraint_capacity) {
        uint32_t new_capacity = g_constraint_manager.constraint_capacity * 2;
        mtpndd_constraint_t *new_constraints = realloc(
            g_constraint_manager.constraints,
            new_capacity * sizeof(mtpndd_constraint_t)
        );
        
        if (!new_constraints) {
            return 0;
        }
        
        g_constraint_manager.constraints = new_constraints;
        g_constraint_manager.constraint_capacity = new_capacity;
    }
    
    uint32_t constraint_id = g_constraint_manager.next_constraint_id++;
    mtpndd_constraint_t *constraint = &g_constraint_manager.constraints[g_constraint_manager.constraint_count];
    
    constraint->constraint_id = constraint_id;
    constraint->name = strdup(name);
    constraint->description = description ? strdup(description) : NULL;
    constraint->type = MTPNDD_CONSTRAINT_RANGE;
    constraint->target_type = target_type;
    constraint->value.range.min = min_value;
    constraint->value.range.max = max_value;
    constraint->value.range.min_inclusive = min_inclusive;
    constraint->value.range.max_inclusive = max_inclusive;
    constraint->is_active = true;
    constraint->validation_count = 0;
    constraint->violation_count = 0;
    
    g_constraint_manager.constraint_count++;
    g_constraint_stats.total_constraints++;
    g_constraint_stats.active_constraints++;
    
    printf("✓ Registered range constraint '%s' (ID: %u)\n", name, constraint_id);
    return constraint_id;
}

uint32_t mtpndd_register_custom_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    bool (*validate_fn)(const mtpndd_terminal_t *terminal, void *user_data),
    void *user_data
) {
    if (!g_constraint_manager.constraints || !name || !validate_fn) {
        return 0;
    }
    
    // 检查容量
    if (g_constraint_manager.constraint_count >= g_constraint_manager.constraint_capacity) {
        uint32_t new_capacity = g_constraint_manager.constraint_capacity * 2;
        mtpndd_constraint_t *new_constraints = realloc(
            g_constraint_manager.constraints,
            new_capacity * sizeof(mtpndd_constraint_t)
        );
        
        if (!new_constraints) {
            return 0;
        }
        
        g_constraint_manager.constraints = new_constraints;
        g_constraint_manager.constraint_capacity = new_capacity;
    }
    
    uint32_t constraint_id = g_constraint_manager.next_constraint_id++;
    mtpndd_constraint_t *constraint = &g_constraint_manager.constraints[g_constraint_manager.constraint_count];
    
    constraint->constraint_id = constraint_id;
    constraint->name = strdup(name);
    constraint->description = description ? strdup(description) : NULL;
    constraint->type = MTPNDD_CONSTRAINT_CUSTOM;
    constraint->target_type = target_type;
    constraint->value.custom.validate_fn = validate_fn;
    constraint->value.custom.user_data = user_data;
    constraint->is_active = true;
    constraint->validation_count = 0;
    constraint->violation_count = 0;
    
    g_constraint_manager.constraint_count++;
    g_constraint_stats.total_constraints++;
    g_constraint_stats.active_constraints++;
    
    printf("✓ Registered custom constraint '%s' (ID: %u)\n", name, constraint_id);
    return constraint_id;
}

// 验证单个终端值
mtpndd_validation_result_t* mtpndd_validate_terminal(const mtpndd_terminal_t *terminal) {
    if (!terminal || !g_constraint_manager.global_validation_enabled) {
        mtpndd_validation_result_t *result = malloc(sizeof(mtpndd_validation_result_t));
        if (result) {
            result->is_valid = true;
            result->violated_constraint_count = 0;
            result->violated_constraint_ids = NULL;
            result->violation_messages = NULL;
            result->error_code = NDD_SUCCESS;
            result->error_message = NULL;
        }
        return result;
    }
    
    mtpndd_validation_result_t *result = malloc(sizeof(mtpndd_validation_result_t));
    if (!result) {
        return NULL;
    }
    
    result->is_valid = true;
    result->violated_constraint_count = 0;
    result->violated_constraint_ids = NULL;
    result->violation_messages = NULL;
    result->error_code = NDD_SUCCESS;
    result->error_message = NULL;
    
    // 遍历所有激活的约束
    for (uint32_t i = 0; i < g_constraint_manager.constraint_count; i++) {
        mtpndd_constraint_t *constraint = &g_constraint_manager.constraints[i];
        
        if (!constraint->is_active) {
            continue;
        }
        
        // 检查类型匹配
        if (constraint->target_type != terminal->type && 
            constraint->target_type != MTPNDD_TERMINAL_CUSTOM) {
            continue;
        }
        
        constraint->validation_count++;
        g_constraint_stats.total_validations++;
        
        bool constraint_satisfied = false;
        
        switch (constraint->type) {
            case MTPNDD_CONSTRAINT_RANGE:
                if (terminal->type == MTPNDD_TERMINAL_INTEGER) {
                    double value = (double)terminal->value.integer;
                    constraint_satisfied = 
                        (constraint->value.range.min_inclusive ? 
                         value >= constraint->value.range.min : 
                         value > constraint->value.range.min) &&
                        (constraint->value.range.max_inclusive ? 
                         value <= constraint->value.range.max : 
                         value < constraint->value.range.max);
                } else if (terminal->type == MTPNDD_TERMINAL_DOUBLE) {
                    double value = terminal->value.floating;
                    constraint_satisfied = 
                        (constraint->value.range.min_inclusive ? 
                         value >= constraint->value.range.min : 
                         value > constraint->value.range.min) &&
                        (constraint->value.range.max_inclusive ? 
                         value <= constraint->value.range.max : 
                         value < constraint->value.range.max);
                }
                break;
                
            case MTPNDD_CONSTRAINT_CUSTOM:
                if (constraint->value.custom.validate_fn) {
                    constraint_satisfied = constraint->value.custom.validate_fn(
                        terminal, constraint->value.custom.user_data
                    );
                }
                break;
                
            case MTPNDD_CONSTRAINT_NOT_NULL:
                constraint_satisfied = (terminal != NULL);
                break;
                
            default:
                constraint_satisfied = true;  // 未实现的约束类型默认通过
                break;
        }
        
        if (!constraint_satisfied) {
            constraint->violation_count++;
            g_constraint_stats.failed_validations++;
            g_constraint_stats.constraint_violations++;
            
            result->is_valid = false;
            result->violated_constraint_count++;
            
            // 扩容违反约束列表
            result->violated_constraint_ids = realloc(
                result->violated_constraint_ids,
                result->violated_constraint_count * sizeof(uint32_t)
            );
            result->violation_messages = realloc(
                result->violation_messages,
                result->violated_constraint_count * sizeof(char*)
            );
            
            if (result->violated_constraint_ids && result->violation_messages) {
                result->violated_constraint_ids[result->violated_constraint_count - 1] = 
                    constraint->constraint_id;
                
                char *message = malloc(256);
                if (message) {
                    snprintf(message, 256, "Constraint '%s' violated", constraint->name);
                    result->violation_messages[result->violated_constraint_count - 1] = message;
                }
            }
        } else {
            g_constraint_stats.successful_validations++;
        }
    }
    
    return result;
}

// 验证结果管理
void mtpndd_destroy_validation_result(mtpndd_validation_result_t *result) {
    if (!result) return;
    
    if (result->violated_constraint_ids) {
        free(result->violated_constraint_ids);
    }
    
    if (result->violation_messages) {
        for (uint32_t i = 0; i < result->violated_constraint_count; i++) {
            if (result->violation_messages[i]) {
                free(result->violation_messages[i]);
            }
        }
        free(result->violation_messages);
    }
    
    free(result);
}

bool mtpndd_is_validation_successful(const mtpndd_validation_result_t *result) {
    return result ? result->is_valid : false;
}

// 预定义约束
uint32_t mtpndd_register_positive_constraint(const char *name) {
    return mtpndd_register_range_constraint(
        name, "Positive number constraint (> 0)",
        MTPNDD_TERMINAL_INTEGER, 0.0, 1e9, false, true
    );
}

uint32_t mtpndd_register_non_negative_constraint(const char *name) {
    return mtpndd_register_range_constraint(
        name, "Non-negative number constraint (>= 0)",
        MTPNDD_TERMINAL_INTEGER, 0.0, 1e9, true, true
    );
}

uint32_t mtpndd_register_percentage_constraint(const char *name) {
    return mtpndd_register_range_constraint(
        name, "Percentage constraint [0, 100]",
        MTPNDD_TERMINAL_INTEGER, 0.0, 100.0, true, true
    );
}

uint32_t mtpndd_register_probability_constraint(const char *name) {
    return mtpndd_register_range_constraint(
        name, "Probability constraint [0.0, 1.0]",
        MTPNDD_TERMINAL_DOUBLE, 0.0, 1.0, true, true
    );
}

// 非空字符串验证函数
static bool validate_non_empty_string(
    const mtpndd_terminal_t *terminal, 
    void *user_data
) {
    (void)user_data;
    
    if (!terminal || terminal->type != MTPNDD_TERMINAL_STRING) {
        return false;
    }
    
    return terminal->value.string.length > 0 && 
           terminal->value.string.data && 
           strlen(terminal->value.string.data) > 0;
}

uint32_t mtpndd_register_non_empty_string_constraint(const char *name) {
    return mtpndd_register_custom_constraint(
        name, "Non-empty string constraint",
        MTPNDD_TERMINAL_STRING, validate_non_empty_string, NULL
    );
}

// 获取约束统计信息
mtpndd_constraint_stats_t mtpndd_get_constraint_stats() {
    g_constraint_stats.active_constraints = 0;
    
    // 重新计算激活约束数量
    for (uint32_t i = 0; i < g_constraint_manager.constraint_count; i++) {
        if (g_constraint_manager.constraints[i].is_active) {
            g_constraint_stats.active_constraints++;
        }
    }
    
    return g_constraint_stats;
}

void mtpndd_reset_constraint_stats() {
    memset(&g_constraint_stats, 0, sizeof(mtpndd_constraint_stats_t));
    g_constraint_stats.total_constraints = g_constraint_manager.constraint_count;
    
    // 重置所有约束的统计信息
    for (uint32_t i = 0; i < g_constraint_manager.constraint_count; i++) {
        g_constraint_manager.constraints[i].validation_count = 0;
        g_constraint_manager.constraints[i].violation_count = 0;
    }
}

void mtpndd_print_constraint_stats() {
    printf("=== MTPNDD Constraint Validation Statistics ===\n");
    printf("Total validations: %lu\n", g_constraint_stats.total_validations);
    printf("Successful validations: %lu\n", g_constraint_stats.successful_validations);
    printf("Failed validations: %lu\n", g_constraint_stats.failed_validations);
    printf("Constraint violations: %lu\n", g_constraint_stats.constraint_violations);
    printf("Type violations: %lu\n", g_constraint_stats.type_violations);
    printf("Active constraints: %u\n", g_constraint_stats.active_constraints);
    printf("Total constraints: %u\n", g_constraint_stats.total_constraints);
    printf("Average validation time: %.6f ms\n", g_constraint_stats.average_validation_time);
    
    if (g_constraint_stats.total_validations > 0) {
        double success_rate = (double)g_constraint_stats.successful_validations / 
                             g_constraint_stats.total_validations * 100.0;
        printf("Validation success rate: %.2f%%\n", success_rate);
    }
}