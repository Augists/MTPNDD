// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "mtpndd_nodetable.h"
#include "mtpndd_node.h"
#include "sylvan.h"

static void gc(void);
static void grow(void);

static void gcOrGrow(void);

mtpndd_nodetable_t *mtpndd_nodetable_declare_field() {
    mtpndd_nodetable_t *table = (mtpndd_nodetable_t *)malloc(sizeof(mtpndd_nodetable_t));
    if (!table) return NULL;
    memset(table, 0, sizeof(mtpndd_nodetable_t));
    size_t bucket_cnt = mtpndd_config_nodetable_bucket_count();
    if (bucket_cnt == 0) {
        bucket_cnt = MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
    }
    table->nodetable_bucket_count = bucket_cnt;

    table->buckets =
            (mtpndd_nodetable_bucket_entry_t **)malloc(
                bucket_cnt * sizeof(mtpndd_nodetable_bucket_entry_t *));
    if (!table->buckets) {
        free(table);
        return NULL;
    }
    table->bucket_locks = (pthread_rwlock_t *)malloc(bucket_cnt * sizeof(pthread_rwlock_t));
    if (!table->bucket_locks) {
        free(table->buckets);
        free(table);
        return NULL;
    }

    for (size_t i = 0; i < bucket_cnt; i++) {
        table->buckets[i] = NULL;
        if (pthread_rwlock_init(&table->bucket_locks[i], NULL) != 0) {
            for (size_t j = 0; j < i; j++) {
                pthread_rwlock_destroy(&table->bucket_locks[j]);
            }
            free(table->bucket_locks);
            free(table->buckets);
            free(table);
            return NULL;
        }
    }

    return table;
}

mtpndd_node_t *find_node_in_nodetable(mtpndd_nodetable_t *nodetable, mtpndd_edge_t *edges) {
    size_t hash = NODETABLE_HASH_VAL(edges, nodetable);
    pthread_rwlock_t *bucket_lock = &nodetable->bucket_locks[hash];
    mtpndd_node_t *found = NULL;

    if (pthread_rwlock_rdlock(bucket_lock) != 0) {
        mtpndd_set_error(MTPNDD_ERROR_THREAD_SAFETY, __func__, __LINE__);
        return NULL;
    }

    for (mtpndd_nodetable_bucket_entry_t *entry = nodetable->buckets[hash];
         entry; entry = entry->next) {
        if (NODETABLE_BUCKET_ENTRY_EQUAL(entry, edges)) {
            found = entry->node;
            break;
        }
    }

    pthread_rwlock_unlock(bucket_lock);
    return found;
}

/********************************
 * MTPNDD node
 ********************************/
mtpndd_error_t mtpndd_ref(mtpndd_t *node) {
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_NULL_POINTER, __func__, __LINE__);
        return MTPNDD_ERROR_NULL_POINTER;
    }
    if (node->ref_count == UINT64_MAX) {
        // Terminal nodes
        return MTPNDD_SUCCESS;
    }
    atomic_fetch_add(&node->ref_count, 1);
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_deref(mtpndd_t *node) {
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_NULL_POINTER, __func__, __LINE__);
        return MTPNDD_ERROR_NULL_POINTER;
    }
    if (node->ref_count == UINT64_MAX) {
        // Terminal nodes
        return MTPNDD_SUCCESS;
    }
    atomic_fetch_sub(&node->ref_count, 1);
    /**
     * lazy free when gc
     */
    // uint64_t new_ref_count = __atomic_fetch_sub(&node->ref_count, 1, __ATOMIC_SEQ_CST) - 1;
    // if (new_ref_count == 0) {
    //     // Free edges
    //     if (node->edges) {
    //         FOR_EACH_ENTRY_IN_ALL_BUCKETS(node->edges, entry) {
    //             mtpndd_deref(entry->child);
    //             free(entry);
    //         }
    //         free(node->edges->buckets);
    //         free(node->edges);
    //     }
    //     // Free node
    //     free(node);
    // }
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_protect(mtpndd_t *node) {
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_NULL_POINTER, __func__, __LINE__);
        return MTPNDD_ERROR_NULL_POINTER;
    }
    atomic_init(&node->ref_count, UINT64_MAX);
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_unprotect(mtpndd_t *node) {
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_NULL_POINTER, __func__, __LINE__);
        return MTPNDD_ERROR_NULL_POINTER;
    }
    atomic_init(&node->ref_count, 0);
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_mk(uint32_t field, mtpndd_edge_t *edges, mtpndd_node_t **result) {
    if (edges->edge_count == 0) {
        *result = &MTPNDD_FALSE;
        return MTPNDD_SUCCESS;
    } else if (edges->edge_count == 1) {
        edge_bucket_entry_t *only_entry = NULL;
        size_t bucket_cnt = edges->bucket_count;
        for (size_t i = 0; i < bucket_cnt && !only_entry; ++i) {
            edge_bucket_entry_t *head = edges->buckets[i];
            if (!head) continue;
            edge_bucket_entry_t *walker = head->next;
            if (walker && walker != head) {
                only_entry = walker;
            }
        }
        if (only_entry && atomic_load_explicit(&only_entry->label, memory_order_acquire) == sylvan_true) {
            *result = only_entry->child;
            return MTPNDD_SUCCESS;
        }
    }
    mtpndd_nodetable_t *nodetable = g_mtpndd_config.node_tables_by_field[field];
    mtpndd_node_t *node = find_node_in_nodetable(nodetable, edges);
    edge_bucket_entry_t *entry = NULL;
    if (node) {
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            sylvan_deref(label);
        }
        *result = node;
        return MTPNDD_SUCCESS;
    }
    // Create new node
    // 1. add ref count of all children
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
        if (!mtpndd_is_terminal(entry->child)) {
            mtpndd_ref(entry->child);
        }
    }
    // 2. check if there should be a gc or grow
    if (g_mtpndd_stats.node_count > g_mtpndd_pal_config.mtpndd_nodetable_size) {
        gcOrGrow();
    }
    // 3. create new node
    node = (mtpndd_node_t *)malloc(sizeof(mtpndd_node_t));
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    node->field = g_mtpndd_config.field_info[field];
    node->edges = edges;
    atomic_init(&node->ref_count, 0);
    // 4. insert into nodetable
    size_t hash = NODETABLE_HASH_VAL(edges, nodetable);
    mtpndd_nodetable_bucket_entry_t *new_entry = (mtpndd_nodetable_bucket_entry_t *)malloc(sizeof(mtpndd_nodetable_bucket_entry_t));
    if (!new_entry) {
        free(node);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    new_entry->edges = edges;
    new_entry->node = node;
    pthread_rwlock_t *bucket_lock = &nodetable->bucket_locks[hash];
    if (pthread_rwlock_wrlock(bucket_lock) != 0) {
        free(new_entry);
        free(node);
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            if (!mtpndd_is_terminal(entry->child)) {
                mtpndd_deref(entry->child);
            }
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            sylvan_deref(label);
        }
        mtpndd_set_error(MTPNDD_ERROR_THREAD_SAFETY, __func__, __LINE__);
        return MTPNDD_ERROR_THREAD_SAFETY;
    }

    mtpndd_nodetable_bucket_entry_t *existing_entry = nodetable->buckets[hash];
    while (existing_entry) {
        if (NODETABLE_BUCKET_ENTRY_EQUAL(existing_entry, edges)) {
            break;
        }
        existing_entry = existing_entry->next;
    }

    if (existing_entry) {
        mtpndd_node_t *existing_node = existing_entry->node;
        pthread_rwlock_unlock(bucket_lock);
        free(new_entry);
        free(node);
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            if (!mtpndd_is_terminal(entry->child)) {
                mtpndd_deref(entry->child);
            }
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            sylvan_deref(label);
        }
        *result = existing_node;
        return MTPNDD_SUCCESS;
    }

    new_entry->next = nodetable->buckets[hash];
    new_entry->prev = NULL;
    if (nodetable->buckets[hash]) {
        nodetable->buckets[hash]->prev = new_entry;
    }
    nodetable->buckets[hash] = new_entry;
    pthread_rwlock_unlock(bucket_lock);
    g_mtpndd_stats.node_count++;
    *result = node;
    return MTPNDD_SUCCESS;
}

static void gcOrGrow(void) {
    gc();
    if (g_mtpndd_pal_config.mtpndd_nodetable_size - g_mtpndd_stats.node_count
            < g_mtpndd_pal_config.quick_growth_threshold * g_mtpndd_pal_config.mtpndd_nodetable_size) {
        grow();
    }
    // TODO: clear op cache
}

static void gc(void) {
    // protect temporary nodes during NDD operations
    // TODO: stop the world and stop lace and gc

    // TODO: gc pre hook and post hook like sylvan
}

static void grow(void) {
    g_mtpndd_pal_config.mtpndd_nodetable_size *= 2;
}
