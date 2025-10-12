// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODE_H
#define MTPNDD_NODE_H

#include <stdatomic.h>
#include "mtpndd_common.h"

/********************************
 * MTPNDD node definition
 ********************************/
#define mtpndd_t mtpndd_node_t

typedef uint64_t mtpndd_bdd_t;

typedef struct mtpndd_node_s {
	mtpndd_field_info_t *field;
	mtpndd_edge_t *edges;
	atomic_uint_fast64_t ref_count;
} mtpndd_node_t;

/********************************
 * MTPNDD edge definition
 ********************************/
uint8_t EDGE_BUCKET_CNT = 8;

typedef struct edge_bucket_entry_s {
    struct edge_bucket_entry_s *next;
    struct edge_bucket_entry_s *prev;
    mtpndd_node_t *child;
    mtpndd_bdd_t label;
} edge_bucket_entry_t;

// mtpndd_t* child -> mtpndd_bdd_t label
typedef struct mtpndd_edge_s {
    size_t edge_count;
    edge_bucket_entry_t **buckets;
} mtpndd_edge_t;

#define EDGE_MAP_INIT(emap) do { \
        (emap)->edge_count = 0; \
        (emap)->buckets = (edge_bucket_entry_t **)malloc(sizeof(edge_bucket_entry_t *) * EDGE_BUCKET_CNT); \
        if (!(emap)->buckets) { \
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__); \
            return MTPNDD_ERROR_OUT_OF_MEMORY; \
        } \
        for (size_t i = 0; i < EDGE_BUCKET_CNT; i++) { \
            (emap)->buckets[i] = NULL; \
        } \
    } while(0)

#define EDGE_MAP_HASH_VAL(key) EDGE_MAP_HASH_PTR(key)
#define EDGE_MAP_HASH_PTR(key) ((size_t)(uintptr_t)(key) % (EDGE_BUCKET_CNT))

#define EDGE_BUCKET_ENTRY_EQUAL(entry, key) ((entry->child) == (key))

#define FOR_EACH_ENTRY_IN_BUCKET(emap, bucket_idx, entry) \
    for (edge_bucket_entry_t *entry = emap->buckets[bucket_idx]; \
        entry; \
        entry = entry->next)
#define FOR_EACH_ENTRY_IN_ALL_BUCKETS(emap, entry) \
    for (size_t _bkt = 0; _bkt < EDGE_BUCKET_CNT; _bkt++) \
        FOR_EACH_ENTRY_IN_BUCKET(emap, _bkt, entry)

edge_bucket_entry_t *find_edge_entry(mtpndd_edge_t *edge, mtpndd_node_t *key);

/********************************
 * MTPNDD terminal nodes
 ********************************/
static mtpndd_t MTPNDD_TRUE = {&MTPNDD_TERMINAL_FIELD, NULL, UINT64_MAX};
static mtpndd_t MTPNDD_FALSE = {&MTPNDD_TERMINAL_FIELD, NULL, UINT64_MAX};

bool mtpndd_is_true(mtpndd_t *ndd);
bool mtpndd_is_false(mtpndd_t *ndd);
bool mtpndd_is_terminal(mtpndd_t *ndd);

/********************************
 * MTPNDD new node
 ********************************/
mtpndd_error_t mtpndd_add_edge(mtpndd_t *ndd, mtpndd_t *descendant, mtpndd_bdd_t label_bdd);

/********************************
 * MTPNDD operations
 ********************************/
mtpndd_error_t mtpndd_and(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result);
mtpndd_error_t mtpndd_or(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result);
mtpndd_error_t mtpndd_not(mtpndd_t *a, mtpndd_t **result);
mtpndd_error_t mtpndd_diff(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result);
mtpndd_error_t mtpndd_exist(mtpndd_t *a, uint32_t field, mtpndd_t **result);

#endif // MTPNDD_NODE_H
