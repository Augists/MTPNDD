// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef OPERATION_CACHE_H
#define OPERATION_CACHE_H

#include "common.h"

// 操作缓存结构体
typedef struct operation_cache_s {
    uint32_t cache_size;
    uint32_t entry_size;
    void **cache;  // 扁平数组存储
    void *result;  // 临时存储查找结果
    uint32_t hash_value;  // 临时存储哈希值
} operation_cache_t;

// 操作缓存函数声明
operation_cache_t* operation_cache_create(uint32_t cache_size, uint32_t entry_size);
void operation_cache_destroy(operation_cache_t *cache);

// 设置缓存条目
void operation_cache_set_entry_unary(operation_cache_t *cache, uint32_t index, 
                                     void *operand1, void *result);
void operation_cache_set_entry_binary(operation_cache_t *cache, uint32_t index, 
                                      void *operand1, void *operand2, void *result);

// 获取缓存条目
int operation_cache_get_entry_unary(operation_cache_t *cache, void *operand1);
int operation_cache_get_entry_binary(operation_cache_t *cache, void *operand1, void *operand2);

// 清空缓存
void operation_cache_clear(operation_cache_t *cache);

// 内部辅助函数
void* operation_cache_get_result(operation_cache_t *cache, uint32_t index);
void operation_cache_set_result(operation_cache_t *cache, uint32_t index, void *result);
void* operation_cache_get_operand(operation_cache_t *cache, uint32_t index, uint32_t operand_index);
void operation_cache_set_operand(operation_cache_t *cache, uint32_t index, uint32_t operand_index, void *operand);

// 哈希函数
uint32_t operation_cache_good_hash_unary(operation_cache_t *cache, void *operand1);
uint32_t operation_cache_good_hash_binary(operation_cache_t *cache, void *operand1, void *operand2);

#endif // OPERATION_CACHE_H 