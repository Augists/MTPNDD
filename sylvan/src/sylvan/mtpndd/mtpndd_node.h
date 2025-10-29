// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODE_H
#define MTPNDD_NODE_H

#include <stdatomic.h>
#include <stdio.h>
#include <lace.h>
#include "mtpndd_common.h"

/********************************
 * MTPNDD node definition
 ********************************/
struct mtpndd_node_s {
    mtpndd_field_info_t *field;
    struct mtpndd_edge_s *edges;
    atomic_uint_fast64_t ref_count;
};

/********************************
 * MTPNDD edge definition
 ********************************/
typedef struct edge_bucket_entry_s {
    struct edge_bucket_entry_s *next;
    struct edge_bucket_entry_s *prev;
    mtpndd_node_t *child;
    _Atomic(mtpndd_bdd_t) label;
} edge_bucket_entry_t;

// mtpndd_t* child -> mtpndd_bdd_t label
struct mtpndd_edge_s {
    size_t edge_count;
    size_t bucket_count;
    edge_bucket_entry_t **buckets;
    atomic_flag *bucket_locks;
};

#define EDGE_MAP_INIT(emap) do { \
        size_t _edge_bucket_cnt = mtpndd_config_edge_bucket_count(); \
        if (_edge_bucket_cnt == 0) { \
            _edge_bucket_cnt = MTPNDD_DEFAULT_EDGE_BUCKET_COUNT; \
        } \
        (emap)->edge_count = 0; \
        (emap)->bucket_count = _edge_bucket_cnt; \
        (emap)->buckets = (edge_bucket_entry_t **)malloc(sizeof(edge_bucket_entry_t *) * _edge_bucket_cnt); \
        if (!(emap)->buckets) { \
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__); \
            return MTPNDD_ERROR_OUT_OF_MEMORY; \
        } \
        (emap)->bucket_locks = (atomic_flag *)malloc(sizeof(atomic_flag) * _edge_bucket_cnt); \
        if (!(emap)->bucket_locks) { \
            free((emap)->buckets); \
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__); \
            return MTPNDD_ERROR_OUT_OF_MEMORY; \
        } \
        for (size_t i = 0; i < _edge_bucket_cnt; i++) { \
            (emap)->buckets[i] = NULL; \
            atomic_flag_clear_explicit(&(emap)->bucket_locks[i], memory_order_relaxed); \
        } \
    } while(0)

static inline size_t edge_map_hash_child(const mtpndd_edge_t *emap, const mtpndd_node_t *child) {
    size_t hash = mtpndd_hash_node_identity(child);
    size_t bucket_cnt = emap ? emap->bucket_count : mtpndd_config_edge_bucket_count();
    return bucket_cnt ? (hash % bucket_cnt) : 0;
}

#define EDGE_MAP_HASH_VAL(map, key) edge_map_hash_child((map), (key))

#define EDGE_BUCKET_ENTRY_EQUAL(entry, key) ((entry->child) == (key))

#define FOR_EACH_ENTRY_IN_BUCKET(emap, bucket_idx, entry) \
    for ((entry) = ((emap) && (emap)->buckets && (emap)->buckets[bucket_idx]) ? (emap)->buckets[bucket_idx]->next : NULL; \
        (entry) && (entry) != (emap)->buckets[bucket_idx]; \
        (entry) = (entry)->next)
#define FOR_EACH_ENTRY_IN_ALL_BUCKETS(emap, entry) \
    for (size_t _bkt = 0, _edge_bucket_cnt = (emap) ? (emap)->bucket_count : 0; _bkt < _edge_bucket_cnt; _bkt++) \
        for ((entry) = ((emap) && (emap)->buckets && (emap)->buckets[_bkt]) ? (emap)->buckets[_bkt]->next : NULL; \
            (entry) && (entry) != (emap)->buckets[_bkt]; \
            (entry) = (entry)->next)

edge_bucket_entry_t *find_edge_entry(mtpndd_edge_t *edge, mtpndd_node_t *key);
void mtpndd_edge_map_free(mtpndd_edge_t *edges);
mtpndd_error_t mtpndd_add_edge(mtpndd_edge_t *edges, mtpndd_t *descendant, mtpndd_bdd_t label_bdd);

/********************************
 * MTPNDD terminal nodes
 ********************************/
extern mtpndd_t MTPNDD_TRUE;
extern mtpndd_t MTPNDD_FALSE;

bool mtpndd_is_true(mtpndd_t *ndd);
bool mtpndd_is_false(mtpndd_t *ndd);
bool mtpndd_is_terminal(mtpndd_t *ndd);

/********************************
 * MTPNDD operations (sub task)
 ********************************/
VOID_TASK_DECL_3(mtpndd_and_rec_same_field_task, edge_bucket_entry_t *, edge_bucket_entry_t *, mtpndd_edge_t *);
VOID_TASK_DECL_3(mtpndd_and_rec_diff_field_task, edge_bucket_entry_t *, mtpndd_t *, mtpndd_edge_t *);
TASK_DECL_3(mtpndd_error_t, mtpndd_and_rec, mtpndd_t *, mtpndd_t *, mtpndd_t **);
VOID_TASK_DECL_5(mtpndd_or_rec_same_field_task, edge_bucket_entry_t *, edge_bucket_entry_t *, mtpndd_edge_t *, mtpndd_edge_t *, mtpndd_edge_t *);
VOID_TASK_DECL_4(mtpndd_or_rec_diff_field_task, edge_bucket_entry_t *, mtpndd_t *, mtpndd_edge_t *, _Atomic(mtpndd_bdd_t) *);
TASK_DECL_3(mtpndd_error_t, mtpndd_or_rec, mtpndd_t *, mtpndd_t *, mtpndd_t **);
VOID_TASK_DECL_3(mtpndd_not_rec_task, edge_bucket_entry_t *, mtpndd_edge_t *, _Atomic(mtpndd_bdd_t) *);
TASK_DECL_2(mtpndd_error_t, mtpndd_not_rec, mtpndd_t *, mtpndd_t **);
VOID_TASK_DECL_3(mtpndd_exist_rec_task, edge_bucket_entry_t *, mtpndd_edge_t *, uint32_t);
TASK_DECL_3(mtpndd_error_t, mtpndd_exist_rec, mtpndd_t *, uint32_t, mtpndd_t **);

/********************************
 * MTPNDD operations
 ********************************/
mtpndd_t *mtpndd_and(mtpndd_t *a, mtpndd_t *b);
mtpndd_t *mtpndd_or(mtpndd_t *a, mtpndd_t *b);
mtpndd_t *mtpndd_not(mtpndd_t *a);
mtpndd_t *mtpndd_diff(mtpndd_t *a, mtpndd_t *b);
mtpndd_t *mtpndd_exist(mtpndd_t *a, uint32_t field);
double mtpndd_satcount(mtpndd_t *node);

/********************************
 * MTPNDD <-> MTBDD convertion
 ********************************/
mtpndd_error_t mtpndd_to_mtbdd(mtpndd_t *node, mtpndd_bdd_t *result);
mtpndd_error_t mtbdd_to_mtpndd(mtpndd_bdd_t bdd, mtpndd_t **result);

void mtpndd_fprint_dot(FILE *out, mtpndd_t *root);
void mtpndd_print_dot(mtpndd_t *root);

#endif // MTPNDD_NODE_H
