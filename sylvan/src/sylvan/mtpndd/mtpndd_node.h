// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODE_H
#define MTPNDD_NODE_H

#include <stdatomic.h>
#include <stdio.h>
#include <stdbool.h>
#include "mtpndd_common.h"

/********************************
 * MTPNDD node definition
 ********************************/
// directly use field_id instead of mtpndd_field_info_t * for better cache performance
// TODO: cache line friendly
// TODO: like JDD, use a t_list data structure for both node memory pool and node table
struct mtpndd_node_s {
    atomic_uint_fast64_t ref_count;
    uint32_t field_id;
    struct mtpndd_edge_s *edges;
};
/********************************
 * MTPNDD edge definition
 ********************************/
typedef struct edge_bucket_entry_s {
    struct edge_bucket_entry_s *next;
    mtpndd_node_t *child;
    _Atomic(mtpndd_bdd_t) label;
} edge_bucket_entry_t;

// TODO: try not to malloc buckets every time, use pooled array instead. Edge map memory pool should alloc every edge map by _edge_bucket_cnt when mtpndd_init
// mtpndd_t* child -> mtpndd_bdd_t label
struct mtpndd_edge_s {
    size_t edge_count;
    uint64_t cached_hash;  // Cached hash value for fast lookup (like Java HashMap)
    edge_bucket_entry_t **buckets;
    size_t bucket_count;
    size_t load_threshold;
    bool buckets_malloced;
};

// Compute hash value for edge map content (matches Java Map.hashCode() semantics)
static inline uint64_t mtpndd_edge_entry_hash(const mtpndd_node_t *child, mtpndd_bdd_t label) {
    uint64_t entry_hash = mtpndd_hash_u64((uintptr_t)child);
    entry_hash ^= mtpndd_hash_u64((uint64_t)label);
    return entry_hash;
}

static inline uint64_t mtpndd_edge_map_compute_hash(const mtpndd_edge_t *edges) {
    if (!edges || !edges->buckets) return 0;

    // Use XOR-based accumulation like Java's HashMap.hashCode()
    // This is order-independent, matching Java's Map.hashCode() behavior
    uint64_t hash = 0;
    size_t edge_bucket_cnt = edges->bucket_count ? edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count;
    for (size_t i = 0; i < edge_bucket_cnt; i++) {
        edge_bucket_entry_t *entry = edges->buckets[i];
        while (entry) {
            // Hash each (child, label) pair and XOR into result
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            uint64_t entry_hash = mtpndd_edge_entry_hash(entry->child, label);
            hash ^= entry_hash;
            entry = entry->next;
        }
    }
    return hash;
}

#define EDGE_MAP_BUCKET_COUNT(map) \
    (((map) && (map)->bucket_count) ? (map)->bucket_count : g_mtpndd_pal_config.edge_bucket_count)
#define EDGE_MAP_BUCKET_INDEX(map, key) \
    (EDGE_MAP_BUCKET_COUNT(map) ? (mtpndd_hash_node_identity((key)) & (EDGE_MAP_BUCKET_COUNT(map) - 1)) : 0)
#define EDGE_MAP_HASH_VAL(map, key) EDGE_MAP_BUCKET_INDEX(map, key)

#define EDGE_BUCKET_ENTRY_EQUAL(entry, key) ((entry->child) == (key))

#define FOR_EACH_ENTRY_IN_BUCKET(emap, bucket_idx, entry) \
    for ((entry) = (emap)->buckets[bucket_idx]; \
        (entry); \
        (entry) = (entry)->next)
#define FOR_EACH_ENTRY_IN_ALL_BUCKETS(emap, entry) \
    for (size_t _bkt = 0, _edge_bucket_cnt = (emap)->bucket_count ? (emap)->bucket_count : g_mtpndd_pal_config.edge_bucket_count; _bkt < _edge_bucket_cnt; _bkt++) \
        for ((entry) = (emap)->buckets[_bkt]; \
            (entry); \
            (entry) = (entry)->next)

edge_bucket_entry_t *find_edge_entry(mtpndd_edge_t *edge, mtpndd_node_t *key);
void mtpndd_edge_map_free(mtpndd_edge_t *edges);
mtpndd_error_t mtpndd_add_edge(mtpndd_edge_t *edges, mtpndd_t *descendant, mtpndd_bdd_t label_bdd);
void mtpndd_edge_map_init(mtpndd_edge_t *edges);

/********************************
 * MTPNDD terminal nodes
 ********************************/
extern mtpndd_t MTPNDD_TRUE;
extern mtpndd_t MTPNDD_FALSE;

bool mtpndd_is_true(mtpndd_t *ndd);
bool mtpndd_is_false(mtpndd_t *ndd);
bool mtpndd_is_terminal(mtpndd_t *ndd);

/********************************
 * MTPNDD operations
 ********************************/
mtpndd_t *mtpndd_and(mtpndd_t *a, mtpndd_t *b);
mtpndd_t *mtpndd_or(mtpndd_t *a, mtpndd_t *b);
mtpndd_t *mtpndd_not(mtpndd_t *a);
mtpndd_t *mtpndd_diff(mtpndd_t *a, mtpndd_t *b);
mtpndd_t *mtpndd_exist(mtpndd_t *a, uint32_t field);
double mtpndd_satcount(mtpndd_t *node);
double mtpndd_satcount_ndd(mtpndd_t *node);
double mtpndd_satcount_mtbdd(mtpndd_t *node);

/********************************
 * MTPNDD <-> MTBDD convertion
 ********************************/
mtpndd_error_t mtpndd_to_mtbdd(mtpndd_t *node, mtpndd_bdd_t *result);
mtpndd_error_t mtbdd_to_mtpndd(mtpndd_bdd_t bdd, mtpndd_t **result);

void mtpndd_fprint_dot(FILE *out, mtpndd_t *root);
void mtpndd_print_dot(mtpndd_t *root, const char *path);

#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
uint64_t mtpndd_temp_refs_grow_total(void);
uint64_t mtpndd_temp_refs_peak_capacity(void);
#endif

#endif // MTPNDD_NODE_H
