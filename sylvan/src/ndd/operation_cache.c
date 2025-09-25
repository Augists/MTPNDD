// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "operation_cache.h"
#include <stdlib.h>
#include <string.h>

operation_cache_t* operation_cache_create(uint32_t cache_size, uint32_t entry_size) {
    operation_cache_t *cache = (operation_cache_t*)malloc(sizeof(operation_cache_t));
    cache->cache_size = cache_size;
    cache->entry_size = entry_size;
    cache->cache = (void**)calloc(cache_size * entry_size, sizeof(void*));
    cache->result = NULL;
    cache->hash_value = 0;
    return cache;
}

void operation_cache_destroy(operation_cache_t *cache) {
    if (cache) {
        free(cache->cache);
        free(cache);
    }
}

void* operation_cache_get_result(operation_cache_t *cache, uint32_t index) {
    // Boundary check
    if (!cache || index >= cache->cache_size) {
        return NULL;
    }
    return cache->cache[index * cache->entry_size];
}

void operation_cache_set_result(operation_cache_t *cache, uint32_t index, void *result) {
    // Boundary check
    if (!cache || index >= cache->cache_size) {
        return;
    }
    cache->cache[index * cache->entry_size] = result;
}

void* operation_cache_get_operand(operation_cache_t *cache, uint32_t index, uint32_t operand_index) {
    // Boundary check
    if (!cache || index >= cache->cache_size || operand_index >= cache->entry_size) {
        return NULL;
    }
    return cache->cache[index * cache->entry_size + operand_index];
}

void operation_cache_set_operand(operation_cache_t *cache, uint32_t index, uint32_t operand_index, void *operand) {
    // Boundary check
    if (!cache || index >= cache->cache_size || operand_index >= cache->entry_size) {
        return;
    }
    cache->cache[index * cache->entry_size + operand_index] = operand;
}

void operation_cache_set_entry_unary(operation_cache_t *cache, uint32_t index, 
                                     void *operand1, void *result) {
    operation_cache_set_operand(cache, index, 1, operand1);
    operation_cache_set_result(cache, index, result);
}

void operation_cache_set_entry_binary(operation_cache_t *cache, uint32_t index, 
                                      void *operand1, void *operand2, void *result) {
    operation_cache_set_operand(cache, index, 1, operand1);
    operation_cache_set_operand(cache, index, 2, operand2);
    operation_cache_set_result(cache, index, result);
}

int operation_cache_get_entry_unary(operation_cache_t *cache, void *operand1) {
    if (!cache || !operand1) return 0;
    
    uint32_t hash = operation_cache_good_hash_unary(cache, operand1);
    if (operation_cache_get_operand(cache, hash, 1) == operand1) {
        cache->result = operation_cache_get_result(cache, hash);
        return 1;  // Found
    } else {
        cache->hash_value = hash;
        return 0;  // Not found
    }
}

int operation_cache_get_entry_binary(operation_cache_t *cache, void *operand1, void *operand2) {
    if (!cache || !operand1 || !operand2) return 0;
    
    uint32_t hash = operation_cache_good_hash_binary(cache, operand1, operand2);
    void *cached_op1 = operation_cache_get_operand(cache, hash, 1);
    void *cached_op2 = operation_cache_get_operand(cache, hash, 2);
    
    if ((cached_op1 == operand1 && cached_op2 == operand2) ||
        (cached_op1 == operand2 && cached_op2 == operand1)) {
        cache->result = operation_cache_get_result(cache, hash);
        return 1;  // Found
    } else {
        cache->hash_value = hash;
        return 0;  // Not found
    }
}

uint32_t operation_cache_good_hash_unary(operation_cache_t *cache, void *operand1) {
    // Java: Math.abs(operand1.hashCode()) % cacheSize
    uintptr_t addr = (uintptr_t)operand1;
    uint32_t hash = (uint32_t)(addr ^ (addr >> 32));
    return hash % cache->cache_size;
}

uint32_t operation_cache_good_hash_binary(operation_cache_t *cache, void *operand1, void *operand2) {
    // Java: Math.abs((long)operand1.hashCode() + (long)operand2.hashCode()) % cacheSize
    uintptr_t addr1 = (uintptr_t)operand1;
    uintptr_t addr2 = (uintptr_t)operand2;
    uint64_t combined = (uint64_t)addr1 + (uint64_t)addr2;
    uint32_t hash = (uint32_t)(combined ^ (combined >> 32));
    return hash % cache->cache_size;
}

void operation_cache_clear(operation_cache_t *cache) {
    // Java: cache = new Object[cacheSize * entrySize];
    memset(cache->cache, 0, cache->cache_size * cache->entry_size * sizeof(void*));
} 