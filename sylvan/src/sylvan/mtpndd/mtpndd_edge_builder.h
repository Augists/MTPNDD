// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_EDGE_BUILDER_H
#define MTPNDD_EDGE_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mtpndd_common.h"
#include "mtpndd_edge_array_pool.h"

typedef struct mtpndd_edge_builder_s {
    mtpndd_edge_record_t *edges;
    size_t capacity;
    size_t count;
} mtpndd_edge_builder_t;

void mtpndd_edge_builder_init(mtpndd_edge_builder_t *builder, size_t initial_capacity);
void mtpndd_edge_builder_reset(mtpndd_edge_builder_t *builder);
void mtpndd_edge_builder_destroy(mtpndd_edge_builder_t *builder);

/**
 * Push an edge into builder.
 * `label` must be a referenced BDD (ownership transferred to builder).
 * Edges to `MTPNDD_FALSE` or with `sylvan_false` label are discarded (label deref'ed).
 */
bool mtpndd_edge_builder_push(mtpndd_edge_builder_t *builder, mtpndd_t child, mtpndd_bdd_t label);

/**
 * Sort edges by child idx and merge duplicates (OR labels).
 * After finalize, builder keeps ownership of all remaining labels.
 */
bool mtpndd_edge_builder_finalize(mtpndd_edge_builder_t *builder);

#endif // MTPNDD_EDGE_BUILDER_H

