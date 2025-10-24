// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_operation_cache.h"
#include <stdlib.h>

int mtpndd_op_cache_initialize(
        size_t size,
        mtpndd_op_cache_t **and_cache,
        mtpndd_op_cache_t **or_cache,
        mtpndd_op_cache_t **not_cache)
{
    (void)size;
    if (and_cache) *and_cache = NULL;
    if (or_cache) *or_cache = NULL;
    if (not_cache) *not_cache = NULL;
    return 1;
}

void mtpndd_op_cache_destroy() {
    /* no-op placeholder until cache is implemented */
}

void mtpndd_op_cache_clear(mtpndd_op_cache_t *cache) {
    (void)cache;
}
