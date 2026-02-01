// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_edge_array_pool.h"

#include <stdlib.h>
#include <string.h>

#include "lace.h"

// Batch size for per-worker local buffer refills (conservative, matches feature/c)
#define MTPNDD_EDGE_LOCAL_REFILL_BATCH 32

// Debug: Track allocations to detect overlaps
#ifdef MTPNDD_DEBUG_ALLOC
#include <stdio.h>
static _Atomic size_t g_alloc_count = 0;
static _Atomic size_t g_fast_count = 0;
static _Atomic size_t g_slow_count = 0;
static _Atomic size_t g_refill_count = 0;

#define DEBUG_ALLOC(fmt, ...) fprintf(stderr, "[ALLOC] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_ALLOC(fmt, ...) ((void)0)
#endif

static inline int mtpndd_worker_id(void) {
    WorkerP *w = lace_get_worker();
    if (!w) return -1;
    return (int)w->worker;
}

static size_t mtpndd_next_capacity(size_t current, size_t required) {
    size_t cap = current ? current : 1024;
    while (cap < required) {
        size_t next = cap << 1;
        if (next <= cap) {
            return required;
        }
        cap = next;
    }
    return cap;
}

void mtpndd_edge_array_pool_init(mtpndd_edge_array_pool_t *pool, size_t initial_capacity) {
    if (!pool) {
        return;
    }

#ifdef MTPNDD_DEBUG_ALLOC
    size_t old_size = atomic_load_explicit(&pool->size, memory_order_relaxed);
    fprintf(stderr, "[POOL] Init: old_size=%zu (before memset)\n", old_size);
#endif

    memset(pool, 0, sizeof(*pool));
    pthread_mutex_init(&pool->mutex, NULL);  // CRITICAL: Init mutex
    atomic_store_explicit(&pool->size, 0, memory_order_relaxed);

#ifdef MTPNDD_DEBUG_ALLOC
    fprintf(stderr, "[POOL] Init: size=%zu, initial_capacity=%zu\n",
            (size_t)atomic_load_explicit(&pool->size, memory_order_relaxed), initial_capacity);
#endif

    if (initial_capacity == 0) {
        return;
    }
    pool->data = (mtpndd_edge_record_t *)calloc(initial_capacity, sizeof(mtpndd_edge_record_t));
    if (!pool->data) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return;
    }
    pool->capacity = initial_capacity;
    // size already initialized to 0 above
}

void mtpndd_edge_array_pool_destroy(mtpndd_edge_array_pool_t *pool) {
    if (!pool) {
        return;
    }

#ifdef MTPNDD_DEBUG_ALLOC
    size_t final_size = atomic_load_explicit(&pool->size, memory_order_relaxed);
    fprintf(stderr, "[POOL] Destroy: final_size=%zu, capacity=%zu\n", final_size, pool->capacity);
#endif

    pthread_mutex_destroy(&pool->mutex);

    // Free per-worker local buffers
    if (pool->local_buffers) {
        free(pool->local_buffers);
        pool->local_buffers = NULL;
        pool->local_buffer_count = 0;
    }

    free(pool->data);
    pool->data = NULL;
    pool->capacity = 0;
    atomic_store_explicit(&pool->size, 0, memory_order_relaxed);
}

// Helper: Grow pool capacity to fit required size (mutex must be held)
static bool mtpndd_edge_pool_grow_locked(mtpndd_edge_array_pool_t *pool, size_t required) {
    if (required <= pool->capacity) {
        return true;  // Already sufficient
    }

    size_t new_capacity = mtpndd_next_capacity(pool->capacity, required);
    mtpndd_edge_record_t *new_data =
        (mtpndd_edge_record_t *)realloc(pool->data, new_capacity * sizeof(mtpndd_edge_record_t));
    if (!new_data) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return false;
    }

    // Zero new space
    if (new_capacity > pool->capacity) {
        memset(new_data + pool->capacity, 0,
               (new_capacity - pool->capacity) * sizeof(mtpndd_edge_record_t));
    }

    pool->data = new_data;
    pool->capacity = new_capacity;
    return true;
}

// Helper: Refill worker's local buffer by batch-allocating from global pool
static bool mtpndd_edge_local_refill(mtpndd_edge_array_pool_t *pool,
                                      mtpndd_edge_local_buffer_t *local,
                                      uint32_t count) {
    // Determine batch size: max of requested count and refill batch
    uint32_t batch = (count > MTPNDD_EDGE_LOCAL_REFILL_BATCH) ? count : MTPNDD_EDGE_LOCAL_REFILL_BATCH;

    // Atomically reserve a chunk from global pool
    size_t start = atomic_fetch_add_explicit(&pool->size, (size_t)batch, memory_order_relaxed);
    size_t required = start + (size_t)batch;

    // Check for overflow
    if (start > (size_t)UINT32_MAX || required > (size_t)UINT32_MAX) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
        return false;
    }

    // Check if capacity growth needed (mutex-protected)
    if (required > pool->capacity) {
        pthread_mutex_lock(&pool->mutex);
        bool grew = mtpndd_edge_pool_grow_locked(pool, required);
        pthread_mutex_unlock(&pool->mutex);
        if (!grew) {
            return false;
        }
    }

    // Update local buffer range
    local->start = (uint32_t)start;
    local->end = (uint32_t)required;
    local->current = local->start;

#ifdef MTPNDD_DEBUG_ALLOC
    size_t refill_num = atomic_fetch_add_explicit(&g_refill_count, 1, memory_order_relaxed);
    DEBUG_ALLOC("REFILL #%zu: wid=%d, start=%u, end=%u, batch=%u",
                refill_num, mtpndd_worker_id(), local->start, local->end, batch);
#endif

    return true;
}

uint32_t mtpndd_edge_array_pool_alloc(mtpndd_edge_array_pool_t *pool, uint32_t count) {
    if (!pool) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NULL_POINTER);
        return UINT32_MAX;
    }
    if (count == 0) {
        return 0;
    }

    // Lazy initialization of local buffers (first allocation after Lace startup)
    if (!pool->local_buffers) {
        size_t nworkers = lace_workers();
        if (nworkers > 0) {
            pthread_mutex_lock(&pool->mutex);
            // Double-check after acquiring lock
            if (!pool->local_buffers) {
                pool->local_buffers = (mtpndd_edge_local_buffer_t *)calloc(
                    nworkers, sizeof(mtpndd_edge_local_buffer_t));
                if (pool->local_buffers) {
                    pool->local_buffer_count = nworkers;
                }
            }
            pthread_mutex_unlock(&pool->mutex);
        }
    }

    // Fast path: Try to allocate from worker-local buffer
    int wid = mtpndd_worker_id();
    if (wid >= 0 && pool->local_buffers && (size_t)wid < pool->local_buffer_count) {
        mtpndd_edge_local_buffer_t *local = &pool->local_buffers[wid];

        // Check if local buffer has enough space
        uint32_t available = local->end - local->current;
        if (available >= count) {
            // Fast path: allocate from local buffer (NO ATOMIC OPERATIONS!)
            uint32_t result = local->current;
            local->current += count;
#ifdef MTPNDD_DEBUG_ALLOC
            size_t num = atomic_fetch_add_explicit(&g_fast_count, 1, memory_order_relaxed);
            if (num < 20 || num % 10000 == 0) {
                DEBUG_ALLOC("FAST #%zu: wid=%d, idx=%u, count=%u (from local [%u,%u), current->%u)",
                            num, wid, result, count, local->start, local->end, local->current);
            }
#endif
            return result;
        }

        // Local buffer exhausted: refill from global pool
        if (!mtpndd_edge_local_refill(pool, local, count)) {
            return UINT32_MAX;
        }

        // Retry allocation from refilled buffer
        available = local->end - local->current;
        if (available >= count) {
            uint32_t result = local->current;
            local->current += count;
#ifdef MTPNDD_DEBUG_ALLOC
            size_t num = atomic_fetch_add_explicit(&g_fast_count, 1, memory_order_relaxed);
            DEBUG_ALLOC("FAST-RETRY #%zu: wid=%d, idx=%u, count=%u (after refill [%u,%u), current->%u)",
                        num, wid, result, count, local->start, local->end, local->current);
#endif
            return result;
        }

        // Should not happen if refill worked correctly
        MTPNDD_SET_ERROR(MTPNDD_ERROR_UNKNOWN);
        return UINT32_MAX;
    }

    // Slow path: Non-Lace thread or local buffers not initialized
    // Fall back to global atomic allocation
    size_t start = atomic_fetch_add_explicit(&pool->size, (size_t)count, memory_order_relaxed);
    size_t required = start + (size_t)count;

#ifdef MTPNDD_DEBUG_ALLOC
    size_t slow_num = atomic_fetch_add_explicit(&g_slow_count, 1, memory_order_relaxed);
    DEBUG_ALLOC("SLOW #%zu: wid=%d, idx=%zu, count=%u (global size %zu->%zu)",
                slow_num, wid, start, count, start, required);
#endif

    if (start > (size_t)UINT32_MAX || required > (size_t)UINT32_MAX) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
        return UINT32_MAX;
    }

    // Check if capacity growth needed
    if (required > pool->capacity) {
        pthread_mutex_lock(&pool->mutex);
        bool grew = mtpndd_edge_pool_grow_locked(pool, required);
        pthread_mutex_unlock(&pool->mutex);
        if (!grew) {
            return UINT32_MAX;
        }
    }

    return (uint32_t)start;
}
