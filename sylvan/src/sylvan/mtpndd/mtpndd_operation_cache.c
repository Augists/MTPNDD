// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_operation_cache.h"
#include "mtpndd_common.h"

#include <stdlib.h>
#include <stdbool.h>
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

static mtpndd_op_cache_t *mtpndd_op_cache_create(size_t requested_size, uint8_t arity) {
    size_t capacity = mtpndd_op_cache_adjust_size(requested_size);
    mtpndd_op_cache_t *cache = (mtpndd_op_cache_t *)malloc(sizeof(mtpndd_op_cache_t));
    if (!cache) {
        return NULL;
    }

    cache->entries = (mtpndd_op_cache_entry_t *)calloc(capacity, sizeof(mtpndd_op_cache_entry_t));
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

static inline size_t mtpndd_op_cache_hash_binary_index(const mtpndd_op_cache_t *cache, const mtpndd_node_t *lhs, const mtpndd_node_t *rhs) {
    uint64_t h1 = mtpndd_hash_node_identity(lhs);
    uint64_t h2 = mtpndd_hash_node_identity(rhs);
    uint64_t hash = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    return (size_t)(hash & cache->mask);
}

static inline size_t mtpndd_op_cache_hash_unary_index(const mtpndd_op_cache_t *cache, const mtpndd_node_t *operand) {
    uint64_t hash = mtpndd_hash_node_identity(operand);
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
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

void mtpndd_op_cache_destroy() {
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
    // Use atomic stores to clear entries
    for (size_t i = 0; i < cache->capacity; ++i) {
        atomic_store_explicit(&cache->entries[i].operands[0], NULL, memory_order_relaxed);
        atomic_store_explicit(&cache->entries[i].operands[1], NULL, memory_order_relaxed);
        atomic_store_explicit(&cache->entries[i].result, NULL, memory_order_relaxed);
    }
}

mtpndd_node_t *mtpndd_op_cache_lookup_binary(mtpndd_op_cache_t *cache, mtpndd_node_t *lhs, mtpndd_node_t *rhs) {
    if (!cache || cache->arity != 2 || cache->capacity == 0) {
        return NULL;
    }
    size_t idx = mtpndd_op_cache_hash_binary_index(cache, lhs, rhs);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];

    // Lock-free read using atomic loads with acquire semantics
    mtpndd_node_t *op0 = atomic_load_explicit(&entry->operands[0], memory_order_acquire);
    mtpndd_node_t *op1 = atomic_load_explicit(&entry->operands[1], memory_order_acquire);
    mtpndd_node_t *res = atomic_load_explicit(&entry->result, memory_order_acquire);

    bool hit = (op0 == lhs && op1 == rhs && res != NULL);
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    if (hit) {
        __atomic_add_fetch(&g_mtpndd_stats.cache_lookup_hits, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&g_mtpndd_stats.cache_lookup_misses, 1, __ATOMIC_RELAXED);
    }
#endif
    return hit ? res : NULL;
}

void mtpndd_op_cache_store_binary(mtpndd_op_cache_t *cache, mtpndd_node_t *lhs, mtpndd_node_t *rhs, mtpndd_node_t *result) {
    if (!cache || cache->arity != 2 || cache->capacity == 0) {
        return;
    }
    if (!result) {
        return;
    }
    size_t idx = mtpndd_op_cache_hash_binary_index(cache, lhs, rhs);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];

#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    __atomic_add_fetch(&g_mtpndd_stats.cache_store_total, 1, __ATOMIC_RELAXED);
    // Check if we're overwriting a valid entry with different operands
    mtpndd_node_t *old_res = atomic_load_explicit(&entry->result, memory_order_relaxed);
    if (old_res != NULL) {
        mtpndd_node_t *old_op0 = atomic_load_explicit(&entry->operands[0], memory_order_relaxed);
        mtpndd_node_t *old_op1 = atomic_load_explicit(&entry->operands[1], memory_order_relaxed);
        if (old_op0 != lhs || old_op1 != rhs) {
            __atomic_add_fetch(&g_mtpndd_stats.cache_store_overwrites, 1, __ATOMIC_RELAXED);
        }
    }
#endif

    // Lock-free write using atomic stores with release semantics
    // Store operands first, then result (result acts as validity flag)
    atomic_store_explicit(&entry->operands[0], lhs, memory_order_relaxed);
    atomic_store_explicit(&entry->operands[1], rhs, memory_order_relaxed);
    atomic_store_explicit(&entry->result, result, memory_order_release);
}

mtpndd_node_t *mtpndd_op_cache_lookup_unary(mtpndd_op_cache_t *cache, mtpndd_node_t *operand) {
    if (!cache || cache->arity != 1 || cache->capacity == 0) {
        return NULL;
    }
    size_t idx = mtpndd_op_cache_hash_unary_index(cache, operand);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];

    // Lock-free read using atomic loads with acquire semantics
    mtpndd_node_t *op0 = atomic_load_explicit(&entry->operands[0], memory_order_acquire);
    mtpndd_node_t *res = atomic_load_explicit(&entry->result, memory_order_acquire);

    bool hit = (op0 == operand && res != NULL);
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    if (hit) {
        __atomic_add_fetch(&g_mtpndd_stats.cache_lookup_hits, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&g_mtpndd_stats.cache_lookup_misses, 1, __ATOMIC_RELAXED);
    }
#endif
    return hit ? res : NULL;
}

void mtpndd_op_cache_store_unary(mtpndd_op_cache_t *cache, mtpndd_node_t *operand, mtpndd_node_t *result) {
    if (!cache || cache->arity != 1 || cache->capacity == 0) {
        return;
    }
    if (!result) {
        return;
    }
    size_t idx = mtpndd_op_cache_hash_unary_index(cache, operand);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];

    // Lock-free write using atomic stores with release semantics
    atomic_store_explicit(&entry->operands[0], operand, memory_order_relaxed);
    atomic_store_explicit(&entry->operands[1], NULL, memory_order_relaxed);
    atomic_store_explicit(&entry->result, result, memory_order_release);
}
