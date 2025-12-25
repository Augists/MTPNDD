// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_operation_cache.h"

#include <stdlib.h>
#include <string.h>

static const size_t kMtpnddOpCacheMinSize = 1024;

static size_t mtpndd_op_cache_adjust_size(size_t requested) {
    size_t capacity = 1;
    while (capacity < requested) {
        if (capacity > (SIZE_MAX >> 1)) {
            capacity = SIZE_MAX & ~(SIZE_MAX >> 1);
            break;
        }
        capacity <<= 1;
    }
    return capacity ? capacity : kMtpnddOpCacheMinSize;
}

// N-way set-associative cache: each set has N entries (1 = direct mapped)
#define MTPNDD_CACHE_WAYS 2

static mtpndd_op_cache_t *mtpndd_op_cache_create(size_t requested_size, uint8_t arity) {
    size_t capacity = mtpndd_op_cache_adjust_size(requested_size);
    mtpndd_op_cache_t *cache = (mtpndd_op_cache_t *)malloc(sizeof(mtpndd_op_cache_t));
    if (!cache) {
        return NULL;
    }

    // Allocate capacity * WAYS entries for 2-way associativity
    cache->entries = (mtpndd_op_cache_entry_t *)calloc(capacity * MTPNDD_CACHE_WAYS, sizeof(mtpndd_op_cache_entry_t));
    if (!cache->entries) {
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->mask = capacity - 1;
    cache->arity = arity;
    return cache;
}

static void mtpndd_op_cache_release(mtpndd_op_cache_t *cache) {
    if (!cache) {
        return;
    }
    free(cache->entries);
    free(cache);
}

static inline size_t mtpndd_op_cache_hash_binary_index(const mtpndd_op_cache_t *cache, mtpndd_t lhs, mtpndd_t rhs) {
    // Use symmetric hash (addition) so that hash(a,b) == hash(b,a) for commutative ops
    uint64_t hash = lhs + rhs;
    return (size_t)(hash & cache->mask);
}

static inline size_t mtpndd_op_cache_hash_unary_index(const mtpndd_op_cache_t *cache, mtpndd_t operand) {
    uint64_t hash = mtpndd_hash_u64(operand);
    return (size_t)(hash & cache->mask);
}

bool mtpndd_op_cache_initialize(
        size_t size,
        mtpndd_op_cache_t **and_cache,
        mtpndd_op_cache_t **or_cache,
        mtpndd_op_cache_t **not_cache)
{
    mtpndd_op_cache_t *andc = mtpndd_op_cache_create(size, 2);
    if (!andc) {
        return false;
    }

    mtpndd_op_cache_t *orc = mtpndd_op_cache_create(size, 2);
    if (!orc) {
        mtpndd_op_cache_release(andc);
        return false;
    }

    mtpndd_op_cache_t *notc = mtpndd_op_cache_create(size, 1);
    if (!notc) {
        mtpndd_op_cache_release(andc);
        mtpndd_op_cache_release(orc);
        return false;
    }

    *and_cache = andc;
    *or_cache = orc;
    *not_cache = notc;
    return true;
}

void mtpndd_op_cache_destroy(void) {
    mtpndd_op_cache_release(g_mtpndd_config.and_cache);
    mtpndd_op_cache_release(g_mtpndd_config.or_cache);
    mtpndd_op_cache_release(g_mtpndd_config.not_cache);
    g_mtpndd_config.and_cache = NULL;
    g_mtpndd_config.or_cache = NULL;
    g_mtpndd_config.not_cache = NULL;
}

void mtpndd_op_cache_clear(mtpndd_op_cache_t *cache) {
    if (!cache || !cache->entries) {
        return;
    }
    memset(cache->entries, 0, cache->capacity * MTPNDD_CACHE_WAYS * sizeof(mtpndd_op_cache_entry_t));
}

mtpndd_t mtpndd_op_cache_lookup_binary(mtpndd_op_cache_t *cache, mtpndd_t lhs, mtpndd_t rhs) {
    if (!cache || cache->arity != 2 || cache->capacity == 0) {
        return MTPNDD_INVALID;
    }
    size_t idx = mtpndd_op_cache_hash_binary_index(cache, lhs, rhs);
    size_t base = idx * MTPNDD_CACHE_WAYS;

    // Check both ways in the set, also check swapped operands for commutative ops
    for (int w = 0; w < MTPNDD_CACHE_WAYS; w++) {
        mtpndd_op_cache_entry_t *entry = &cache->entries[base + w];
        if (entry->result_plus_one != 0 &&
            ((entry->operands[0] == lhs && entry->operands[1] == rhs) ||
             (entry->operands[0] == rhs && entry->operands[1] == lhs))) {
            return (mtpndd_t)(entry->result_plus_one - 1);
        }
    }
    return MTPNDD_INVALID;
}

void mtpndd_op_cache_store_binary(mtpndd_op_cache_t *cache, mtpndd_t lhs, mtpndd_t rhs, mtpndd_t result) {
    if (!cache || cache->arity != 2 || cache->capacity == 0) {
        return;
    }
    if (result == MTPNDD_INVALID) {
        return;
    }
    size_t idx = mtpndd_op_cache_hash_binary_index(cache, lhs, rhs);
    size_t base = idx * MTPNDD_CACHE_WAYS;

    // First, try to find an empty slot
    for (int w = 0; w < MTPNDD_CACHE_WAYS; w++) {
        mtpndd_op_cache_entry_t *entry = &cache->entries[base + w];
        if (entry->result_plus_one == 0) {
            entry->operands[0] = lhs;
            entry->operands[1] = rhs;
            entry->result_plus_one = (uint64_t)result + 1;
            return;
        }
    }

    // No empty slot, replace the first slot (simple replacement policy)
    mtpndd_op_cache_entry_t *entry = &cache->entries[base];
    entry->operands[0] = lhs;
    entry->operands[1] = rhs;
    entry->result_plus_one = (uint64_t)result + 1;
}

mtpndd_t mtpndd_op_cache_lookup_unary(mtpndd_op_cache_t *cache, mtpndd_t operand) {
    if (!cache || cache->arity != 1 || cache->capacity == 0) {
        return MTPNDD_INVALID;
    }
    size_t idx = mtpndd_op_cache_hash_unary_index(cache, operand);
    size_t base = idx * MTPNDD_CACHE_WAYS;

    // Check both ways in the set
    for (int w = 0; w < MTPNDD_CACHE_WAYS; w++) {
        mtpndd_op_cache_entry_t *entry = &cache->entries[base + w];
        if (entry->result_plus_one != 0 && entry->operands[0] == operand) {
            return (mtpndd_t)(entry->result_plus_one - 1);
        }
    }
    return MTPNDD_INVALID;
}

void mtpndd_op_cache_store_unary(mtpndd_op_cache_t *cache, mtpndd_t operand, mtpndd_t result) {
    if (!cache || cache->arity != 1 || cache->capacity == 0) {
        return;
    }
    if (result == MTPNDD_INVALID) {
        return;
    }
    size_t idx = mtpndd_op_cache_hash_unary_index(cache, operand);
    size_t base = idx * MTPNDD_CACHE_WAYS;

    // First, try to find an empty slot
    for (int w = 0; w < MTPNDD_CACHE_WAYS; w++) {
        mtpndd_op_cache_entry_t *entry = &cache->entries[base + w];
        if (entry->result_plus_one == 0) {
            entry->operands[0] = operand;
            entry->operands[1] = MTPNDD_INVALID;
            entry->result_plus_one = (uint64_t)result + 1;
            return;
        }
    }

    // No empty slot, replace the first slot (simple replacement policy)
    mtpndd_op_cache_entry_t *entry = &cache->entries[base];
    entry->operands[0] = operand;
    entry->operands[1] = MTPNDD_INVALID;
    entry->result_plus_one = (uint64_t)result + 1;
}

