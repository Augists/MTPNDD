// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_common.h"
#include "mtpndd_node.h"
#include "mtpndd_nodetable.h"
#include "mtpndd_operation_cache.h"
#include "sylvan.h"

/**
 * Find edge entry in hash table
 * Returns NULL if not found
 */
edge_bucket_entry_t *find_edge_entry(mtpndd_edge_t *edge, mtpndd_node_t *key) {
    if (edge == NULL || key == NULL || edge->buckets == NULL) {
        return NULL;
    }
    
    size_t hash = EDGE_MAP_HASH_VAL(key);
    
    edge_bucket_entry_t *entry;
    FOR_EACH_ENTRY_IN_BUCKET(edge, hash, entry) {
        if (EDGE_BUCKET_ENTRY_EQUAL(entry, key)) {
            return entry;
        }
    }
    
    return NULL;
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
 * MTPNDD new node
 ********************************/
mtpndd_error_t mtpndd_add_edge(mtpndd_t *ndd, mtpndd_t *descendant, mtpndd_bdd_t label_bdd) {
    if (mtpndd_is_false(descendant)) {
        sylvan_deref(label_bdd);
        return MTPNDD_SUCCESS;
    }

    mtpndd_bdd_t old_label = sylvan_false;
    size_t hash = EDGE_MAP_HASH_VAL(descendant);
    edge_bucket_entry_t *entry = NULL;
    edge_bucket_entry_t *bucket_head = ndd->edges->buckets[hash];

    if (bucket_head == NULL) {
        // Empty bucket, cannot find existing entry
        bucket_head = (edge_bucket_entry_t *)malloc(sizeof(edge_bucket_entry_t));
        if (!bucket_head) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        bucket_head->next = bucket_head;
        bucket_head->prev = bucket_head;
        bucket_head->child = NULL;
        bucket_head->label = sylvan_false;
        ndd->edges->buckets[hash] = bucket_head;
        entry = (edge_bucket_entry_t *)malloc(sizeof(edge_bucket_entry_t));
        if (!entry) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        entry->child = descendant;
    } else {
        entry = find_edge_entry(ndd->edges, descendant);
        if (entry) {
            // Edge already exists, update old_label
            old_label = entry->label;
            // Remove entry from hash table
            entry->prev->next = entry->next;
            entry->next->prev = entry->prev;
            ndd->edges->edge_count--;
        } else {
            // Create new edge entry
            entry = (edge_bucket_entry_t *)malloc(sizeof(edge_bucket_entry_t));
            if (!entry) {
                mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
                return MTPNDD_ERROR_OUT_OF_MEMORY;
            }
            entry->child = descendant;
        }
    }

    // Update label
    entry->label = sylvan_ref(sylvan_or(old_label, label_bdd));
    sylvan_deref(old_label);
    sylvan_deref(label_bdd);

    // Insert into bucket at the front
    entry->next = bucket_head->next;
    entry->prev = bucket_head;
    bucket_head->next->prev = entry;
    bucket_head->next = entry;

    ndd->edges->edge_count++;
    return MTPNDD_SUCCESS;
}

/********************************
 * MTPNDD operations
 ********************************/
// SPAWN();
// CALL();
// SYNC();