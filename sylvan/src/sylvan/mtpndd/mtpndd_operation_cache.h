// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_OPERATION_CACHE_H
#define MTPNDD_OPERATION_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mtpndd_common.h"

typedef struct mtpndd_op_cache_entry_s {
    mtpndd_t operands[2];
    uint64_t result_plus_one; // 0 means empty; otherwise (result = result_plus_one - 1)
} mtpndd_op_cache_entry_t;

typedef struct mtpndd_op_cache_s {
    mtpndd_op_cache_entry_t *entries;
    size_t capacity;
    size_t mask;
    uint8_t arity;
} mtpndd_op_cache_t;

bool mtpndd_op_cache_initialize(
        size_t size,
        mtpndd_op_cache_t **and_cache,
        mtpndd_op_cache_t **or_cache,
        mtpndd_op_cache_t **not_cache);
void mtpndd_op_cache_destroy(void);
void mtpndd_op_cache_clear(mtpndd_op_cache_t *cache);

mtpndd_t mtpndd_op_cache_lookup_binary(mtpndd_op_cache_t *cache, mtpndd_t lhs, mtpndd_t rhs);
void mtpndd_op_cache_store_binary(mtpndd_op_cache_t *cache, mtpndd_t lhs, mtpndd_t rhs, mtpndd_t result);

mtpndd_t mtpndd_op_cache_lookup_unary(mtpndd_op_cache_t *cache, mtpndd_t operand);
void mtpndd_op_cache_store_unary(mtpndd_op_cache_t *cache, mtpndd_t operand, mtpndd_t result);

#endif // MTPNDD_OPERATION_CACHE_H

