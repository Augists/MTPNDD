// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_common.h"
#include "mtpndd_node.h"
#include "mtpndd_nodetable.h"
#include "mtpndd_operation_cache.h"
#include "mtpndd_memory_pool.h"
#include "sylvan.h"
#include "sylvan_mtbdd.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#ifdef ENABLE_RECORDING
#include <time.h>
#endif

mtpndd_t MTPNDD_TRUE = {0};
mtpndd_t MTPNDD_FALSE = {0};

static inline mtpndd_bdd_t mtpndd_edge_label_load(edge_bucket_entry_t *entry);
static inline void mtpndd_residual_apply_mask(edge_bucket_entry_t *entry, mtpndd_bdd_t mask);
static mtpndd_error_t mtpndd_edge_map_deep_clone(const mtpndd_edge_t *source, mtpndd_edge_t *dest);
static void mtpndd_edge_map_reset(mtpndd_edge_t *edges);
void mtpndd_edge_map_free(mtpndd_edge_t *edges);
static void mtpndd_edge_map_maybe_rehash(mtpndd_edge_t *edges);
static size_t mtpndd_round_up_pow2(size_t v);
static bool mtpndd_edge_map_rehash(mtpndd_edge_t *edges, size_t new_bucket_count);

typedef struct {
    mtpndd_t **items;
    size_t count;
    size_t capacity;
} mtpndd_temp_ref_list_t;

static void mtpndd_temp_refs_init(mtpndd_temp_ref_list_t *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool mtpndd_temp_refs_push(mtpndd_temp_ref_list_t *list, mtpndd_t *node) {
    if (!list) return false;
    if (!node) return true;
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 32;
        mtpndd_t **next = (mtpndd_t **)realloc(list->items, new_cap * sizeof(mtpndd_t *));
        if (!next) {
            return false;
        }
        list->items = next;
        list->capacity = new_cap;
    }
    mtpndd_ref(node);
    list->items[list->count++] = node;
    return true;
}

static void mtpndd_temp_refs_release(mtpndd_temp_ref_list_t *list) {
    if (!list || !list->items) return;
    for (size_t i = 0; i < list->count; ++i) {
        mtpndd_deref(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void mtpndd_edge_map_init(mtpndd_edge_t *edges) {
    size_t bucket_cnt = edges->bucket_count ? edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count;
    if (bucket_cnt < MTPNDD_DEFAULT_EDGE_BUCKET_COUNT) bucket_cnt = MTPNDD_DEFAULT_EDGE_BUCKET_COUNT;
    bucket_cnt = mtpndd_round_up_pow2(bucket_cnt);
    edges->bucket_count = bucket_cnt;
    edges->load_threshold = bucket_cnt - (bucket_cnt >> 3); // ~0.875 load factor (default 0.8+)
    edges->edge_count = 0;
    edges->cached_hash = 0;
    edges->buckets_malloced = false;
    // Use memset for faster initialization than loop
    memset(edges->buckets, 0, bucket_cnt * sizeof(edge_bucket_entry_t *));
}

size_t mtpndd_hash_node_identity(const mtpndd_node_t *node) {
    if (!node) return 0;

    uint64_t hash = 1469598103934665603ULL; /* FNV offset basis */
    uint64_t field_id = (uint64_t)node->field_id;
    uintptr_t edges_addr = (uintptr_t)node->edges;

    hash ^= field_id;
    hash *= 1099511628211ULL;             /* FNV prime */
    hash ^= edges_addr;
    hash *= 1099511628211ULL;

    return (size_t)hash;
}

static mtpndd_error_t mtpndd_edge_map_deep_clone(const mtpndd_edge_t *source, mtpndd_edge_t *dest) {
    dest->edge_count = source->edge_count;
    dest->cached_hash = source->cached_hash;

    size_t bucket_cnt = source->bucket_count ? source->bucket_count : g_mtpndd_pal_config.edge_bucket_count;
    if (bucket_cnt < MTPNDD_DEFAULT_EDGE_BUCKET_COUNT) bucket_cnt = MTPNDD_DEFAULT_EDGE_BUCKET_COUNT;
    bucket_cnt = mtpndd_round_up_pow2(bucket_cnt);
    if (bucket_cnt != dest->bucket_count) {
        if (dest->buckets_malloced && dest->buckets) {
            free(dest->buckets);
        }
        dest->buckets = (edge_bucket_entry_t **)calloc(bucket_cnt, sizeof(edge_bucket_entry_t *));
        if (!dest->buckets) {
            dest->bucket_count = 0;
            dest->load_threshold = 0;
            dest->buckets_malloced = false;
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        dest->buckets_malloced = true;
        dest->bucket_count = bucket_cnt;
        dest->load_threshold = bucket_cnt - (bucket_cnt >> 2);
    } else {
        memset(dest->buckets, 0, bucket_cnt * sizeof(edge_bucket_entry_t *));
    }

    if (!source->buckets) {
        return MTPNDD_SUCCESS;
    }

    for (size_t i = 0; i < bucket_cnt; ++i) {
        edge_bucket_entry_t *src_entry = source->buckets[i];
        if (!src_entry) continue;

        edge_bucket_entry_t *dst_head = NULL;
        edge_bucket_entry_t *dst_tail = NULL;

        while (src_entry) {
            edge_bucket_entry_t *new_entry = mtpndd_memory_acquire_edge_entry();
            if (!new_entry) {
                mtpndd_edge_map_reset(dest);
                return MTPNDD_ERROR_OUT_OF_MEMORY;
            }
            new_entry->child = src_entry->child;
            if (new_entry->child && !mtpndd_is_terminal(new_entry->child)) {
                mtpndd_ref(new_entry->child);
            }
            mtpndd_bdd_t label = src_entry->label;
            new_entry->label = sylvan_ref(label);

            new_entry->next = NULL;
            if (dst_tail) {
                dst_tail->next = new_entry;
            } else {
                dst_head = new_entry;
            }
            dst_tail = new_entry;

            src_entry = src_entry->next;
        }

        dest->buckets[i] = dst_head;
    }

    return MTPNDD_SUCCESS;
}

static void mtpndd_edge_map_reset(mtpndd_edge_t *edges) {
    if (!edges) {
        return;
    }
    if (edges->buckets) {
        size_t bucket_cnt = edges->bucket_count ? edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count;
        // Release all entries first
        for (size_t i = 0; i < bucket_cnt; ++i) {
            edge_bucket_entry_t *entry = edges->buckets[i];
            while (entry) {
                edge_bucket_entry_t *next = entry->next;
                mtpndd_bdd_t label = entry->label;
                sylvan_deref(label);
                mtpndd_memory_release_edge_entry(entry);
                entry = next;
            }
        }
        // Use memset to clear all buckets at once
        memset(edges->buckets, 0, bucket_cnt * sizeof(edge_bucket_entry_t *));
    }
    edges->edge_count = 0;
    edges->cached_hash = 0;
}

void mtpndd_edge_map_free(mtpndd_edge_t *edges) {
    if (!edges) {
        return;
    }
    mtpndd_edge_map_reset(edges);
    mtpndd_memory_release_edge_map(edges);
}

static bool mtpndd_edge_map_rehash(mtpndd_edge_t *edges, size_t new_bucket_count) {
    if (!edges) return false;
    if (new_bucket_count < MTPNDD_DEFAULT_EDGE_BUCKET_COUNT) new_bucket_count = MTPNDD_DEFAULT_EDGE_BUCKET_COUNT;
    new_bucket_count = mtpndd_round_up_pow2(new_bucket_count);

    edge_bucket_entry_t **new_buckets = (edge_bucket_entry_t **)calloc(new_bucket_count, sizeof(edge_bucket_entry_t *));
    if (!new_buckets) {
        return false;
    }

    size_t old_bucket_cnt = edges->bucket_count ? edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count;
    for (size_t i = 0; i < old_bucket_cnt; ++i) {
        edge_bucket_entry_t *entry = edges->buckets[i];
        while (entry) {
            edge_bucket_entry_t *next = entry->next;
            size_t hash = mtpndd_hash_node_identity(entry->child) % new_bucket_count;
            entry->next = new_buckets[hash];
            new_buckets[hash] = entry;
            entry = next;
        }
    }

    if (edges->buckets_malloced && edges->buckets) {
        free(edges->buckets);
    }
    edges->buckets = new_buckets;
    edges->buckets_malloced = true;
    edges->bucket_count = new_bucket_count;
    edges->load_threshold = new_bucket_count - (new_bucket_count >> 3);
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(edge_map_rehash_total, 1);
    MTPNDD_STAT_MAX(edge_map_max_buckets, new_bucket_count);
#endif
    return true;
}

static void mtpndd_edge_map_maybe_rehash(mtpndd_edge_t *edges) {
    if (!edges) return;
    if (edges->edge_count >= edges->load_threshold) {
        size_t target = edges->bucket_count ? edges->bucket_count * 2 : (g_mtpndd_pal_config.edge_bucket_count ? g_mtpndd_pal_config.edge_bucket_count * 2 : 16);
        mtpndd_edge_map_rehash(edges, target);
    }
}

static size_t mtpndd_round_up_pow2(size_t v) {
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    if (sizeof(size_t) == 8) {
        v |= v >> 32;
    }
    v++;
    return v;
}

/**
 * Find edge entry in hash table
 * Returns NULL if not found
 */
edge_bucket_entry_t *find_edge_entry(mtpndd_edge_t *edge, mtpndd_node_t *key) {
    if (edge == NULL || key == NULL || edge->buckets == NULL) {
        return NULL;
    }
    
    size_t hash = EDGE_MAP_BUCKET_INDEX(edge, key);
    
    edge_bucket_entry_t *entry = NULL;
    FOR_EACH_ENTRY_IN_BUCKET(edge, hash, entry) {
        if (EDGE_BUCKET_ENTRY_EQUAL(entry, key)) {
            return entry;
        }
    }
    
    return NULL;
}

mtpndd_error_t mtpndd_add_edge(mtpndd_edge_t *edges, mtpndd_t *descendant, mtpndd_bdd_t label_bdd) {
    if (mtpndd_is_false(descendant)) {
        sylvan_deref(label_bdd);
        return MTPNDD_SUCCESS;
    }

    mtpndd_bdd_t old_label = sylvan_false;
    size_t hash = EDGE_MAP_BUCKET_INDEX(edges, descendant);

    mtpndd_error_t status = MTPNDD_SUCCESS;
    edge_bucket_entry_t *entry = NULL;
    edge_bucket_entry_t *bucket_head = edges->buckets[hash];
#ifdef ENABLE_RECORDING
    bool created_entry = false;
    bool collision_on_insert = false;
#endif

    edge_bucket_entry_t *prev = NULL;
    edge_bucket_entry_t *cursor = bucket_head;
    while (cursor) {
        if (EDGE_BUCKET_ENTRY_EQUAL(cursor, descendant)) {
            old_label = cursor->label;
            if (prev) {
                prev->next = cursor->next;
            } else {
                bucket_head = cursor->next;
            }
            entry = cursor;
            edges->edge_count--;
            break;
        }
        prev = cursor;
        cursor = cursor->next;
    }

    if (!entry) {
        entry = mtpndd_memory_acquire_edge_entry();
        if (!entry) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            status = MTPNDD_ERROR_OUT_OF_MEMORY;
            goto unlock_and_return;
        }
        entry->child = descendant;
#ifdef ENABLE_RECORDING
        created_entry = true;
        if (bucket_head != NULL) {
            collision_on_insert = true;
        }
#endif
    }

    // Update label
    mtpndd_bdd_t new_label = sylvan_ref(sylvan_or(old_label, label_bdd));
    entry->label = new_label;
    sylvan_deref(old_label);
    sylvan_deref(label_bdd);

    // Insert into bucket at the front
    entry->next = bucket_head;
    bucket_head = entry;

    edges->buckets[hash] = bucket_head;
    edges->edge_count++;
    mtpndd_edge_map_maybe_rehash(edges);
#ifdef ENABLE_RECORDING
    if (created_entry) {
        MTPNDD_STAT_ADD(edge_insert_total, 1);
        MTPNDD_STAT_ADD(edge_entry_total, 1);
        if (collision_on_insert) {
            MTPNDD_STAT_ADD(edge_collision_total, 1);
        }
        MTPNDD_STAT_MAX(max_edges_per_node, edges->edge_count);
    }
#endif
unlock_and_return:
    if (status != MTPNDD_SUCCESS) {
        return status;
    }
    return MTPNDD_SUCCESS;
}

bool mtpndd_is_true(mtpndd_t *ndd) {
    return ndd == &MTPNDD_TRUE;
}

bool mtpndd_is_false(mtpndd_t *ndd) {
    return ndd == &MTPNDD_FALSE;
}

bool mtpndd_is_terminal(mtpndd_t *ndd) {
    return ndd->field_id == 0;
}

static mtpndd_error_t mtpndd_and_rec(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result);
static mtpndd_error_t mtpndd_or_rec(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result);
static mtpndd_error_t mtpndd_not_rec(mtpndd_t *a, mtpndd_t **result);
static mtpndd_error_t mtpndd_exist_rec(mtpndd_t *a, uint32_t field, mtpndd_t **result);

#ifdef ENABLE_RECORDING
typedef enum {
    MTPNDD_AND_PROF_OTHER = 0,
    MTPNDD_AND_PROF_FASTPATH,
    MTPNDD_AND_PROF_CACHE_HIT,
    MTPNDD_AND_PROF_BUILD_EDGES,
    MTPNDD_AND_PROF_SAME_OUTER_LOOP,
    MTPNDD_AND_PROF_SAME_INNER_LOOP,
    MTPNDD_AND_PROF_SAME_LABEL_LOAD,
    MTPNDD_AND_PROF_SAME_BDD_OP,
    MTPNDD_AND_PROF_SAME_ADD_EDGE,
    MTPNDD_AND_PROF_DIFF_FIELD,
    MTPNDD_AND_PROF_MK,
    MTPNDD_AND_PROF_COUNT
} mtpndd_and_prof_bucket_t;

typedef struct {
    bool recording;
    mtpndd_and_prof_bucket_t active;
    struct timespec last_switch;
    uint64_t bucket_ns[MTPNDD_AND_PROF_COUNT];
} mtpndd_and_prof_ctx_t;

static _Thread_local uint32_t mtpndd_and_depth = 0;
static _Thread_local mtpndd_and_prof_ctx_t mtpndd_and_prof_ctx = {0};

static inline void mtpndd_and_prof_start(void) {
    if (mtpndd_and_depth == 0) {
        mtpndd_and_prof_ctx.recording = true;
        mtpndd_and_prof_ctx.active = MTPNDD_AND_PROF_OTHER;
        for (size_t i = 0; i < MTPNDD_AND_PROF_COUNT; i++) {
            mtpndd_and_prof_ctx.bucket_ns[i] = 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &mtpndd_and_prof_ctx.last_switch);
    }
    mtpndd_and_depth++;
}

static inline void mtpndd_and_prof_switch(mtpndd_and_prof_bucket_t bucket) {
    if (!mtpndd_and_prof_ctx.recording) return;
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t delta = mtpndd_timespec_diff_ns(&mtpndd_and_prof_ctx.last_switch, &now);
    mtpndd_and_prof_ctx.bucket_ns[mtpndd_and_prof_ctx.active] += delta;
    mtpndd_and_prof_ctx.active = bucket;
    mtpndd_and_prof_ctx.last_switch = now;
}

static inline void mtpndd_and_prof_finish(void) {
    if (mtpndd_and_prof_ctx.recording && mtpndd_and_depth == 1) {
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_OTHER);
        mtpndd_and_prof_ctx.recording = false;
        uint64_t total_ns = 0;
        for (size_t i = 0; i < MTPNDD_AND_PROF_COUNT; i++) {
            total_ns += mtpndd_and_prof_ctx.bucket_ns[i];
        }
        uint64_t same_total =
                mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_OUTER_LOOP] +
                mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_INNER_LOOP] +
                mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_LABEL_LOAD] +
                mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_BDD_OP] +
                mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_ADD_EDGE];
        MTPNDD_STAT_ADD(and_fastpath_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_FASTPATH]);
        MTPNDD_STAT_ADD(and_cache_hit_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_CACHE_HIT]);
        MTPNDD_STAT_ADD(and_build_edges_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_BUILD_EDGES]);
        MTPNDD_STAT_ADD(and_same_field_ns, same_total);
        MTPNDD_STAT_ADD(and_same_outer_loop_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_OUTER_LOOP]);
        MTPNDD_STAT_ADD(and_same_inner_loop_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_INNER_LOOP]);
        MTPNDD_STAT_ADD(and_same_label_load_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_LABEL_LOAD]);
        MTPNDD_STAT_ADD(and_same_bdd_op_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_BDD_OP]);
        MTPNDD_STAT_ADD(and_same_add_edge_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_SAME_ADD_EDGE]);
        MTPNDD_STAT_ADD(and_diff_field_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_DIFF_FIELD]);
        MTPNDD_STAT_ADD(and_mk_ns, mtpndd_and_prof_ctx.bucket_ns[MTPNDD_AND_PROF_MK]);
        MTPNDD_STAT_ADD(and_time_ns, total_ns);
    }
    if (mtpndd_and_depth > 0) mtpndd_and_depth--;
}

#define MTPNDD_AND_RETURN(val) do { mtpndd_and_prof_finish(); return (val); } while(0)
#else
#define mtpndd_and_prof_start() ((void)0)
#define mtpndd_and_prof_switch(bucket) ((void)0)
#define mtpndd_and_prof_finish() ((void)0)
#define MTPNDD_AND_RETURN(val) return (val)
#endif

/********************************
 * MTPNDD operations (sub task)
 ********************************/
static mtpndd_error_t mtpndd_and_same_field(edge_bucket_entry_t *entry_a,
        edge_bucket_entry_t *entry_b, mtpndd_edge_t *res_edges,
        mtpndd_temp_ref_list_t *temp_refs)
{
    mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_LABEL_LOAD);
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t label_b = mtpndd_edge_label_load(entry_b);
    mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_BDD_OP);
    mtpndd_bdd_t combined_label = sylvan_ref(sylvan_and(label_a, label_b));
    if (combined_label == sylvan_false) {
        sylvan_deref(combined_label);
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_INNER_LOOP);
        return MTPNDD_SUCCESS;
    }

    mtpndd_and_prof_switch(MTPNDD_AND_PROF_OTHER);
    mtpndd_node_t *sub_result = NULL;
    mtpndd_error_t status = mtpndd_and_rec(entry_a->child, entry_b->child, &sub_result);
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(combined_label);
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_INNER_LOOP);
        return status;
    }

    mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_ADD_EDGE);
    if (!mtpndd_temp_refs_push(temp_refs, sub_result)) {
        sylvan_deref(combined_label);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    status = mtpndd_add_edge(res_edges, sub_result, combined_label);
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(combined_label);
    }
    mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_INNER_LOOP);
    return status;
}

static mtpndd_error_t mtpndd_and_diff_field(edge_bucket_entry_t *entry_a,
        mtpndd_t *b, mtpndd_edge_t *res_edges,
        mtpndd_temp_ref_list_t *temp_refs)
{
    mtpndd_node_t *sub_result = NULL;
    mtpndd_error_t status = mtpndd_and_rec(entry_a->child, b, &sub_result);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    mtpndd_bdd_t label_a = sylvan_ref(mtpndd_edge_label_load(entry_a));
    if (!mtpndd_temp_refs_push(temp_refs, sub_result)) {
        sylvan_deref(label_a);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    status = mtpndd_add_edge(res_edges, sub_result, label_a);
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(label_a);
    }
    return status;
}

static mtpndd_error_t mtpndd_and_rec(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result) {
    mtpndd_and_prof_start();
    mtpndd_error_t status = MTPNDD_SUCCESS;
    if (mtpndd_is_false(a) || mtpndd_is_true(b)) {
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_FASTPATH);
        *result = a;
        MTPNDD_AND_RETURN(MTPNDD_SUCCESS);
    } else if (mtpndd_is_false(b) || mtpndd_is_true(a) || a == b) {
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_FASTPATH);
        *result = b;
        MTPNDD_AND_RETURN(MTPNDD_SUCCESS);
    }

    mtpndd_node_t *cache_a = a;
    mtpndd_node_t *cache_b = b;
    if ((uintptr_t)cache_a > (uintptr_t)cache_b) {
        mtpndd_node_t *tmp = cache_a;
        cache_a = cache_b;
        cache_b = tmp;
    }
    mtpndd_op_cache_t *and_cache = g_mtpndd_config.and_cache;
    mtpndd_and_prof_switch(MTPNDD_AND_PROF_CACHE_HIT);
    mtpndd_node_t *cached = mtpndd_op_cache_lookup_binary(and_cache, cache_a, cache_b);
    if (cached) {
        *result = cached;
        MTPNDD_AND_RETURN(MTPNDD_SUCCESS);
    }

    mtpndd_t *res_node = NULL;
    mtpndd_edge_t *res_edges = mtpndd_memory_acquire_edge_map();
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        MTPNDD_AND_RETURN(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    mtpndd_edge_map_init(res_edges);
    mtpndd_and_prof_switch(MTPNDD_AND_PROF_BUILD_EDGES);
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);
    if (a->field_id == b->field_id) {
        // Same field, combine edges
        edge_bucket_entry_t *entry_a;
        edge_bucket_entry_t *entry_b;
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_OUTER_LOOP);
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_INNER_LOOP);
            FOR_EACH_ENTRY_IN_ALL_BUCKETS(b->edges, entry_b) {
                status = mtpndd_and_same_field(entry_a, entry_b, res_edges, &temp_refs);
                if (status != MTPNDD_SUCCESS) {
                    mtpndd_temp_refs_release(&temp_refs);
                    mtpndd_edge_map_free(res_edges);
                    MTPNDD_AND_RETURN(status);
                }
            }
            mtpndd_and_prof_switch(MTPNDD_AND_PROF_SAME_OUTER_LOOP);
        }
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_BUILD_EDGES);
    } else {
        // Different fields
        if (a->field_id > b->field_id) {
            mtpndd_t *temp = a;
            a = b;
            b = temp;
        }
        mtpndd_and_prof_switch(MTPNDD_AND_PROF_DIFF_FIELD);
        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            status = mtpndd_and_diff_field(entry_a, b, res_edges, &temp_refs);
            if (status != MTPNDD_SUCCESS) {
                mtpndd_temp_refs_release(&temp_refs);
                mtpndd_edge_map_free(res_edges);
                MTPNDD_AND_RETURN(status);
            }
        }
    }
    mtpndd_and_prof_switch(MTPNDD_AND_PROF_MK);
#ifdef ENABLE_RECORDING
    struct timespec mk_total_start = {0};
    struct timespec mk_total_end = {0};
    struct timespec mk_phase_start = {0};
    uint64_t mk_call_ns = 0;
    uint64_t mk_gc_ns = 0;
    uint64_t mk_cache_ns = 0;
    uint64_t mk_other_ns = 0;
    clock_gettime(CLOCK_MONOTONIC, &mk_total_start);
    clock_gettime(CLOCK_MONOTONIC, &mk_phase_start);
#endif
    mtpndd_mk(a->field_id, res_edges, &res_node);
#ifdef ENABLE_RECORDING
    struct timespec mk_phase_end = {0};
    clock_gettime(CLOCK_MONOTONIC, &mk_phase_end);
    mk_call_ns = mtpndd_timespec_diff_ns(&mk_phase_start, &mk_phase_end);
    mtpndd_stat_add(&g_mtpndd_stats.and_mk_call_ns, mk_call_ns);
#endif
    if (!res_node) {
        mtpndd_temp_refs_release(&temp_refs);
        mtpndd_edge_map_free(res_edges);
#ifdef ENABLE_RECORDING
        clock_gettime(CLOCK_MONOTONIC, &mk_total_end);
        uint64_t mk_total_ns = mtpndd_timespec_diff_ns(&mk_total_start, &mk_total_end);
        mk_other_ns = 0;
        if (mk_total_ns > mk_call_ns + mk_gc_ns + mk_cache_ns) {
            mk_other_ns = mk_total_ns - mk_call_ns - mk_gc_ns - mk_cache_ns;
        }
        mtpndd_stat_add(&g_mtpndd_stats.and_mk_other_ns, mk_other_ns);
#endif
        MTPNDD_AND_RETURN(mtpndd_get_last_error().code);
    }

    mtpndd_temp_refs_release(&temp_refs);

    mtpndd_op_cache_store_binary(and_cache, cache_a, cache_b, res_node);
#ifdef ENABLE_RECORDING
    clock_gettime(CLOCK_MONOTONIC, &mk_phase_end);
    mk_cache_ns = mtpndd_timespec_diff_ns(&mk_phase_start, &mk_phase_end);
    mtpndd_stat_add(&g_mtpndd_stats.and_mk_cache_store_ns, mk_cache_ns);
    clock_gettime(CLOCK_MONOTONIC, &mk_total_end);
    uint64_t mk_total_ns = mtpndd_timespec_diff_ns(&mk_total_start, &mk_total_end);
    mk_other_ns = 0;
    if (mk_total_ns > mk_call_ns + mk_gc_ns + mk_cache_ns) {
        mk_other_ns = mk_total_ns - mk_call_ns - mk_gc_ns - mk_cache_ns;
    }
    mtpndd_stat_add(&g_mtpndd_stats.and_mk_other_ns, mk_other_ns);
#endif

    *result = res_node;
    MTPNDD_AND_RETURN(MTPNDD_SUCCESS);
}

static inline void mtpndd_residual_apply_mask(edge_bucket_entry_t *entry, mtpndd_bdd_t mask) {
    if (!entry) {
        return;
    }

    mtpndd_bdd_t expected = entry->label;
    if (expected == sylvan_false) {
        return;
    }
    mtpndd_bdd_t updated = sylvan_ref(sylvan_and(expected, mask));
    entry->label = updated;
    sylvan_deref(expected);
}

static inline mtpndd_bdd_t mtpndd_edge_label_load(edge_bucket_entry_t *entry) {
    if (!entry) {
        return sylvan_false;
    }
    return entry->label;
}

static mtpndd_error_t mtpndd_or_same_field(edge_bucket_entry_t *entry_a,
        edge_bucket_entry_t *entry_b,
        mtpndd_edge_t *res_edges,
        mtpndd_edge_t *residualA,
        mtpndd_edge_t *residualB,
        mtpndd_temp_ref_list_t *temp_refs)
{
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t label_b = mtpndd_edge_label_load(entry_b);
    mtpndd_bdd_t intersect = sylvan_ref(sylvan_and(label_a, label_b));
    if (intersect == sylvan_false) {
        sylvan_deref(intersect);
        return MTPNDD_SUCCESS;
    }

    mtpndd_bdd_t notIntersect = sylvan_ref(sylvan_not(intersect));
    edge_bucket_entry_t *residual_entry_a = find_edge_entry(residualA, entry_a->child);
    edge_bucket_entry_t *residual_entry_b = find_edge_entry(residualB, entry_b->child);
    mtpndd_residual_apply_mask(residual_entry_a, notIntersect);
    mtpndd_residual_apply_mask(residual_entry_b, notIntersect);
    sylvan_deref(notIntersect);

    mtpndd_node_t *subResult = NULL;
    mtpndd_error_t status = mtpndd_or_rec(entry_a->child, entry_b->child, &subResult);
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(intersect);
        return status;
    }

    if (!mtpndd_temp_refs_push(temp_refs, subResult)) {
        sylvan_deref(intersect);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    status = mtpndd_add_edge(res_edges, subResult, intersect);
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(intersect);
    }
    return status;
}

static mtpndd_error_t mtpndd_or_diff_field(edge_bucket_entry_t *entry_a,
        mtpndd_t *b,
        mtpndd_edge_t *res_edges,
        mtpndd_bdd_t *residualB,
        mtpndd_temp_ref_list_t *temp_refs)
{
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t notIntersect = sylvan_ref(sylvan_not(label_a));
    mtpndd_bdd_t expected = *residualB;
    mtpndd_bdd_t updated = sylvan_ref(sylvan_and(expected, notIntersect));
    *residualB = updated;
    sylvan_deref(expected);
    sylvan_deref(notIntersect);

    mtpndd_node_t *subResult = NULL;
    mtpndd_error_t status = mtpndd_or_rec(entry_a->child, b, &subResult);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    if (!mtpndd_temp_refs_push(temp_refs, subResult)) {
        sylvan_deref(label_a);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    status = mtpndd_add_edge(res_edges, subResult, sylvan_ref(label_a));
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(label_a);
    }
    return status;
}


static mtpndd_error_t mtpndd_or_rec(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result) {
    if (mtpndd_is_true(a) || mtpndd_is_false(b)) {
        *result = a;
        return MTPNDD_SUCCESS;
    } else if (mtpndd_is_true(b) || mtpndd_is_false(a) || a == b) {
        *result = b;
        return MTPNDD_SUCCESS;
    }

    mtpndd_node_t *cache_a = a;
    mtpndd_node_t *cache_b = b;
    if ((uintptr_t)cache_a > (uintptr_t)cache_b) {
        mtpndd_node_t *tmp = cache_a;
        cache_a = cache_b;
        cache_b = tmp;
    }
    mtpndd_op_cache_t *or_cache = g_mtpndd_config.or_cache;
    mtpndd_node_t *cached = mtpndd_op_cache_lookup_binary(or_cache, cache_a, cache_b);
    if (cached) {
        *result = cached;
        return MTPNDD_SUCCESS;
    }

    mtpndd_t *res_node = NULL;
    mtpndd_edge_t *res_edges = mtpndd_memory_acquire_edge_map();
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    mtpndd_edge_map_init(res_edges);
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);

    if (a->field_id == b->field_id) {
        // Same field, combine edges
        mtpndd_edge_t *residualA = mtpndd_memory_acquire_edge_map();
        if (!residualA) {
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        if (mtpndd_edge_map_deep_clone(a->edges, residualA) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(residualA);
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        mtpndd_edge_t *residualB = mtpndd_memory_acquire_edge_map();
        if (!residualB) {
            mtpndd_edge_map_free(residualA);
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        if (mtpndd_edge_map_deep_clone(b->edges, residualB) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(residualA);
            mtpndd_edge_map_free(residualB);
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        // already sylvan_ref when deep cloning

        edge_bucket_entry_t *entry_a;
        edge_bucket_entry_t *entry_b;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            FOR_EACH_ENTRY_IN_ALL_BUCKETS(b->edges, entry_b) {
                mtpndd_error_t status = mtpndd_or_same_field(entry_a, entry_b, res_edges, residualA, residualB, &temp_refs);
                if (status != MTPNDD_SUCCESS) {
                    mtpndd_edge_map_free(residualA);
                    mtpndd_edge_map_free(residualB);
                    mtpndd_edge_map_free(res_edges);
                    mtpndd_temp_refs_release(&temp_refs);
                    return status;
                }
            }
        }
        /**
         * Each residual of A doesn't match with any explicit edge of B,
         * and will match with the edge pointing to FALSE of B, which is omitted.
         * The situation is the same for B.
         */
        edge_bucket_entry_t *entry_res;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(residualA, entry_res) {
            mtpndd_bdd_t res_label = mtpndd_edge_label_load(entry_res);
            if (res_label != sylvan_false) {
                if (!mtpndd_temp_refs_push(&temp_refs, entry_res->child)) {
                    sylvan_deref(res_label);
                    mtpndd_edge_map_free(residualA);
                    mtpndd_edge_map_free(residualB);
                    mtpndd_edge_map_free(res_edges);
                    mtpndd_temp_refs_release(&temp_refs);
                    return MTPNDD_ERROR_OUT_OF_MEMORY;
                }
                mtpndd_error_t status = mtpndd_add_edge(res_edges, entry_res->child, sylvan_ref(res_label));
                if (status != MTPNDD_SUCCESS) {
                    mtpndd_edge_map_free(residualA);
                    mtpndd_edge_map_free(residualB);
                    mtpndd_edge_map_free(res_edges);
                    mtpndd_temp_refs_release(&temp_refs);
                    return status;
                }
            }
        }
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(residualB, entry_res) {
            mtpndd_bdd_t res_label = mtpndd_edge_label_load(entry_res);
            if (res_label != sylvan_false) {
                if (!mtpndd_temp_refs_push(&temp_refs, entry_res->child)) {
                    sylvan_deref(res_label);
                    mtpndd_edge_map_free(residualA);
                    mtpndd_edge_map_free(residualB);
                    mtpndd_edge_map_free(res_edges);
                    mtpndd_temp_refs_release(&temp_refs);
                    return MTPNDD_ERROR_OUT_OF_MEMORY;
                }
                mtpndd_error_t status = mtpndd_add_edge(res_edges, entry_res->child, sylvan_ref(res_label));
                if (status != MTPNDD_SUCCESS) {
                    mtpndd_edge_map_free(residualA);
                    mtpndd_edge_map_free(residualB);
                    mtpndd_edge_map_free(res_edges);
                    mtpndd_temp_refs_release(&temp_refs);
                    return status;
                }
            }
        }
        mtpndd_edge_map_free(residualA);
        mtpndd_edge_map_free(residualB);
    } else {
        // Different fields
    if (a->field_id > b->field_id) {
        mtpndd_t *temp = a;
        a = b;
        b = temp;
    }

        mtpndd_bdd_t residualB = sylvan_true;
        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            mtpndd_error_t status = mtpndd_or_diff_field(entry_a, b, res_edges, &residualB, &temp_refs);
            if (status != MTPNDD_SUCCESS) {
                mtpndd_edge_map_free(res_edges);
                mtpndd_temp_refs_release(&temp_refs);
                return status;
            }
        }
        mtpndd_bdd_t residualB_val = residualB;
        if (residualB_val != sylvan_false) {
            if (!mtpndd_temp_refs_push(&temp_refs, b)) {
                sylvan_deref(residualB_val);
                mtpndd_edge_map_free(res_edges);
                mtpndd_temp_refs_release(&temp_refs);
                return MTPNDD_ERROR_OUT_OF_MEMORY;
            }
            mtpndd_error_t status = mtpndd_add_edge(res_edges, b, residualB_val);
            if (status != MTPNDD_SUCCESS) {
                sylvan_deref(residualB_val);
                mtpndd_edge_map_free(res_edges);
                mtpndd_temp_refs_release(&temp_refs);
                return status;
            }
        }
    }

    mtpndd_mk(a->field_id, res_edges, &res_node);
    if (!res_node) {
        mtpndd_edge_map_free(res_edges);
        mtpndd_temp_refs_release(&temp_refs);
        return mtpndd_get_last_error().code;
    }
    mtpndd_temp_refs_release(&temp_refs);

    mtpndd_op_cache_store_binary(or_cache, cache_a, cache_b, res_node);

    *result = res_node;
    return MTPNDD_SUCCESS;
}

static mtpndd_error_t mtpndd_not_expand(edge_bucket_entry_t *entry_a,
        mtpndd_edge_t *res_edges,
        mtpndd_bdd_t *residual,
        mtpndd_temp_ref_list_t *temp_refs)
{
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t notIntersect = sylvan_ref(sylvan_not(label_a));
    mtpndd_bdd_t expected = *residual;
    mtpndd_bdd_t updated = sylvan_ref(sylvan_and(expected, notIntersect));
    *residual = updated;
    sylvan_deref(expected);
    sylvan_deref(notIntersect);

    mtpndd_node_t *subResult = NULL;
    mtpndd_error_t status = mtpndd_not_rec(entry_a->child, &subResult);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    if (!mtpndd_temp_refs_push(temp_refs, subResult)) {
        sylvan_deref(label_a);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    status = mtpndd_add_edge(res_edges, subResult, sylvan_ref(label_a));
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(label_a);
    }
    return status;
}

static mtpndd_error_t mtpndd_not_rec(mtpndd_t *a, mtpndd_t **result) {
    if (mtpndd_is_true(a)) {
        *result = &MTPNDD_FALSE;
        return MTPNDD_SUCCESS;
    } else if (mtpndd_is_false(a)) {
        *result = &MTPNDD_TRUE;
        return MTPNDD_SUCCESS;
    }

    mtpndd_op_cache_t *not_cache = g_mtpndd_config.not_cache;
    mtpndd_node_t *cached = mtpndd_op_cache_lookup_unary(not_cache, a);
    if (cached) {
        *result = cached;
        return MTPNDD_SUCCESS;
    }

    mtpndd_edge_t *res_edges = mtpndd_memory_acquire_edge_map();
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    mtpndd_edge_map_init(res_edges);
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);

    mtpndd_bdd_t residual = sylvan_true;
    mtpndd_t *res_node = NULL;
    edge_bucket_entry_t *entry_a;
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
        mtpndd_error_t status = mtpndd_not_expand(entry_a, res_edges, &residual, &temp_refs);
        if (status != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            return status;
        }
    }
    mtpndd_bdd_t residual_val = residual;
    if (residual_val != sylvan_false) {
        if (!mtpndd_temp_refs_push(&temp_refs, &MTPNDD_TRUE)) {
            sylvan_deref(residual_val);
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        mtpndd_error_t status = mtpndd_add_edge(res_edges, &MTPNDD_TRUE, residual_val);
        if (status != MTPNDD_SUCCESS) {
            sylvan_deref(residual_val);
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            return status;
        }
    }
    mtpndd_mk(a->field_id, res_edges, &res_node);
    if (!res_node) {
        mtpndd_edge_map_free(res_edges);
        mtpndd_temp_refs_release(&temp_refs);
        return mtpndd_get_last_error().code;
    }
    mtpndd_temp_refs_release(&temp_refs);

    mtpndd_op_cache_store_unary(not_cache, a, res_node);

    *result = res_node;
    return MTPNDD_SUCCESS;
}

// diff = a AND (NOT b)
static mtpndd_error_t mtpndd_exist_expand(edge_bucket_entry_t *entry_a,
        mtpndd_edge_t *res_edges, uint32_t field,
        mtpndd_temp_ref_list_t *temp_refs)
{
    mtpndd_t *subResult = NULL;
    mtpndd_error_t status = mtpndd_exist_rec(entry_a->child, field, &subResult);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    if (!mtpndd_temp_refs_push(temp_refs, subResult)) {
        sylvan_deref(label_a);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    status = mtpndd_add_edge(res_edges, subResult, sylvan_ref(label_a));
    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(label_a);
    }
    return status;
}

static mtpndd_error_t mtpndd_exist_rec(mtpndd_t *a, uint32_t field, mtpndd_t **result) {
    if (mtpndd_is_terminal(a)) {
        *result = a;
        return MTPNDD_SUCCESS;
    }

    mtpndd_t *res_node = &MTPNDD_FALSE;
    if (a->field_id == field) {
        edge_bucket_entry_t *entry_a = NULL;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            mtpndd_error_t status = mtpndd_or_rec(res_node, entry_a->child, &res_node);
            if (status != MTPNDD_SUCCESS) {
                return status;
            }
        }
    } else {
        // Keep this field
        mtpndd_edge_t *res_edges = mtpndd_memory_acquire_edge_map();
        if (!res_edges) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        mtpndd_edge_map_init(res_edges);

        mtpndd_temp_ref_list_t temp_refs;
        mtpndd_temp_refs_init(&temp_refs);
        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            mtpndd_error_t status = mtpndd_exist_expand(entry_a, res_edges, field, &temp_refs);
            if (status != MTPNDD_SUCCESS) {
                mtpndd_edge_map_free(res_edges);
                mtpndd_temp_refs_release(&temp_refs);
                return status;
            }
        }
        mtpndd_mk(a->field_id, res_edges, &res_node);
        if (!res_node) {
            mtpndd_edge_map_free(res_edges);
            mtpndd_temp_refs_release(&temp_refs);
            return mtpndd_get_last_error().code;
        }
        mtpndd_temp_refs_release(&temp_refs);
    }
    *result = res_node;
    return MTPNDD_SUCCESS;
}

/********************************
 * MTPNDD operations (wrappers)
 ********************************/
mtpndd_t *mtpndd_and(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_t *result = NULL;
    if (mtpndd_and_rec(a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_t *mtpndd_or(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_t *result = NULL;
    MTPNDD_RECORD_TIME_START(_t_or);
    if (mtpndd_or_rec(a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    MTPNDD_RECORD_TIME_END(or_time_ns, _t_or);
    return result;
}

mtpndd_t *mtpndd_not(mtpndd_t *a) {
    mtpndd_t *result = NULL;
    MTPNDD_RECORD_TIME_START(_t_not);
    if (mtpndd_not_rec(a, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    MTPNDD_RECORD_TIME_END(not_time_ns, _t_not);
    return result;
}

mtpndd_t *mtpndd_diff(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_t *result = NULL;
    mtpndd_t *not_b = NULL;
    if (mtpndd_not_rec(b, &not_b) != MTPNDD_SUCCESS) {
        return NULL;
    }
    if (mtpndd_and_rec(a, not_b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_t *mtpndd_exist(mtpndd_t *a, uint32_t field) {
    mtpndd_t *result = NULL;
    if (mtpndd_exist_rec(a, field, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

/********************************
 * MTPNDD <-> MTBDD convertion
 ********************************/
typedef struct {
    mtpndd_node_t **keys;
    mtpndd_bdd_t *values;
    size_t capacity;
    size_t count;
} mtpndd_to_mtbdd_cache_t;

typedef struct {
    mtpndd_bdd_t *keys;
    mtpndd_node_t **values;
    size_t capacity;
    size_t count;
} mtbdd_to_mtpndd_cache_t;

static mtpndd_error_t mtpndd_to_mtbdd_cache_init(mtpndd_to_mtbdd_cache_t *cache, size_t initial_capacity) {
    cache->capacity = 1;
    while (cache->capacity < initial_capacity) {
        cache->capacity <<= 1;
    }
    cache->count = 0;
    cache->keys = (mtpndd_node_t **)calloc(cache->capacity, sizeof(mtpndd_node_t *));
    cache->values = (mtpndd_bdd_t *)calloc(cache->capacity, sizeof(mtpndd_bdd_t));
    if (!cache->keys || !cache->values) {
        free(cache->keys);
        free(cache->values);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    return MTPNDD_SUCCESS;
}

static void mtpndd_to_mtbdd_cache_destroy(mtpndd_to_mtbdd_cache_t *cache) {
    if (!cache || !cache->keys) {
        return;
    }
    for (size_t i = 0; i < cache->capacity; ++i) {
        if (cache->keys[i]) {
            sylvan_deref(cache->values[i]);
        }
    }
    free(cache->keys);
    free(cache->values);
    cache->keys = NULL;
    cache->values = NULL;
    cache->capacity = 0;
    cache->count = 0;
}

static inline void mtpndd_to_mtbdd_cache_place(
        mtpndd_to_mtbdd_cache_t *cache,
        mtpndd_node_t *key,
        mtpndd_bdd_t value,
        bool take_ref)
{
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64((uint64_t)(uintptr_t)key) & mask;
    while (cache->keys[idx]) {
        idx = (idx + 1) & mask;
    }
    cache->keys[idx] = key;
    cache->values[idx] = take_ref ? sylvan_ref(value) : value;
    cache->count++;
}

static bool mtpndd_to_mtbdd_cache_lookup(mtpndd_to_mtbdd_cache_t *cache, mtpndd_node_t *key, mtpndd_bdd_t *value) {
    if (!cache->keys || cache->count == 0) {
        return false;
    }
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64((uint64_t)(uintptr_t)key) & mask;
    while (cache->keys[idx]) {
        if (cache->keys[idx] == key) {
            *value = sylvan_ref(cache->values[idx]);
            return true;
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

static mtpndd_error_t mtpndd_to_mtbdd_cache_insert(mtpndd_to_mtbdd_cache_t *cache, mtpndd_node_t *key, mtpndd_bdd_t value);

static mtpndd_error_t mtpndd_to_mtbdd_cache_rehash(mtpndd_to_mtbdd_cache_t *cache) {
    size_t new_capacity = cache->capacity << 1;
    mtpndd_node_t **new_keys = (mtpndd_node_t **)calloc(new_capacity, sizeof(mtpndd_node_t *));
    mtpndd_bdd_t *new_values = (mtpndd_bdd_t *)calloc(new_capacity, sizeof(mtpndd_bdd_t));
    if (!new_keys || !new_values) {
        free(new_keys);
        free(new_values);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    size_t old_capacity = cache->capacity;
    mtpndd_node_t **old_keys = cache->keys;
    mtpndd_bdd_t *old_values = cache->values;
    cache->keys = new_keys;
    cache->values = new_values;
    cache->capacity = new_capacity;
    cache->count = 0;
    for (size_t i = 0; i < old_capacity; ++i) {
        if (old_keys[i]) {
            mtpndd_to_mtbdd_cache_place(cache, old_keys[i], old_values[i], false);
        }
    }
    free(old_keys);
    free(old_values);
    return MTPNDD_SUCCESS;
}

static mtpndd_error_t mtpndd_to_mtbdd_cache_insert(mtpndd_to_mtbdd_cache_t *cache, mtpndd_node_t *key, mtpndd_bdd_t value) {
    if ((cache->count + 1) * 4 >= cache->capacity * 3) {
        mtpndd_error_t err = mtpndd_to_mtbdd_cache_rehash(cache);
        if (err != MTPNDD_SUCCESS) {
            return err;
        }
    }
    mtpndd_to_mtbdd_cache_place(cache, key, value, true);
    return MTPNDD_SUCCESS;
}

static mtpndd_error_t mtbdd_to_mtpndd_cache_init(mtbdd_to_mtpndd_cache_t *cache, size_t initial_capacity) {
    cache->capacity = 1;
    while (cache->capacity < initial_capacity) {
        cache->capacity <<= 1;
    }
    cache->count = 0;
    cache->keys = (mtpndd_bdd_t *)calloc(cache->capacity, sizeof(mtpndd_bdd_t));
    cache->values = (mtpndd_t **)calloc(cache->capacity, sizeof(mtpndd_t *));
    if (!cache->keys || !cache->values) {
        free(cache->keys);
        free(cache->values);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    return MTPNDD_SUCCESS;
}

static void mtbdd_to_mtpndd_cache_destroy(mtbdd_to_mtpndd_cache_t *cache) {
    if (!cache || !cache->keys) {
        return;
    }
    free(cache->keys);
    free(cache->values);
    cache->keys = NULL;
    cache->values = NULL;
    cache->capacity = 0;
    cache->count = 0;
}

static bool mtbdd_to_mtpndd_cache_lookup(mtbdd_to_mtpndd_cache_t *cache, mtpndd_bdd_t key, mtpndd_t **value) {
    if (!cache->keys || cache->count == 0) {
        return false;
    }
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64(key) & mask;
    while (cache->keys[idx]) {
        if (cache->keys[idx] == key) {
            *value = cache->values[idx];
            return true;
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

static mtpndd_error_t mtbdd_to_mtpndd_cache_insert(mtbdd_to_mtpndd_cache_t *cache, mtpndd_bdd_t key, mtpndd_t *value);

static mtpndd_error_t mtbdd_to_mtpndd_cache_rehash(mtbdd_to_mtpndd_cache_t *cache) {
    size_t new_capacity = cache->capacity << 1;
    mtpndd_bdd_t *new_keys = (mtpndd_bdd_t *)calloc(new_capacity, sizeof(mtpndd_bdd_t));
    mtpndd_t **new_values = (mtpndd_t **)calloc(new_capacity, sizeof(mtpndd_t *));
    if (!new_keys || !new_values) {
        free(new_keys);
        free(new_values);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    size_t old_capacity = cache->capacity;
    mtpndd_bdd_t *old_keys = cache->keys;
    mtpndd_t **old_values = cache->values;
    cache->keys = new_keys;
    cache->values = new_values;
    cache->capacity = new_capacity;
    cache->count = 0;
    for (size_t i = 0; i < old_capacity; ++i) {
        if (old_keys[i]) {
            mtbdd_to_mtpndd_cache_insert(cache, old_keys[i], old_values[i]);
        }
    }
    free(old_keys);
    free(old_values);
    return MTPNDD_SUCCESS;
}

static mtpndd_error_t mtbdd_to_mtpndd_cache_insert(mtbdd_to_mtpndd_cache_t *cache, mtpndd_bdd_t key, mtpndd_t *value) {
    if ((cache->count + 1) * 4 >= cache->capacity * 3) {
        mtpndd_error_t err = mtbdd_to_mtpndd_cache_rehash(cache);
        if (err != MTPNDD_SUCCESS) {
            return err;
        }
    }
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64(key) & mask;
    while (cache->keys[idx]) {
        idx = (idx + 1) & mask;
    }
    cache->keys[idx] = key;
    cache->values[idx] = value;
    cache->count++;
    return MTPNDD_SUCCESS;
}

static uint32_t mtpndd_field_expanded_start(uint32_t field_id) {
    uint32_t start = 0;
    for (uint32_t i = 1; i < field_id; ++i) {
        start += g_mtpndd_config.pending_field_bit_widths[i - 1];
    }
    return start;
}

static mtpndd_field_info_t *mtpndd_find_field_by_expanded_var(uint32_t var) {
    uint32_t start = 0;
    for (uint32_t i = 1; i <= g_mtpndd_config.field_count; ++i) {
        uint32_t width = g_mtpndd_config.pending_field_bit_widths[i - 1];
        uint32_t end = start + width;
        if (var >= start && var < end) {
            return g_mtpndd_config.field_info[i];
        }
        start = end;
    }
    return NULL;
}

static mtpndd_bdd_t mtpndd_replace_shared_to_expanded(mtpndd_bdd_t bdd, uint32_t field_id) {
    if (bdd == sylvan_false || bdd == sylvan_true) {
        return sylvan_ref(bdd);
    }

    uint32_t bit_width = g_mtpndd_config.pending_field_bit_widths[field_id - 1];
    uint32_t shared_offset = g_mtpndd_config.max_bit_width - bit_width;
    uint32_t expanded_offset = mtpndd_field_expanded_start(field_id);

    if (expanded_offset < g_mtpndd_config.max_bit_width) {
        return sylvan_ref(bdd);
    }

    BDDMAP map = sylvan_map_empty();
    for (uint32_t i = 0; i < bit_width; ++i) {
        uint32_t key = shared_offset + i;
        uint32_t expanded_var = expanded_offset + i;
        map = sylvan_map_add(map, key, sylvan_ithvar(expanded_var));
    }

    BDDMAP map_ref = sylvan_ref(map);
    mtpndd_bdd_t replaced = sylvan_ref(sylvan_compose(bdd, map_ref));
    sylvan_deref(map_ref);
    return replaced;
}

static mtpndd_error_t mtpndd_to_mtbdd_rec(mtpndd_t *node, mtpndd_to_mtbdd_cache_t *cache, mtpndd_bdd_t *result) {
    if (mtpndd_is_true(node)) {
        *result = sylvan_true;
        return MTPNDD_SUCCESS;
    }
    if (mtpndd_is_false(node)) {
        *result = sylvan_false;
        return MTPNDD_SUCCESS;
    }

    mtpndd_bdd_t cached;
    if (mtpndd_to_mtbdd_cache_lookup(cache, node, &cached)) {
        *result = cached;
        return MTPNDD_SUCCESS;
    }

    mtpndd_bdd_t acc = sylvan_ref(sylvan_false);
    mtpndd_error_t status = MTPNDD_SUCCESS;
    size_t bucket_cnt = (node->edges && node->edges->buckets) ? (node->edges->bucket_count ? node->edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count) : 0;
    for (size_t bucket = 0; bucket < bucket_cnt; ++bucket) {
        edge_bucket_entry_t *head = node->edges->buckets[bucket];
        if (!head) {
            continue;
        }
        edge_bucket_entry_t *entry = head;
        while (entry) {
            if (entry->child) {
                mtpndd_bdd_t label = mtpndd_edge_label_load(entry);
                if (label != sylvan_false) {
                    mtpndd_bdd_t child_bdd = sylvan_false;
                    status = mtpndd_to_mtbdd_rec(entry->child, cache, &child_bdd);
                    if (status != MTPNDD_SUCCESS) {
                        break;
                    }
                    mtpndd_bdd_t mapped_label = mtpndd_replace_shared_to_expanded(label, node->field_id);
                    mtpndd_bdd_t conjunct = sylvan_ref(sylvan_and(mapped_label, child_bdd));
                    sylvan_deref(mapped_label);
                    if (conjunct != sylvan_false) {
                        mtpndd_bdd_t combined = sylvan_ref(sylvan_or(acc, conjunct));
                        sylvan_deref(acc);
                        sylvan_deref(conjunct);
                        acc = combined;
                    } else {
                        sylvan_deref(conjunct);
                    }
                }
            }
            entry = entry->next;
        }
        if (status != MTPNDD_SUCCESS) {
            break;
        }
    }

    if (status != MTPNDD_SUCCESS) {
        sylvan_deref(acc);
        return status;
    }

    mtpndd_error_t err = mtpndd_to_mtbdd_cache_insert(cache, node, acc);
    if (err != MTPNDD_SUCCESS) {
        sylvan_deref(acc);
        return err;
    }

    *result = acc;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_to_mtbdd(mtpndd_t *node, mtpndd_bdd_t *result) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_NULL(node, MTPNDD_ERROR_NULL_POINTER);
    MTPNDD_CHECK_NULL(result, MTPNDD_ERROR_NULL_POINTER);

    mtpndd_to_mtbdd_cache_t cache;
    mtpndd_error_t status = mtpndd_to_mtbdd_cache_init(&cache, 64);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    mtpndd_bdd_t tmp = sylvan_false;
    status = mtpndd_to_mtbdd_rec(node, &cache, &tmp);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_to_mtbdd_cache_destroy(&cache);
        return status;
    }
#ifdef ENABLE_RECORDING
    if (status == MTPNDD_SUCCESS) {
        uint64_t nodecount = sylvan_nodecount(tmp);
        MTPNDD_STAT_SET(bdd_nodes_converted, nodecount);
        MTPNDD_STAT_ADD(bdd_nodes_processed_total, nodecount);
    }
#endif

    mtpndd_bdd_t final = sylvan_ref(tmp);
    mtpndd_to_mtbdd_cache_destroy(&cache);
    sylvan_deref(tmp);
    *result = final;
    return MTPNDD_SUCCESS;
}

static mtpndd_error_t mtbdd_to_mtpndd_rec(mtpndd_bdd_t bdd, mtbdd_to_mtpndd_cache_t *cache, mtpndd_t **result);

static mtpndd_error_t mtbdd_to_mtpndd_collect(
        mtpndd_bdd_t current,
        const mtpndd_field_info_t *field,
        uint32_t field_start,
        uint32_t offset,
        mtpndd_bdd_t cube,
        mtbdd_to_mtpndd_cache_t *cache,
        mtpndd_edge_t *edges)
{
    if (current == sylvan_false) {
        return MTPNDD_SUCCESS;
    }

    uint32_t field_end = field_start + field->bit_width;
    if (offset >= field->bit_width || mtbdd_isleaf(current) ||
        (!mtbdd_isleaf(current) && sylvan_var(current) >= field_end)) {
        mtpndd_t *child = NULL;
        mtpndd_error_t status = mtbdd_to_mtpndd_rec(current, cache, &child);
        if (status != MTPNDD_SUCCESS) {
            return status;
        }
        mtpndd_bdd_t label = sylvan_ref(cube);
        status = mtpndd_add_edge(edges, child, label);
        if (status != MTPNDD_SUCCESS) {
            sylvan_deref(label);
        }
        return status;
    }

    uint32_t var = field_start + offset;
    mtpndd_bdd_t literal_pos = field->bdd_vars[offset];
    mtpndd_bdd_t literal_neg = field->bdd_not_vars[offset];

    mtpndd_bdd_t low_child = current;
    mtpndd_bdd_t high_child = current;
    if (!mtbdd_isleaf(current)) {
        uint32_t top_var = sylvan_var(current);
        if (top_var == var) {
            low_child = sylvan_low(current);
            high_child = sylvan_high(current);
        }
    }

    mtpndd_error_t status;
    mtpndd_bdd_t cube_low = sylvan_ref(sylvan_and(cube, literal_neg));
    status = mtbdd_to_mtpndd_collect(low_child, field, field_start, offset + 1, cube_low, cache, edges);
    sylvan_deref(cube_low);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    mtpndd_bdd_t cube_high = sylvan_ref(sylvan_and(cube, literal_pos));
    status = mtbdd_to_mtpndd_collect(high_child, field, field_start, offset + 1, cube_high, cache, edges);
    sylvan_deref(cube_high);
    return status;
}

static mtpndd_error_t mtbdd_to_mtpndd_rec(mtpndd_bdd_t bdd, mtbdd_to_mtpndd_cache_t *cache, mtpndd_t **result) {
    if (bdd == sylvan_false) {
        *result = &MTPNDD_FALSE;
        return MTPNDD_SUCCESS;
    }
    if (bdd == sylvan_true) {
        *result = &MTPNDD_TRUE;
        return MTPNDD_SUCCESS;
    }

    mtpndd_t *cached;
    if (mtbdd_to_mtpndd_cache_lookup(cache, bdd, &cached)) {
        *result = cached;
        return MTPNDD_SUCCESS;
    }

    uint32_t var = sylvan_var(bdd);
    mtpndd_field_info_t *field = mtpndd_find_field_by_expanded_var(var);
    if (!field) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_FIELD, __func__, __LINE__);
        return MTPNDD_ERROR_INVALID_FIELD;
    }
    uint32_t field_start = mtpndd_field_expanded_start(field->field_id);

    mtpndd_edge_t *edges = mtpndd_memory_acquire_edge_map();
    if (!edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    mtpndd_edge_map_init(edges);

    mtpndd_error_t status = mtbdd_to_mtpndd_collect(bdd, field, field_start, 0, sylvan_true, cache, edges);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_edge_map_free(edges);
        return status;
    }

    mtpndd_t *node = NULL;
    mtpndd_mk(field->field_id, edges, &node);
    if (!node) {
        mtpndd_edge_map_free(edges);
        return mtpndd_get_last_error().code;
    }

    mtbdd_to_mtpndd_cache_insert(cache, bdd, node);
    *result = node;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtbdd_to_mtpndd(mtpndd_bdd_t bdd, mtpndd_t **result) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_NULL(result, MTPNDD_ERROR_NULL_POINTER);

#ifdef ENABLE_RECORDING
    uint64_t bdd_nodecount = sylvan_nodecount(bdd);
    MTPNDD_STAT_SET(bdd_nodes_converted, bdd_nodecount);
    MTPNDD_STAT_ADD(bdd_nodes_processed_total, bdd_nodecount);
#endif

    mtbdd_to_mtpndd_cache_t cache;
    mtpndd_error_t status = mtbdd_to_mtpndd_cache_init(&cache, 64);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    mtpndd_t *tmp = NULL;
    status = mtbdd_to_mtpndd_rec(bdd, &cache, &tmp);
    mtbdd_to_mtpndd_cache_destroy(&cache);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    *result = tmp;
    return MTPNDD_SUCCESS;
}

static size_t mtpndd_total_bdd_vars(void) {
    size_t total = 0;
    for (uint32_t i = 1; i <= g_mtpndd_config.field_count; ++i) {
        total += g_mtpndd_config.pending_field_bit_widths[i - 1];
    }
    return total;
}

static double mtpndd_satcount_rec(mtpndd_t *node, uint32_t field) {
    if (mtpndd_is_false(node)) {
        return 0.0;
    }
    if (mtpndd_is_true(node)) {
        if (field > g_mtpndd_config.field_count) {
            return 1.0;
        }
        double result = 1.0;
        for (uint32_t f = field; f <= g_mtpndd_config.field_count; ++f) {
            uint32_t bits = g_mtpndd_config.pending_field_bit_widths[f - 1];
            result *= pow(2.0, (double)bits);
        }
        return result;
    }

    if (field == node->field_id) {
        uint32_t field_bits = g_mtpndd_config.pending_field_bit_widths[field - 1];
        double divisor = pow(2.0, (double)(g_mtpndd_config.max_bit_width - field_bits));
        double result = 0.0;
        size_t bucket_cnt = (node->edges && node->edges->buckets)
                ? (node->edges->bucket_count ? node->edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count)
                : 0;
        for (size_t bucket = 0; bucket < bucket_cnt; ++bucket) {
            edge_bucket_entry_t *entry = node->edges->buckets[bucket];
            while (entry) {
                if (entry->child) {
                    mtpndd_bdd_t label = mtpndd_edge_label_load(entry);
                    if (label != sylvan_false) {
                        double bdd_sat = (label == sylvan_true)
                                ? pow(2.0, (double)g_mtpndd_config.max_bit_width)
                                : mtbdd_satcount(label, g_mtpndd_config.max_bit_width);
                        double ndd_sat = mtpndd_satcount_rec(entry->child, field + 1);
                        result += (bdd_sat / divisor) * ndd_sat;
                    }
                }
                entry = entry->next;
            }
        }
        return result;
    }

    uint32_t bits = g_mtpndd_config.pending_field_bit_widths[field - 1];
    return pow(2.0, (double)bits) * mtpndd_satcount_rec(node, field + 1);
}

double mtpndd_satcount_ndd(mtpndd_t *node) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_NULL(node, MTPNDD_ERROR_NULL_POINTER);
    return mtpndd_satcount_rec(node, 1);
}

double mtpndd_satcount_mtbdd(mtpndd_t *node) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_NULL(node, MTPNDD_ERROR_NULL_POINTER);

    mtpndd_bdd_t bdd = sylvan_false;
    mtpndd_error_t status = mtpndd_to_mtbdd(node, &bdd);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    size_t nvars = mtpndd_total_bdd_vars();
    double count = mtbdd_satcount(bdd, nvars);
    sylvan_deref(bdd);

    return count;
}

double mtpndd_satcount(mtpndd_t *node) {
    return mtpndd_satcount_ndd(node);
}

/********************************
 * DOT visualization utilities
 ********************************/
typedef struct mtpndd_dot_visit_s {
    const mtpndd_node_t *node;
    size_t id;
    struct mtpndd_dot_visit_s *next;
} mtpndd_dot_visit_t;

typedef struct {
    mtpndd_dot_visit_t *visited;
    size_t next_id;
    const mtpndd_t *root;
    bool error;
} mtpndd_dot_ctx_t;

static void mtpndd_dot_ctx_cleanup(mtpndd_dot_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    mtpndd_dot_visit_t *entry = ctx->visited;
    while (entry) {
        mtpndd_dot_visit_t *next = entry->next;
        free(entry);
        entry = next;
    }
    ctx->visited = NULL;
    ctx->next_id = 0;
    ctx->error = false;
}

static size_t mtpndd_dot_ctx_get_id(mtpndd_dot_ctx_t *ctx, const mtpndd_node_t *node, bool *is_new) {
    if (!ctx || !node) {
        if (is_new) {
            *is_new = false;
        }
        return 0;
    }

    for (mtpndd_dot_visit_t *entry = ctx->visited; entry; entry = entry->next) {
        if (entry->node == node) {
            if (is_new) {
                *is_new = false;
            }
            return entry->id;
        }
    }

    mtpndd_dot_visit_t *entry = (mtpndd_dot_visit_t *)malloc(sizeof(mtpndd_dot_visit_t));
    if (!entry) {
        ctx->error = true;
        if (is_new) {
            *is_new = false;
        }
        return 0;
    }

    entry->node = node;
    entry->id = ctx->next_id++;
    entry->next = ctx->visited;
    ctx->visited = entry;

    if (is_new) {
        *is_new = true;
    }
    return entry->id;
}

static void mtpndd_dot_format_edge_label(mtpndd_bdd_t label, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    if (label == sylvan_true) {
        (void)snprintf(buffer, buffer_size, "TRUE");
    } else if (label == sylvan_false) {
        (void)snprintf(buffer, buffer_size, "FALSE");
    } else {
        (void)snprintf(buffer, buffer_size, "0x%" PRIx64, (uint64_t)label);
    }
}

static void mtpndd_dot_emit_node(FILE *out, const mtpndd_t *node, size_t node_id, bool highlight_root) {
    if (!out || !node) {
        return;
    }

    if (mtpndd_is_true((mtpndd_t *)node)) {
        (void)fprintf(out,
                "    n%zu [label=\"TRUE\", shape=box, style=\"filled\", fillcolor=\"#dff0d8\", peripheries=%d];\n",
                node_id,
                highlight_root ? 2 : 1);
        return;
    }

    if (mtpndd_is_false((mtpndd_t *)node)) {
        (void)fprintf(out,
                "    n%zu [label=\"FALSE\", shape=box, style=\"filled\", fillcolor=\"#f2dede\", peripheries=%d];\n",
                node_id,
                highlight_root ? 2 : 1);
        return;
    }

    const mtpndd_field_info_t *field = (node->field_id > 0 && node->field_id <= g_mtpndd_config.field_count)
                                           ? g_mtpndd_config.field_info[node->field_id]
                                           : NULL;
    size_t edge_count = (node->edges != NULL) ? node->edges->edge_count : 0;
    uint32_t field_id = node->field_id;
    uint32_t bit_width = field ? field->bit_width : 0;

    (void)fprintf(out,
            "    n%zu [label=\"Field %u\\nBits %u\\nEdges %zu\", shape=ellipse, peripheries=%d];\n",
            node_id,
            field_id,
            bit_width,
            edge_count,
            highlight_root ? 2 : 1);
}

static void mtpndd_fprint_dot_rec(FILE *out, mtpndd_t *node, mtpndd_dot_ctx_t *ctx) {
    if (!out || !node || !ctx || ctx->error) {
        return;
    }

    bool is_new = false;
    size_t node_id = mtpndd_dot_ctx_get_id(ctx, node, &is_new);
    if (ctx->error || !is_new) {
        return;
    }

    mtpndd_dot_emit_node(out, node, node_id, node == ctx->root);

    if (!node->edges || node->edges->edge_count == 0) {
        return;
    }

    edge_bucket_entry_t *entry = NULL;
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(node->edges, entry) {
        if (!entry || !entry->child) {
            continue;
        }
        mtpndd_t *child = entry->child;
        bool child_is_new = false;
        size_t child_id = mtpndd_dot_ctx_get_id(ctx, child, &child_is_new);
        if (ctx->error) {
            return;
        }

        char label_buffer[32] = {0};
        mtpndd_bdd_t label = entry->label;
        mtpndd_dot_format_edge_label(label, label_buffer, sizeof(label_buffer));

        (void)fprintf(out,
                "    n%zu -> n%zu [label=\"%s\"];\n",
                node_id,
                child_id,
                label_buffer);

        if (child_is_new) {
            mtpndd_fprint_dot_rec(out, child, ctx);
        }
    }
}

void mtpndd_fprint_dot(FILE *out, mtpndd_t *root) {
    if (!root) {
        return;
    }
    if (!out) {
        out = stdout;
    }

    mtpndd_dot_ctx_t ctx = {
        .visited = NULL,
        .next_id = 0,
        .root = root,
        .error = false
    };

    (void)fprintf(out, "digraph MTPNDD {\n");
    (void)fprintf(out, "    rankdir=TB;\n");
    (void)fprintf(out, "    node [fontname=\"Helvetica\"];\n");
    (void)fprintf(out, "    edge [fontname=\"Helvetica\"];\n");

    mtpndd_fprint_dot_rec(out, root, &ctx);

    (void)fprintf(out, "}\n");

    mtpndd_dot_ctx_cleanup(&ctx);
}

void mtpndd_print_dot(mtpndd_t *root) {
    mtpndd_fprint_dot(stdout, root);
}
