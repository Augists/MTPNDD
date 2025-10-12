// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include <stdatomic.h>
#include "mtpndd_nodetable.h"
#include "mtpndd_node.h"
#include "sylvan.h"

mtpndd_nodetable_t *mtpndd_nodetable_declare_field() {
    mtpndd_nodetable_t *table = (mtpndd_nodetable_t *)malloc(sizeof(mtpndd_nodetable_t));
    if (!table) return NULL;
    memset(table, 0, sizeof(mtpndd_nodetable_t));
    table->nodetable_bucket_count = NODETABLE_BUCKET_CNT;

    table->buckets = 
            (mtpndd_nodetable_bucket_entry_t **)malloc(
                NODETABLE_BUCKET_CNT * sizeof(mtpndd_nodetable_bucket_entry_t *));
    if (!table->buckets) {
        free(table);
        return NULL;
    }
    for (size_t i = 0; i < NODETABLE_BUCKET_CNT; i++)
        table->buckets[i] = NULL;

    return table;
}

mtpndd_node_t *find_node_in_nodetable(mtpndd_nodetable_t *nodetable, mtpndd_edge_t *edges) {
    size_t hash = NODETABLE_HASH_VAL(edges, nodetable);
    mtpndd_nodetable_bucket_entry_t *entry;
    FOR_EACH_ENTRY_IN_NODETABLE_BUCKET(nodetable, hash, entry) {
        if (NODETABLE_BUCKET_ENTRY_EQUAL(entry, edges)) {
            return entry->node;
        }
    }
    return NULL;
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
    node->ref_count = UINT64_MAX;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_unprotect(mtpndd_t *node) {
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_NULL_POINTER, __func__, __LINE__);
        return MTPNDD_ERROR_NULL_POINTER;
    }
    node->ref_count = 0;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_mk(uint32_t field, mtpndd_edge_t *edges, mtpndd_node_t **result) {
    if (edges->edge_count == 0) {
        *result = &MTPNDD_FALSE;
        return MTPNDD_SUCCESS;
    } else if (edges->edge_count == 1) {
        edge_bucket_entry_t *only_entry = NULL;
        //
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, only_entry) {
            if (only_entry) break;
        }
        if (only_entry->label == sylvan_true) {
            *result = only_entry->child;
            return MTPNDD_SUCCESS;
        }
    }
    mtpndd_nodetable_t *nodetable = g_mtpndd_config.node_tables_by_field[field];
    mtpndd_node_t *node = find_node_in_nodetable(nodetable, edges);
    if (node) {
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            sylvan_deref(entry->label);
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
    node->field = &g_mtpndd_config.field_info[field];
    node->edges = edges;
    node->ref_count = 0;
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
    new_entry->next = nodetable->buckets[hash];
    new_entry->prev = NULL;
    if (nodetable->buckets[hash]) {
        nodetable->buckets[hash]->prev = new_entry;
    }
    nodetable->buckets[hash] = new_entry;
    g_mtpndd_stats.node_count++;
    *result = node;
    return MTPNDD_SUCCESS;
}

void gcOrGrow() {
    gc();
    if (g_mtpndd_pal_config.mtpndd_nodetable_size - g_mtpndd_stats.node_count
            < g_mtpndd_pal_config.quick_growth_threshold * g_mtpndd_pal_config.mtpndd_nodetable_size) {
        grow();
    }
    // TODO: clear op cache
}

void gc() {
    // protect temporary nodes during NDD operations
    // TODO
}

void grow() {
    g_mtpndd_pal_config.mtpndd_nodetable_size *= 2;
}