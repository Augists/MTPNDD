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
#include <stdatomic.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <lace.h>
#include <lace14.h>
#ifdef ENABLE_RECORDING
#include <time.h>
#endif

mtpndd_t MTPNDD_TRUE = {0};
mtpndd_t MTPNDD_FALSE = {0};

static edge_bucket_entry_t *mtpndd_edge_entry_create(void);
static edge_bucket_entry_t *mtpndd_edge_entry_create_sentinel(void);
static void mtpndd_edge_entry_destroy(edge_bucket_entry_t *entry);
static mtpndd_edge_t *mtpndd_edge_map_create_empty(void);
static void mtpndd_edge_map_release_uninitialized(mtpndd_edge_t *edges);

static inline mtpndd_bdd_t mtpndd_edge_label_load(edge_bucket_entry_t *entry);
static inline void mtpndd_residual_apply_mask(edge_bucket_entry_t *entry, mtpndd_bdd_t mask);
static inline mtpndd_error_t mtpndd_edge_map_init_local(mtpndd_edge_t *edges);
static mtpndd_error_t mtpndd_edge_map_deep_clone(const mtpndd_edge_t *source, mtpndd_edge_t *dest);
static void mtpndd_edge_map_reset(mtpndd_edge_t *edges);
void mtpndd_edge_map_free(mtpndd_edge_t *edges);

static edge_bucket_entry_t *mtpndd_edge_entry_create(void) {
    edge_bucket_entry_t *entry = mtpndd_memory_acquire_edge_entry();
    if (!entry) {
        return NULL;
    }
    memset(entry, 0, sizeof(edge_bucket_entry_t));
    return entry;
}

static edge_bucket_entry_t *mtpndd_edge_entry_create_sentinel(void) {
    edge_bucket_entry_t *entry = mtpndd_edge_entry_create();
    if (!entry) {
        return NULL;
    }
    entry->next = entry;
    entry->prev = entry;
    entry->child = NULL;
    atomic_store_explicit(&entry->label, sylvan_false, memory_order_relaxed);
    return entry;
}

static void mtpndd_edge_entry_destroy(edge_bucket_entry_t *entry) {
    if (!entry) {
        return;
    }
    mtpndd_memory_release_edge_entry(entry);
}

static mtpndd_edge_t *mtpndd_edge_map_create_empty(void) {
    mtpndd_edge_t *edges = mtpndd_memory_acquire_edge_map();
    if (!edges) {
        return NULL;
    }
    memset(edges, 0, sizeof(mtpndd_edge_t));
    return edges;
}

static void mtpndd_edge_map_release_uninitialized(mtpndd_edge_t *edges) {
    if (!edges) {
        return;
    }
    mtpndd_memory_release_edge_map(edges);
}

size_t mtpndd_hash_node_identity(const mtpndd_node_t *node) {
    // TODO: like hashcode of map in java, we may need to hash mtpndd_edge_t edpending on its content instead of its address. Give the mtpndd_edge_t a structural hash value and keep updating it when the edge map is changed. And mtpndd_node_t hashcode will depend on its field_id and edge map hashcode.
    if (!node) return 0;

    const mtpndd_field_info_t *field = node->field;
    const mtpndd_edge_t *edges = node->edges;

    uint64_t hash = 1469598103934665603ULL; /* FNV offset basis */
    uint64_t field_id = (uint64_t)field->field_id;
    uintptr_t edges_addr = (uintptr_t)edges;
    // uintptr_t node_addr = (uintptr_t)node;

    hash ^= field_id;
    hash *= 1099511628211ULL;             /* FNV prime */
    hash ^= edges_addr;
    hash *= 1099511628211ULL;
    // hash ^= node_addr;
    // hash *= 1099511628211ULL;

    return (size_t)hash;
}

static inline mtpndd_error_t mtpndd_edge_map_init_local(mtpndd_edge_t *edges)
{
    EDGE_MAP_INIT(edges);
    return MTPNDD_SUCCESS;
}

static mtpndd_error_t mtpndd_edge_map_deep_clone(const mtpndd_edge_t *source, mtpndd_edge_t *dest)
{
    dest->edge_count = source->edge_count;
    dest->bucket_count = source->bucket_count;
    dest->buckets = NULL;
    dest->bucket_locks = NULL;

    size_t bucket_cnt = source->bucket_count ? source->bucket_count : mtpndd_config_edge_bucket_count();
    if (bucket_cnt == 0) {
        bucket_cnt = MTPNDD_DEFAULT_EDGE_BUCKET_COUNT;
    }
    dest->bucket_count = bucket_cnt;

    dest->buckets = (edge_bucket_entry_t **)malloc(sizeof(edge_bucket_entry_t *) * bucket_cnt);
    if (!dest->buckets) {
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    dest->bucket_locks = (atomic_flag *)malloc(sizeof(atomic_flag) * bucket_cnt);
    if (!dest->bucket_locks) {
        free(dest->buckets);
        dest->buckets = NULL;
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < bucket_cnt; ++i) {
        dest->buckets[i] = NULL;
        atomic_flag_clear_explicit(&dest->bucket_locks[i], memory_order_relaxed);
    }

    if (!source->buckets) {
        return MTPNDD_SUCCESS;
    }

    for (size_t i = 0; i < bucket_cnt; ++i) {
        edge_bucket_entry_t *src_head = source->buckets[i];
        if (!src_head) {
            continue;
        }

        edge_bucket_entry_t *dst_head = mtpndd_edge_entry_create_sentinel();
        if (!dst_head) {
            mtpndd_edge_map_reset(dest);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        dest->buckets[i] = dst_head;

        edge_bucket_entry_t *tail = dst_head;
        edge_bucket_entry_t *entry = src_head->next;
        while (entry && entry != src_head) {
            edge_bucket_entry_t *new_entry = mtpndd_edge_entry_create();
            if (!new_entry) {
            mtpndd_edge_map_reset(dest);
                return MTPNDD_ERROR_OUT_OF_MEMORY;
            }
            new_entry->child = entry->child;
            if (new_entry->child && !mtpndd_is_terminal(new_entry->child)) {
                mtpndd_error_t ref_status = mtpndd_ref(new_entry->child);
                if (ref_status != MTPNDD_SUCCESS) {
                    mtpndd_edge_entry_destroy(new_entry);
                    mtpndd_edge_map_reset(dest);
                    return ref_status;
                }
            }
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            atomic_store_explicit(&new_entry->label, sylvan_ref(label), memory_order_relaxed);

            new_entry->prev = tail;
            new_entry->next = dst_head;
            tail->next = new_entry;
            dst_head->prev = new_entry;
            tail = new_entry;

            entry = entry->next;
        }
    }

    return MTPNDD_SUCCESS;
}

static void mtpndd_edge_map_reset(mtpndd_edge_t *edges) {
    if (!edges) {
        return;
    }
    if (edges->buckets) {
        size_t bucket_cnt = edges->bucket_count;
        for (size_t i = 0; i < bucket_cnt; ++i) {
            edge_bucket_entry_t *head = edges->buckets[i];
            if (!head) {
                continue;
            }
            edge_bucket_entry_t *entry = head->next;
            while (entry && entry != head) {
                edge_bucket_entry_t *next = entry->next;
                mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
                sylvan_deref(label);
                mtpndd_edge_entry_destroy(entry);
                entry = next;
            }
            mtpndd_bdd_t head_label = atomic_load_explicit(&head->label, memory_order_relaxed);
            if (head_label != sylvan_false) {
                sylvan_deref(head_label);
            }
            mtpndd_edge_entry_destroy(head);
        }
        free(edges->buckets);
        edges->buckets = NULL;
    }
    if (edges->bucket_locks) {
        free(edges->bucket_locks);
        edges->bucket_locks = NULL;
    }
    edges->edge_count = 0;
    edges->bucket_count = 0;
}

void mtpndd_edge_map_free(mtpndd_edge_t *edges) {
    if (!edges) {
        return;
    }
    mtpndd_edge_map_reset(edges);
    mtpndd_memory_release_edge_map(edges);
}

/**
 * Find edge entry in hash table
 * Returns NULL if not found
 */
edge_bucket_entry_t *find_edge_entry(mtpndd_edge_t *edge, mtpndd_node_t *key) {
    if (edge == NULL || key == NULL || edge->buckets == NULL) {
        return NULL;
    }
    
    size_t hash = EDGE_MAP_HASH_VAL(edge, key);
    
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
    size_t hash = EDGE_MAP_HASH_VAL(edges, descendant);
    atomic_flag *bucket_lock = &edges->bucket_locks[hash];
#ifdef ENABLE_RECORDING
    struct timespec lock_wait_start = {0};
    bool lock_wait_started = false;
    uint64_t lock_spin_count = 0;
#endif
    while (atomic_flag_test_and_set_explicit(bucket_lock, memory_order_acquire)) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause");
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield");
#endif
#endif
#ifdef ENABLE_RECORDING
        if (!lock_wait_started) {
            clock_gettime(CLOCK_MONOTONIC, &lock_wait_start);
            lock_wait_started = true;
        }
        lock_spin_count++;
#endif
    }
#ifdef ENABLE_RECORDING
    if (lock_wait_started) {
        struct timespec lock_wait_end = {0};
        clock_gettime(CLOCK_MONOTONIC, &lock_wait_end);
        MTPNDD_STAT_ADD(edge_lock_spin_total, lock_spin_count);
        MTPNDD_STAT_ADD(edge_lock_wait_time_ns, mtpndd_timespec_diff_ns(&lock_wait_start, &lock_wait_end));
    }
#endif

    mtpndd_error_t status = MTPNDD_SUCCESS;
    edge_bucket_entry_t *entry = NULL;
    edge_bucket_entry_t *bucket_head = edges->buckets[hash];
#ifdef ENABLE_RECORDING
    bool created_entry = false;
    bool collision_on_insert = false;
#endif

    if (bucket_head == NULL) {
        // Empty bucket, cannot find existing entry
        bucket_head = mtpndd_edge_entry_create_sentinel();
        if (!bucket_head) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            status = MTPNDD_ERROR_OUT_OF_MEMORY;
            goto unlock_and_return;
        }
        edges->buckets[hash] = bucket_head;
        entry = mtpndd_edge_entry_create();
        if (!entry) {
            edges->buckets[hash] = NULL;
            mtpndd_edge_entry_destroy(bucket_head);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            status = MTPNDD_ERROR_OUT_OF_MEMORY;
            goto unlock_and_return;
        }
        entry->child = descendant;
#ifdef ENABLE_RECORDING
        created_entry = true;
#endif
    } else {
        // TODO: cononical ordering edge map entries by label, insert between entries with smaller and larger labels
        entry = find_edge_entry(edges, descendant);
        if (entry) {
            // Edge already exists, update old_label
            old_label = atomic_load_explicit(&entry->label, memory_order_acquire);
            // Remove entry from hash table
            entry->prev->next = entry->next;
            entry->next->prev = entry->prev;
            edges->edge_count--;
        } else {
            // Create new edge entry
            entry = mtpndd_edge_entry_create();
            if (!entry) {
                mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
                status = MTPNDD_ERROR_OUT_OF_MEMORY;
                goto unlock_and_return;
            }
            entry->child = descendant;
#ifdef ENABLE_RECORDING
            created_entry = true;
            collision_on_insert = true;
#endif
        }
    }

    // Update label
    mtpndd_bdd_t new_label = sylvan_ref(sylvan_or(old_label, label_bdd));
    atomic_store_explicit(&entry->label, new_label, memory_order_release);
    sylvan_deref(old_label);
    sylvan_deref(label_bdd);

    // Insert into bucket at the front
    entry->next = bucket_head->next;
    entry->prev = bucket_head;
    bucket_head->next->prev = entry;
    bucket_head->next = entry;

    edges->edge_count++;
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
    atomic_flag_clear_explicit(bucket_lock, memory_order_release);
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
    return ndd->field == &MTPNDD_TERMINAL_FIELD;
}

/********************************
 * MTPNDD operations (sub task)
 ********************************/
VOID_TASK_IMPL_3(mtpndd_and_rec_same_field_task,
        edge_bucket_entry_t *, entry_a,
        edge_bucket_entry_t *, entry_b,
        mtpndd_edge_t *, res_edges)
{
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t label_b = mtpndd_edge_label_load(entry_b);
    mtpndd_bdd_t combined_label = sylvan_ref(sylvan_and(label_a, label_b));
    if (combined_label != sylvan_false) {
        mtpndd_node_t *subResult = NULL;
        if (CALL(mtpndd_and_rec, entry_a->child, entry_b->child, &subResult) == MTPNDD_SUCCESS) {
            mtpndd_add_edge(res_edges, subResult, combined_label);
        }
    }
}

VOID_TASK_IMPL_3(mtpndd_and_rec_diff_field_task,
        edge_bucket_entry_t *, entry_a,
        mtpndd_t *, b,
        mtpndd_edge_t *, res_edges)
{
    mtpndd_node_t *subResult = NULL;
    if (CALL(mtpndd_and_rec, entry_a->child, b, &subResult) == MTPNDD_SUCCESS) {
        mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
        mtpndd_add_edge(res_edges, subResult, label_a);
    }
}

TASK_IMPL_3(mtpndd_error_t, mtpndd_and_rec, mtpndd_t *, a, mtpndd_t *, b, mtpndd_t **, result) {
    if (mtpndd_is_false(a) || mtpndd_is_true(b)) {
        *result = a;
        return MTPNDD_SUCCESS;
    } else if (mtpndd_is_false(b) || mtpndd_is_true(a) || a == b) {
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
    mtpndd_op_cache_t *and_cache = g_mtpndd_config.and_cache;
    mtpndd_node_t *cached = mtpndd_op_cache_lookup_binary(and_cache, cache_a, cache_b);
    if (cached) {
        *result = cached;
        return MTPNDD_SUCCESS;
    }

    size_t count=0;
    mtpndd_t *res_node = NULL;
    mtpndd_edge_t *res_edges = mtpndd_edge_map_create_empty();
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    if (mtpndd_edge_map_init_local(res_edges) != MTPNDD_SUCCESS) {
        mtpndd_edge_map_release_uninitialized(res_edges);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    if (a->field == b->field) {
        // Same field, combine edges
        edge_bucket_entry_t *entry_a;
        edge_bucket_entry_t *entry_b;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            FOR_EACH_ENTRY_IN_ALL_BUCKETS(b->edges, entry_b) {
                SPAWN(mtpndd_and_rec_same_field_task, entry_a, entry_b, res_edges);
                count++;
            }
        }
        while (count-- > 0) {
            SYNC(mtpndd_and_rec_same_field_task);
        }
    } else {
        // Different fields
        if (a->field->field_id > b->field->field_id) {
            mtpndd_t *temp = a;
            a = b;
            b = temp;
        }

        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            SPAWN(mtpndd_and_rec_diff_field_task, entry_a, b, res_edges);
            count++;
        }
        while (count-- > 0) {
            SYNC(mtpndd_and_rec_diff_field_task);
        }
    }

    if (mtpndd_mk(a->field->field_id, res_edges, &res_node) != MTPNDD_SUCCESS) {
        mtpndd_edge_map_free(res_edges);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    if (mtpndd_gc_protect_add(res_node) != MTPNDD_SUCCESS) {
        // Error during gc protect
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    mtpndd_op_cache_store_binary(and_cache, cache_a, cache_b, res_node);

    *result = res_node;
    return MTPNDD_SUCCESS;
}

static inline void mtpndd_residual_apply_mask(edge_bucket_entry_t *entry, mtpndd_bdd_t mask) {
    if (!entry) {
        return;
    }

    mtpndd_bdd_t expected = atomic_load_explicit(&entry->label, memory_order_acquire);

    while (expected != sylvan_false) {
        mtpndd_bdd_t updated = sylvan_ref(sylvan_and(expected, mask));
        if (atomic_compare_exchange_weak_explicit(
                &entry->label, &expected, updated,
                memory_order_acq_rel, memory_order_acquire)) {
            sylvan_deref(expected);
            return;
        }
        sylvan_deref(updated);
    }
}

static inline mtpndd_bdd_t mtpndd_edge_label_load(edge_bucket_entry_t *entry) {
    if (!entry) {
        return sylvan_false;
    }
    return atomic_load_explicit(&entry->label, memory_order_acquire);
}

VOID_TASK_IMPL_5(mtpndd_or_rec_same_field_task,
        edge_bucket_entry_t *, entry_a,
        edge_bucket_entry_t *, entry_b,
        mtpndd_edge_t *, res_edges,
        mtpndd_edge_t *, residualA,
        mtpndd_edge_t *, residualB)
{
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t label_b = mtpndd_edge_label_load(entry_b);
    mtpndd_bdd_t intersect = sylvan_ref(sylvan_and(label_a, label_b));
    if (intersect != sylvan_false) {
        // update residual
        mtpndd_bdd_t notIntersect = sylvan_ref(sylvan_not(intersect));
        edge_bucket_entry_t *residual_entry_a = find_edge_entry(residualA, entry_a->child);
        edge_bucket_entry_t *residual_entry_b = find_edge_entry(residualB, entry_b->child);
        mtpndd_residual_apply_mask(residual_entry_a, notIntersect);
        mtpndd_residual_apply_mask(residual_entry_b, notIntersect);
        sylvan_deref(notIntersect);
        // the descendant of the new edge
        mtpndd_node_t *subResult = NULL;
        if (CALL(mtpndd_or_rec, entry_a->child, entry_b->child, &subResult) == MTPNDD_SUCCESS) {
            mtpndd_add_edge(res_edges, subResult, intersect);
        }
    }
}

VOID_TASK_IMPL_4(mtpndd_or_rec_diff_field_task,
        edge_bucket_entry_t *, entry_a,
        mtpndd_t *, b,
        mtpndd_edge_t *, res_edges,
        _Atomic(mtpndd_bdd_t) *, residualB)
{
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t notIntersect = sylvan_ref(sylvan_not(label_a));
    mtpndd_bdd_t expected = atomic_load_explicit(residualB, memory_order_relaxed);
    for (;;) {
        mtpndd_bdd_t updated = sylvan_ref(sylvan_and(expected, notIntersect));
        // use cas to exchange residualB
        // update expected if cas fails and retry
        if (atomic_compare_exchange_weak_explicit(
                residualB, &expected, updated,
                memory_order_acq_rel, memory_order_acquire)) {
            sylvan_deref(expected);
            break;
        }
        sylvan_deref(updated);
    }
    sylvan_deref(notIntersect);
    mtpndd_node_t *subResult = NULL;
    if (CALL(mtpndd_or_rec, entry_a->child, b, &subResult) == MTPNDD_SUCCESS) {
        mtpndd_add_edge(res_edges, subResult, sylvan_ref(label_a));
    }
}

TASK_IMPL_3(mtpndd_error_t, mtpndd_or_rec, mtpndd_t *, a, mtpndd_t *, b, mtpndd_t **, result) {
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

    size_t count=0;
    mtpndd_t *res_node = NULL;
    mtpndd_edge_t *res_edges = mtpndd_edge_map_create_empty();
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    if (mtpndd_edge_map_init_local(res_edges) != MTPNDD_SUCCESS) {
        mtpndd_edge_map_release_uninitialized(res_edges);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    if (a->field == b->field) {
        // Same field, combine edges
        mtpndd_edge_t *residualA = mtpndd_edge_map_create_empty();
        if (!residualA) {
            mtpndd_edge_map_free(res_edges);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        if (mtpndd_edge_map_deep_clone(a->edges, residualA) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(residualA);
            mtpndd_edge_map_free(res_edges);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        mtpndd_edge_t *residualB = mtpndd_edge_map_create_empty();
        if (!residualB) {
            mtpndd_edge_map_free(residualA);
            mtpndd_edge_map_free(res_edges);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        if (mtpndd_edge_map_deep_clone(b->edges, residualB) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(residualA);
            mtpndd_edge_map_free(residualB);
            mtpndd_edge_map_free(res_edges);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        // already sylvan_ref when deep cloning

        edge_bucket_entry_t *entry_a;
        edge_bucket_entry_t *entry_b;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            FOR_EACH_ENTRY_IN_ALL_BUCKETS(b->edges, entry_b) {
                SPAWN(mtpndd_or_rec_same_field_task, entry_a, entry_b, res_edges, residualA, residualB);
                count++;
            }
        }
        while (count-- > 0) {
            SYNC(mtpndd_or_rec_same_field_task);
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
                mtpndd_add_edge(res_edges, entry_res->child, sylvan_ref(res_label));
            }
        }
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(residualB, entry_res) {
            mtpndd_bdd_t res_label = mtpndd_edge_label_load(entry_res);
            if (res_label != sylvan_false) {
                mtpndd_add_edge(res_edges, entry_res->child, sylvan_ref(res_label));
            }
        }
        mtpndd_edge_map_free(residualA);
        mtpndd_edge_map_free(residualB);
    } else {
        // Different fields
        if (a->field->field_id > b->field->field_id) {
            mtpndd_t *temp = a;
            a = b;
            b = temp;
        }

        _Atomic(mtpndd_bdd_t) residualB = sylvan_true;
        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            SPAWN(mtpndd_or_rec_diff_field_task, entry_a, b, res_edges, &residualB);
            count++;
        }
        while (count-- > 0) {
            SYNC(mtpndd_or_rec_diff_field_task);
        }
        mtpndd_bdd_t residualB_val = atomic_load_explicit(&residualB, memory_order_relaxed);
        if (residualB_val != sylvan_false) {
            mtpndd_add_edge(res_edges, b, residualB_val);
        }
    }

    mtpndd_mk(a->field->field_id, res_edges, &res_node);
    mtpndd_gc_protect_add(res_node);

    mtpndd_op_cache_store_binary(or_cache, cache_a, cache_b, res_node);

    *result = res_node;
    return MTPNDD_SUCCESS;
}

VOID_TASK_IMPL_3(mtpndd_not_rec_task,
        edge_bucket_entry_t *, entry_a,
        mtpndd_edge_t *, res_edges,
        _Atomic(mtpndd_bdd_t) *, residual)
{
    mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
    mtpndd_bdd_t notIntersect = sylvan_ref(sylvan_not(label_a));
    mtpndd_bdd_t expected = atomic_load_explicit(residual, memory_order_relaxed);
    for (;;) {
        mtpndd_bdd_t updated = sylvan_ref(sylvan_and(expected, notIntersect));
        // use cas to exchange residual
        // update expected if cas fails and retry
        if (atomic_compare_exchange_weak_explicit(
                residual, &expected, updated,
                memory_order_acq_rel, memory_order_acquire)) {
            sylvan_deref(expected);
            break;
        }
        sylvan_deref(updated);
    }
    sylvan_deref(notIntersect);
    mtpndd_node_t *subResult = NULL;
    if (CALL(mtpndd_not_rec, entry_a->child, &subResult) == MTPNDD_SUCCESS) {
        mtpndd_add_edge(res_edges, subResult, sylvan_ref(label_a));
    }
}

TASK_IMPL_2(mtpndd_error_t, mtpndd_not_rec, mtpndd_t *, a, mtpndd_t **, result) {
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

    mtpndd_edge_t *res_edges = mtpndd_edge_map_create_empty();
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    if (mtpndd_edge_map_init_local(res_edges) != MTPNDD_SUCCESS) {
        mtpndd_edge_map_release_uninitialized(res_edges);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    size_t count=0;
    _Atomic(mtpndd_bdd_t) residual = sylvan_true;
    mtpndd_t *res_node = NULL;
    edge_bucket_entry_t *entry_a;
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
        SPAWN(mtpndd_not_rec_task, entry_a, res_edges, &residual);
        count++;
    }
    while (count-- > 0) {
        SYNC(mtpndd_not_rec_task);
    }
    mtpndd_bdd_t residual_val = atomic_load_explicit(&residual, memory_order_relaxed);
    if (residual_val != sylvan_false) {
        mtpndd_add_edge(res_edges, &MTPNDD_TRUE, residual_val);
    }
    if (mtpndd_mk(a->field->field_id, res_edges, &res_node) != MTPNDD_SUCCESS) {
        mtpndd_edge_map_free(res_edges);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    if (mtpndd_gc_protect_add(res_node) != MTPNDD_SUCCESS) {
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    mtpndd_op_cache_store_unary(not_cache, a, res_node);

    *result = res_node;
    return MTPNDD_SUCCESS;
}

// diff = a AND (NOT b)
VOID_TASK_IMPL_3(mtpndd_exist_rec_task, edge_bucket_entry_t *, entry_a, mtpndd_edge_t *, res_edges, uint32_t, field) {
    mtpndd_t *subResult = NULL;
    if (CALL(mtpndd_exist_rec, entry_a->child, field, &subResult) == MTPNDD_SUCCESS) {
        mtpndd_bdd_t label_a = mtpndd_edge_label_load(entry_a);
        mtpndd_add_edge(res_edges, subResult, sylvan_ref(label_a));
    }
}

TASK_IMPL_3(mtpndd_error_t, mtpndd_exist_rec, mtpndd_t *, a, uint32_t, field, mtpndd_t **, result) {
    if (mtpndd_is_terminal(a)) {
        *result = a;
        return MTPNDD_SUCCESS;
    }

    mtpndd_t *res_node = &MTPNDD_FALSE;
    if (a->field->field_id == field) {
        edge_bucket_entry_t *entry_a = NULL;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            CALL(mtpndd_or_rec, res_node, entry_a->child, &res_node);
        }
    } else {
        // Keep this field
        mtpndd_edge_t *res_edges = mtpndd_edge_map_create_empty();
        if (!res_edges) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        if (mtpndd_edge_map_init_local(res_edges) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_release_uninitialized(res_edges);
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }

        size_t count = 0;
        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            SPAWN(mtpndd_exist_rec_task, entry_a, res_edges, field);
            count++;
        }
        while (count-- > 0) {
            SYNC(mtpndd_exist_rec_task);
        }
        if (mtpndd_mk(a->field->field_id, res_edges, &res_node) != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(res_edges);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
    }
    mtpndd_gc_protect_add(res_node);
    *result = res_node;
    return MTPNDD_SUCCESS;
}

/********************************
 * MTPNDD operations (wrappers)
 ********************************/
mtpndd_t *mtpndd_and(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (RUN(mtpndd_and_rec, a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_t *mtpndd_or(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (RUN(mtpndd_or_rec, a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_t *mtpndd_not(mtpndd_t *a) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (RUN(mtpndd_not_rec, a, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_t *mtpndd_diff(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    mtpndd_t *not_b = NULL;
    if (RUN(mtpndd_not_rec, b, &not_b) != MTPNDD_SUCCESS) {
        return NULL;
    }
    if (RUN(mtpndd_and_rec, a, not_b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_t *mtpndd_exist(mtpndd_t *a, uint32_t field) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (RUN(mtpndd_exist_rec, a, field, &result) != MTPNDD_SUCCESS) {
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

static inline size_t mtpndd_hash_ptr(const void *ptr) {
    uintptr_t value = (uintptr_t)ptr;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return (size_t)value;
}

static inline size_t mtpndd_hash_u64(uint64_t key) {
    uint64_t value = key;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return (size_t)value;
}

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
    size_t idx = mtpndd_hash_ptr(key) & mask;
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
    size_t idx = mtpndd_hash_ptr(key) & mask;
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

static mtpndd_field_info_t *mtpndd_find_field_by_var(uint32_t var) {
    for (uint32_t i = 1; i <= g_mtpndd_config.field_count; ++i) {
        mtpndd_field_info_t *field = g_mtpndd_config.field_info[i];
        if (!field) {
            continue;
        }
        uint32_t start = field->start_var;
        uint32_t end = start + field->bit_width;
        if (var >= start && var < end) {
            return field;
        }
    }
    return NULL;
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
    size_t bucket_cnt = (node->edges != NULL) ? node->edges->bucket_count : 0;
    for (size_t bucket = 0; bucket < bucket_cnt; ++bucket) {
        edge_bucket_entry_t *head = node->edges->buckets[bucket];
        if (!head) {
            continue;
        }
        edge_bucket_entry_t *entry = head->next;
        while (entry && entry != head) {
            if (entry->child) {
                mtpndd_bdd_t label = mtpndd_edge_label_load(entry);
                if (label != sylvan_false) {
                    mtpndd_bdd_t child_bdd = sylvan_false;
                    status = mtpndd_to_mtbdd_rec(entry->child, cache, &child_bdd);
                    if (status != MTPNDD_SUCCESS) {
                        break;
                    }
                    mtpndd_bdd_t conjunct = sylvan_ref(sylvan_and(label, child_bdd));
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
        __atomic_store_n(&g_mtpndd_stats.bdd_nodes_converted, nodecount, __ATOMIC_RELAXED);
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
        uint32_t offset,
        mtpndd_bdd_t cube,
        mtbdd_to_mtpndd_cache_t *cache,
        mtpndd_edge_t *edges)
{
    if (current == sylvan_false) {
        return MTPNDD_SUCCESS;
    }

    uint32_t field_end = field->start_var + field->bit_width;
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

    uint32_t var = field->start_var + offset;
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
    status = mtbdd_to_mtpndd_collect(low_child, field, offset + 1, cube_low, cache, edges);
    sylvan_deref(cube_low);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    mtpndd_bdd_t cube_high = sylvan_ref(sylvan_and(cube, literal_pos));
    status = mtbdd_to_mtpndd_collect(high_child, field, offset + 1, cube_high, cache, edges);
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
    mtpndd_field_info_t *field = mtpndd_find_field_by_var(var);
    if (!field) {
        mtpndd_set_error(MTPNDD_ERROR_INVALID_FIELD, __func__, __LINE__);
        return MTPNDD_ERROR_INVALID_FIELD;
    }

    mtpndd_edge_t *edges = mtpndd_edge_map_create_empty();
    if (!edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    if (mtpndd_edge_map_init_local(edges) != MTPNDD_SUCCESS) {
        mtpndd_edge_map_release_uninitialized(edges);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    mtpndd_error_t status = mtbdd_to_mtpndd_collect(bdd, field, 0, sylvan_true, cache, edges);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_edge_map_free(edges);
        return status;
    }

    mtpndd_t *node = NULL;
    status = mtpndd_mk(field->field_id, edges, &node);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_edge_map_free(edges);
        return status;
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
    __atomic_store_n(&g_mtpndd_stats.bdd_nodes_converted, bdd_nodecount, __ATOMIC_RELAXED);
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
        mtpndd_field_info_t *field = g_mtpndd_config.field_info[i];
        if (!field) {
            continue;
        }
        size_t end = (size_t)field->start_var + field->bit_width;
        if (end > total) {
            total = end;
        }
    }
    return total;
}

double mtpndd_satcount(mtpndd_t *node) {
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

    const mtpndd_field_info_t *field = node->field;
    size_t edge_count = (node->edges != NULL) ? node->edges->edge_count : 0;
    uint32_t field_id = field ? field->field_id : 0;
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
        mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
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
