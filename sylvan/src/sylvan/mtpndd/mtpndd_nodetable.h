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

// Sylvan-style packed slot layout: 40-bit index + 24-bit hash (top bits).
// This is used for the open-addressing `hash[]` table to speed up mismatch detection.
#define MTPNDD_NODETABLE_SLOT_MASK_INDEX ((uint64_t)0x000000ffffffffffULL) // low 40 bits
#define MTPNDD_NODETABLE_SLOT_MASK_HASH  ((uint64_t)0xffffff0000000000ULL) // high 24 bits
#define MTPNDD_NODETABLE_SLOT_INDEX_BITS 40u
#define MTPNDD_NODETABLE_SLOT_HASH_BITS  24u

typedef struct mtpndd_nodetable_s {
    // Open addressing hash table.
    // Each slot stores a packed 64-bit value:
    //   [ 24-bit hash (top bits) | 40-bit node idx ]
    // where idx==0 means empty (we never store terminals 0/1).
    // The hash bits let us skip expensive edge comparisons when hashes differ (Sylvan-style).
    uint64_t *hash;
    size_t hash_capacity;
    size_t hash_mask;
    size_t hash_count;
    size_t hash_capacity_max;
    size_t hash_probe_threshold; // number of cache lines to probe before giving up (Sylvan-style)

    // node records indexed by idx
    mtpndd_node_record_t *data;
    size_t data_capacity;
    size_t data_size; // next idx (>=2)
    size_t data_capacity_max;

    // free-list of reusable node indices (0 means empty)
    mtpndd_t free_list_head;

    // backing storage for all edges (append-only, optional compact on GC)
    mtpndd_edge_array_pool_t edge_pool;
} mtpndd_nodetable_t;

extern mtpndd_nodetable_t g_mtpndd_nodetable;

void mtpndd_nodetable_init(mtpndd_nodetable_t *table,
                           size_t node_capacity_min,
                           size_t node_capacity_max,
                           size_t hash_capacity_min,
                           size_t hash_capacity_max,
                           size_t edge_capacity_hint);
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
