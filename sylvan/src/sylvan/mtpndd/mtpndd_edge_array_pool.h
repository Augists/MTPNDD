// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_EDGE_ARRAY_POOL_H
#define MTPNDD_EDGE_ARRAY_POOL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "mtpndd_common.h"

typedef struct mtpndd_edge_record_s {
    mtpndd_t child;
    mtpndd_bdd_t label;
} mtpndd_edge_record_t;

_Static_assert(sizeof(mtpndd_edge_record_t) == sizeof(uint64_t) * 2, "edge record must be 2x u64");

// Per-worker local allocation buffer to reduce atomic contention.
// Each worker maintains a private range [start, end) and allocates from it without atomics.
typedef struct mtpndd_edge_local_buffer_s {
    uint32_t start;      // Start index of local chunk (inclusive)
    uint32_t end;        // End index of local chunk (exclusive)
    uint32_t current;    // Current allocation position within [start, end)
} mtpndd_edge_local_buffer_t;

typedef struct mtpndd_edge_array_pool_s {
    mtpndd_edge_record_t *data;
    size_t capacity;
    _Atomic size_t size;  // CRITICAL: Atomic for concurrent allocation
    pthread_mutex_t mutex;  // Protects capacity growth (realloc)

    // Per-worker local buffers (lazily initialized after Lace startup)
    mtpndd_edge_local_buffer_t *local_buffers;
    size_t local_buffer_count;
} mtpndd_edge_array_pool_t;

void mtpndd_edge_array_pool_init(mtpndd_edge_array_pool_t *pool, size_t initial_capacity);
void mtpndd_edge_array_pool_destroy(mtpndd_edge_array_pool_t *pool);

/**
 * Allocate `count` consecutive edge records, returning the start index.
 * Returns 0 when count==0.
 * On OOM, returns UINT32_MAX and sets last error.
 */
uint32_t mtpndd_edge_array_pool_alloc(mtpndd_edge_array_pool_t *pool, uint32_t count);

static inline mtpndd_edge_record_t *mtpndd_edge_array_pool_at(mtpndd_edge_array_pool_t *pool, uint32_t idx) {
    return pool && pool->data ? &pool->data[idx] : NULL;
}

static inline const mtpndd_edge_record_t *mtpndd_edge_array_pool_at_const(const mtpndd_edge_array_pool_t *pool, uint32_t idx) {
    return pool && pool->data ? &pool->data[idx] : NULL;
}

#endif // MTPNDD_EDGE_ARRAY_POOL_H

