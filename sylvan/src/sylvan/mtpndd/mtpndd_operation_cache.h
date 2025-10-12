// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_OPERATION_CACHE_H
#define MTPNDD_OPERATION_CACHE_H

typedef struct mtpndd_op_cache_s {
} mtpndd_op_cache_t;

int mtpndd_op_cache_initialize(
        size_t size,
        mtpndd_op_cache_t **and_cache,
        mtpndd_op_cache_t **or_cache,
        mtpndd_op_cache_t **not_cache);
void mtpndd_op_cache_destroy();
void mtpndd_op_cache_clear(mtpndd_op_cache_t *cache);

#endif // MTPNDD_OPERATION_CACHE_H