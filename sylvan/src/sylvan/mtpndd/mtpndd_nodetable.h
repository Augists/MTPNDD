// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODETABLE_H
#define MTPNDD_NODETABLE_H

#include <stdint.h>
#include "mtpndd_common.h"
#include "mtpndd_node.h"
#ifdef ENABLE_RECORDING
#include <time.h>
#endif

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
    size_t entry_count; // number of nodes stored in this table
    size_t load_threshold; // trigger rehash when entry_count >= load_threshold
} mtpndd_nodetable_t;

#define NODETABLE_HASH_VAL(key, nodetable) \
    (((nodetable) && (nodetable)->nodetable_bucket_count) \
        ? (size_t)(((key) ? (key)->cached_hash : 0) & ((nodetable)->nodetable_bucket_count - 1)) \
        : 0)

// Compare edge map CONTENT, not pointer
// Returns true if both edge maps have identical (child, label) pairs
// Optimized: first compare cached hash values for fast rejection
static inline bool nodetable_edges_equal(const mtpndd_edge_t *a, const mtpndd_edge_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;

#ifdef ENABLE_RECORDING
    struct timespec compare_start = {0};
    struct timespec compare_end = {0};
    size_t compare_entries = 0;
    size_t compare_steps_total = 0;
    size_t compare_max_steps = 0;
    clock_gettime(CLOCK_MONOTONIC, &compare_start);
#endif
#ifdef ENABLE_RECORDING
    bool result = true;
#define NODETABLE_RETURN(val) do { result = (val); goto record_compare; } while (0)
#else
#define NODETABLE_RETURN(val) return (val)
#endif

    // Fast path: compare cached hash values first (like Java HashMap)
    // Different hash means definitely not equal
    if (a->cached_hash != b->cached_hash) {
        NODETABLE_RETURN(false);
    }

    // Hash match, now check edge count
    if (a->edge_count != b->edge_count) {
        NODETABLE_RETURN(false);
    }
    if (a->edge_count == 0) {
        NODETABLE_RETURN(true);
    }

    // Full comparison only if hash and count match
    // For each edge in 'a', find matching edge in 'b'
    size_t edge_bucket_cnt = a->bucket_count ? a->bucket_count : g_mtpndd_pal_config.edge_bucket_count;
    for (size_t i = 0; i < edge_bucket_cnt; i++) {
        edge_bucket_entry_t *entry_a = a->buckets ? a->buckets[i] : NULL;
        while (entry_a) {
#ifdef ENABLE_RECORDING
            size_t steps = 0;
            compare_entries++;
#endif
            // Find this (child, label) pair in b
            mtpndd_node_t *child_a = entry_a->child;
            mtpndd_bdd_t label_a = atomic_load_explicit(&entry_a->label, memory_order_relaxed);

            // Lookup in b's edge map
            bool found = false;
            if (b->buckets) {
                size_t b_bucket = EDGE_MAP_BUCKET_INDEX(b, child_a);
                edge_bucket_entry_t *entry_b = b->buckets[b_bucket];
                while (entry_b) {
#ifdef ENABLE_RECORDING
                    steps++;
#endif
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
#ifdef ENABLE_RECORDING
            compare_steps_total += steps;
            if (steps > compare_max_steps) {
                compare_max_steps = steps;
            }
#endif
            if (!found) {
                NODETABLE_RETURN(false);
            }

            entry_a = entry_a->next;
        }
    }
#ifdef ENABLE_RECORDING
record_compare:
    clock_gettime(CLOCK_MONOTONIC, &compare_end);
    MTPNDD_STAT_ADD(nodetable_edge_compare_ns, mtpndd_timespec_diff_ns(&compare_start, &compare_end));
    MTPNDD_STAT_ADD(nodetable_edge_compare_entries, compare_entries);
    MTPNDD_STAT_ADD(nodetable_edge_compare_steps_total, compare_steps_total);
    MTPNDD_STAT_MAX(nodetable_edge_compare_max_steps, compare_max_steps);
    return result;
#endif
#undef NODETABLE_RETURN
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

#endif // MTPNDD_NODETABLE_H
