// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_NODETABLE_H
#define MTPNDD_NODETABLE_H

#include <pthread.h>
#include "mtpndd_common.h"
#include "mtpndd_node.h"
#include <bits/pthreadtypes.h>

#ifdef LARGE_NODETABLE
#define NODETABLE_BUCKET_CNT 65537
#else
#define NODETABLE_BUCKET_CNT 1024
#endif

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

static inline size_t nodetable_hash_edges(const mtpndd_edge_t *key, const mtpndd_nodetable_t *nodetable);
#define NODETABLE_HASH_VAL(key, nodetable) nodetable_hash_edges((key), (nodetable))

// TODO: compare all children with their labels
#define NODETABLE_BUCKET_ENTRY_EQUAL(entry, keyEdges) ((entry->edges) == (keyEdges))

#define FOR_EACH_ENTRY_IN_NODETABLE_BUCKET(emap, bucket_idx, entry) \
    for (mtpndd_nodetable_bucket_entry_t *entry = emap->buckets[bucket_idx]; \
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
mtpndd_error_t mtpndd_mk(uint32_t field, mtpndd_edge_t *edges, mtpndd_node_t **result);

#endif // MTPNDD_NODETABLE_H
