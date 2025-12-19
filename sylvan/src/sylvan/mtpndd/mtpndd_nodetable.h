// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODETABLE_H
#define MTPNDD_NODETABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mtpndd_common.h"
#include "mtpndd_edge_array_pool.h"
#include "mtpndd_edge_builder.h"

typedef struct mtpndd_node_record_s {
    uint32_t field_id;
    uint32_t ref_count;
    uint32_t edge_array_idx;
    uint32_t edge_num;
} mtpndd_node_record_t;

#define MTPNDD_REFCOUNT_PROTECTED UINT32_MAX

typedef struct mtpndd_nodetable_s {
    // Open addressing hash table.
    // Each slot stores a packed 64-bit value:
    //   [ 32-bit hash fingerprint | 32-bit node idx ]
    // where idx==0 means empty (we never store terminals 0/1).
    // The fingerprint lets us skip expensive edge comparisons when hashes differ (Sylvan-style).
    uint64_t *hash;
    size_t hash_capacity;
    size_t hash_mask;
    size_t hash_count;

    // node records indexed by idx
    mtpndd_node_record_t *data;
    size_t data_capacity;
    size_t data_size; // next idx (>=2)

    // free-list of reusable node indices (0 means empty)
    uint32_t free_list_head;

    // backing storage for all edges (append-only, optional compact on GC)
    mtpndd_edge_array_pool_t edge_pool;
} mtpndd_nodetable_t;

extern mtpndd_nodetable_t g_mtpndd_nodetable;

void mtpndd_nodetable_init(mtpndd_nodetable_t *table, size_t node_capacity_hint, size_t hash_capacity_hint, size_t edge_capacity_hint);
void mtpndd_nodetable_destroy(mtpndd_nodetable_t *table);

/**
 * Stop-the-world reclamation for the serial MTPNDD nodetable.
 * Returns number of reclaimed nodes.
 *
 * Notes:
 * - This may rebuild the internal hash table.
 * - This may compact the edge pool (edge_array_pool) when fragmentation is high.
 */
size_t mtpndd_nodetable_collect_garbage(mtpndd_nodetable_t *table);

/**
 * Unique a node by (field_id, edges).
 * `builder` must have been finalized; on return it is consumed (builder->count becomes 0).
 * Returns node idx, or MTPNDD_INVALID on error.
 */
mtpndd_t mtpndd_mk(uint32_t field_id, mtpndd_edge_builder_t *builder);

void mtpndd_ref(mtpndd_t node);
void mtpndd_deref(mtpndd_t node);
void mtpndd_protect(mtpndd_t node);
void mtpndd_unprotect(mtpndd_t node);

static inline mtpndd_node_record_t *mtpndd_node_record(mtpndd_nodetable_t *table, mtpndd_t idx) {
    if (!table || !table->data) return NULL;
    if (idx >= table->data_size) return NULL;
    return &table->data[(size_t)idx];
}

static inline bool mtpndd_node_is_valid(mtpndd_t idx) {
    return idx != MTPNDD_INVALID;
}

static inline mtpndd_edge_record_t mtpndd_edge_at(uint32_t edge_idx) {
    return g_mtpndd_nodetable.edge_pool.data[edge_idx];
}

#endif // MTPNDD_NODETABLE_H
