// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_edge_array_pool.h"

#include <stdlib.h>
#include <string.h>

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
    memset(pool, 0, sizeof(*pool));
    if (initial_capacity == 0) {
        return;
    }
    pool->data = (mtpndd_edge_record_t *)calloc(initial_capacity, sizeof(mtpndd_edge_record_t));
    if (!pool->data) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return;
    }
    pool->capacity = initial_capacity;
    pool->size = 0;
}

void mtpndd_edge_array_pool_destroy(mtpndd_edge_array_pool_t *pool) {
    if (!pool) {
        return;
    }
    free(pool->data);
    pool->data = NULL;
    pool->capacity = 0;
    pool->size = 0;
}

uint32_t mtpndd_edge_array_pool_alloc(mtpndd_edge_array_pool_t *pool, uint32_t count) {
    if (!pool) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NULL_POINTER);
        return UINT32_MAX;
    }
    if (count == 0) {
        return 0;
    }
    if (pool->size > (size_t)UINT32_MAX || count > UINT32_MAX) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
        return UINT32_MAX;
    }

    size_t start = pool->size;
    size_t required = start + (size_t)count;
    if (required > pool->capacity) {
        size_t new_capacity = mtpndd_next_capacity(pool->capacity, required);
        mtpndd_edge_record_t *new_data =
                (mtpndd_edge_record_t *)realloc(pool->data, new_capacity * sizeof(mtpndd_edge_record_t));
        if (!new_data) {
            MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
            return UINT32_MAX;
        }
        if (new_capacity > pool->capacity) {
            memset(new_data + pool->capacity, 0,
                   (new_capacity - pool->capacity) * sizeof(mtpndd_edge_record_t));
        }
        pool->data = new_data;
        pool->capacity = new_capacity;
    }

    pool->size = required;
    return (uint32_t)start;
}
