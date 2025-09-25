// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef OPERATION_CACHE_H
#define OPERATION_CACHE_H

#include "common.h"

// Operation cache structure
typedef struct operation_cache_s {
    uint32_t cache_size;
    uint32_t entry_size;
    void **cache;  // Flat array storage
    void *result;  // Temporary storage for lookup result
    uint32_t hash_value;  // Temporary storage for hash value
} operation_cache_t;

// Operation cache function declarations
operation_cache_t* operation_cache_create(uint32_t cache_size, uint32_t entry_size);
void operation_cache_destroy(operation_cache_t *cache);

// Set cache entries
void operation_cache_set_entry_unary(operation_cache_t *cache, uint32_t index, 
                                     void *operand1, void *result);
void operation_cache_set_entry_binary(operation_cache_t *cache, uint32_t index, 
                                      void *operand1, void *operand2, void *result);

// Get cache entries
int operation_cache_get_entry_unary(operation_cache_t *cache, void *operand1);
int operation_cache_get_entry_binary(operation_cache_t *cache, void *operand1, void *operand2);

// Clear cache
void operation_cache_clear(operation_cache_t *cache);

// Internal helper functions
void* operation_cache_get_result(operation_cache_t *cache, uint32_t index);
void operation_cache_set_result(operation_cache_t *cache, uint32_t index, void *result);
void* operation_cache_get_operand(operation_cache_t *cache, uint32_t index, uint32_t operand_index);
void operation_cache_set_operand(operation_cache_t *cache, uint32_t index, uint32_t operand_index, void *operand);

// Hash functions
uint32_t operation_cache_good_hash_unary(operation_cache_t *cache, void *operand1);
uint32_t operation_cache_good_hash_binary(operation_cache_t *cache, void *operand1, void *operand2);

#endif // OPERATION_CACHE_H 