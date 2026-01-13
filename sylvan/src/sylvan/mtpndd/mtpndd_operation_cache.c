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
    memset(cache->entries, 0, cache->capacity * sizeof(mtpndd_op_cache_entry_t));
}

mtpndd_node_t *mtpndd_op_cache_lookup_binary(mtpndd_op_cache_t *cache, mtpndd_node_t *lhs, mtpndd_node_t *rhs) {
    if (!cache || cache->arity != 2 || cache->capacity == 0) {
        return NULL;
    }
    uint64_t h1 = mtpndd_hash_node_identity(lhs);
    uint64_t h2 = mtpndd_hash_node_identity(rhs);
    uint64_t hash = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    size_t idx = (size_t)(hash & cache->mask);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];
    mtpndd_node_t *result = NULL;
    bool hit = (entry->operands[0] == lhs && entry->operands[1] == rhs && entry->result);
#ifdef ENABLE_RECORDING
    if (hit) {
        result = entry->result;
        MTPNDD_STAT_ADD(cache_lookup_hits, 1);
    } else {
        MTPNDD_STAT_ADD(cache_lookup_misses, 1);
    }
#else
    if (hit) {
        result = entry->result;
    }
#endif
    return result;
}

void mtpndd_op_cache_store_binary(mtpndd_op_cache_t *cache, mtpndd_node_t *lhs, mtpndd_node_t *rhs, mtpndd_node_t *result) {
    if (!cache || cache->arity != 2 || cache->capacity == 0) {
        return;
    }
    if (!result) {
        return;
    }
    uint64_t h1 = mtpndd_hash_node_identity(lhs);
    uint64_t h2 = mtpndd_hash_node_identity(rhs);
    uint64_t hash = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    size_t idx = (size_t)(hash & cache->mask);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(cache_store_total, 1);
    // Check if we're overwriting a valid entry with different operands
    if (entry->result != NULL &&
        (entry->operands[0] != lhs || entry->operands[1] != rhs)) {
        MTPNDD_STAT_ADD(cache_store_overwrites, 1);
    }
#endif
    entry->operands[0] = lhs;
    entry->operands[1] = rhs;
    entry->result = result;
}

mtpndd_node_t *mtpndd_op_cache_lookup_unary(mtpndd_op_cache_t *cache, mtpndd_node_t *operand) {
    if (!cache || cache->arity != 1 || cache->capacity == 0) {
        return NULL;
    }
    uint64_t hash = mtpndd_hash_node_identity(operand);
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    size_t idx = (size_t)(hash & cache->mask);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];
    mtpndd_node_t *result = NULL;
    bool hit = (entry->operands[0] == operand && entry->result);
#ifdef ENABLE_RECORDING
    if (hit) {
        result = entry->result;
        MTPNDD_STAT_ADD(cache_lookup_hits, 1);
    } else {
        MTPNDD_STAT_ADD(cache_lookup_misses, 1);
    }
#else
    if (hit) {
        result = entry->result;
    }
#endif
    return result;
}

void mtpndd_op_cache_store_unary(mtpndd_op_cache_t *cache, mtpndd_node_t *operand, mtpndd_node_t *result) {
    if (!cache || cache->arity != 1 || cache->capacity == 0) {
        return;
    }
    if (!result) {
        return;
    }
    uint64_t hash = mtpndd_hash_node_identity(operand);
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    size_t idx = (size_t)(hash & cache->mask);
    mtpndd_op_cache_entry_t *entry = &cache->entries[idx];
    entry->operands[0] = operand;
    entry->result = result;
    entry->operands[1] = NULL;
}
