// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODETABLE_H
#define MTPNDD_NODETABLE_H

#include <pthread.h>
#include <stdint.h>
#include "mtpndd_common.h"
#include "mtpndd_node.h"

/********************************
 * MTPNDD nodetable
 ********************************/
typedef struct mtpndd_nodetable_bucket_entry_s {
    struct mtpndd_nodetable_bucket_entry_s *next;
    struct mtpndd_nodetable_bucket_entry_s *prev;
    mtpndd_edge_t *edges;
    mtpndd_node_t *node;
} mtpndd_nodetable_bucket_entry_t;

// mtpndd_edge_t *edges -> mtpndd_node_t* node
typedef struct mtpndd_nodetable_s {
    size_t nodetable_bucket_count;
    mtpndd_nodetable_bucket_entry_t **buckets;
    pthread_rwlock_t *bucket_locks;
} mtpndd_nodetable_t;

static inline size_t nodetable_hash_edges_with_bucket_count(const mtpndd_edge_t *key, size_t bucket_count);
static inline size_t nodetable_hash_edges(const mtpndd_edge_t *key, const mtpndd_nodetable_t *nodetable);
#define NODETABLE_HASH_VAL(key, nodetable) nodetable_hash_edges((key), (nodetable))

// Compare edge map CONTENT, not pointer
// Returns true if both edge maps have identical (child, label) pairs
static inline bool nodetable_edges_equal(const mtpndd_edge_t *a, const mtpndd_edge_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->edge_count != b->edge_count) return false;
    if (a->edge_count == 0) return true;

    // For each edge in 'a', find matching edge in 'b'
    size_t edge_bucket_cnt = g_mtpndd_pal_config.edge_bucket_count;
    for (size_t i = 0; i < edge_bucket_cnt; i++) {
        edge_bucket_entry_t *entry_a = a->buckets ? a->buckets[i] : NULL;
        while (entry_a) {
            // Find this (child, label) pair in b
            mtpndd_node_t *child_a = entry_a->child;
            mtpndd_bdd_t label_a = atomic_load_explicit(&entry_a->label, memory_order_relaxed);

            // Lookup in b's edge map
            bool found = false;
            if (b->buckets) {
                size_t b_bucket = edge_map_hash_child(b, child_a);
                edge_bucket_entry_t *entry_b = b->buckets[b_bucket];
                while (entry_b) {
                    if (entry_b->child == child_a) {
                        mtpndd_bdd_t label_b = atomic_load_explicit(&entry_b->label, memory_order_relaxed);
                        if (label_a == label_b) {
                            found = true;
                        }
                        break;
                    }
                    entry_b = entry_b->next;
                }
            }
            if (!found) return false;

            entry_a = entry_a->next;
        }
    }
    return true;
}

#define NODETABLE_BUCKET_ENTRY_EQUAL(entry, keyEdges) nodetable_edges_equal((entry)->edges, (keyEdges))

#define FOR_EACH_ENTRY_IN_NODETABLE_BUCKET(emap, bucket_idx, entry) \
    for (entry = emap->buckets[bucket_idx]; \
        entry; \
        entry = entry->next)
#define FOR_EACH_ENTRY_IN_ALL_NODETABLE_BUCKETS(emap, entry) \
    for (size_t _bkt = 0; _bkt < emap->nodetable_bucket_count; _bkt++) \
        FOR_EACH_ENTRY_IN_NODETABLE_BUCKET(emap, _bkt, entry)

mtpndd_node_t *find_node_in_nodetable(mtpndd_nodetable_t *nodetable, mtpndd_edge_t *edges);

mtpndd_nodetable_t *mtpndd_nodetable_declare_field();

/********************************
 * MTPNDD node
 ********************************/
mtpndd_error_t mtpndd_ref(mtpndd_t *node);
mtpndd_error_t mtpndd_deref(mtpndd_t *node);
mtpndd_error_t mtpndd_protect(mtpndd_t *node);
mtpndd_error_t mtpndd_unprotect(mtpndd_t *node);
// create or reuse node
void mtpndd_mk(uint32_t field, mtpndd_edge_t *edges, mtpndd_node_t **result);

/********************************
 * Implementation of nodetable hash
 ********************************/
static inline size_t nodetable_hash_edges(const mtpndd_edge_t *key, const mtpndd_nodetable_t *nodetable)
{
    if (!nodetable) return 0;
    return nodetable_hash_edges_with_bucket_count(key, nodetable->nodetable_bucket_count);
}

static inline size_t nodetable_hash_edges_with_bucket_count(const mtpndd_edge_t *key, size_t bucket_count) {
    if (!key || bucket_count == 0) {
        return 0;
    }

    // Hash edge CONTENT, not pointer address
    // This matches Java's HashMap behavior: hash based on (child, label) pairs
    uint64_t hash = 1469598103934665603ULL; /* FNV offset basis */

    if (key->buckets) {
        size_t edge_bucket_cnt = g_mtpndd_pal_config.edge_bucket_count;
        for (size_t i = 0; i < edge_bucket_cnt; i++) {
            edge_bucket_entry_t *entry = key->buckets[i];
            while (entry) {
                // Hash child pointer (node identity)
                uintptr_t child_addr = (uintptr_t)entry->child;
                hash ^= child_addr;
                hash *= 1099511628211ULL;
                // Hash BDD label value
                mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
                hash ^= (uint64_t)label;
                hash *= 1099511628211ULL;
                entry = entry->next;
            }
        }
    }

    return (size_t)(hash % bucket_count);
}

#endif // MTPNDD_NODETABLE_H
