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

mtpndd_error_t mtpndd_add_edge(mtpndd_edge_t *edges, mtpndd_t *descendant, mtpndd_bdd_t label_bdd) {
    if (mtpndd_is_false(descendant)) {
        sylvan_deref(label_bdd);
        return MTPNDD_SUCCESS;
    }

    mtpndd_bdd_t old_label = sylvan_false;
    size_t hash = EDGE_MAP_HASH_VAL(descendant);
    edge_bucket_entry_t *entry = NULL;
    edge_bucket_entry_t *bucket_head = edges->buckets[hash];

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
        edges->buckets[hash] = bucket_head;
        entry = (edge_bucket_entry_t *)malloc(sizeof(edge_bucket_entry_t));
        if (!entry) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        entry->child = descendant;
    } else {
        entry = find_edge_entry(edges, descendant);
        if (entry) {
            // Edge already exists, update old_label
            old_label = entry->label;
            // Remove entry from hash table
            entry->prev->next = entry->next;
            entry->next->prev = entry->prev;
            edges->edge_count--;
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

    edges->edge_count++;
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
 * MTPNDD operations
 ********************************/
// SPAWN();
// CALL();
// SYNC();

mtpndd_t *mtpndd_and(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (mtpndd_and_rec(a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_error_t mtpndd_and_rec(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result) {
    if (mtpndd_is_false(a) || mtpndd_is_true(b)) {
        *result = a;
        return MTPNDD_SUCCESS;
    } else if (mtpndd_is_false(b) || mtpndd_is_true(a) || a == b) {
        *result = b;
        return MTPNDD_SUCCESS;
    }

    // TODO: and cache

    mtpndd_t *res_node = NULL;
    mtpndd_edge_t *res_edges = (mtpndd_edge_t *)malloc(sizeof(mtpndd_edge_t));
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    EDGE_MAP_INIT(res_edges);
    if (a->field == b->field) {
        // Same field, combine edges
        edge_bucket_entry_t *entry_a;
        edge_bucket_entry_t *entry_b;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            FOR_EACH_ENTRY_IN_ALL_BUCKETS(b->edges, entry_b) {
                mtpndd_bdd_t combined_label = sylvan_ref(sylvan_and(entry_a->label, entry_b->label));
                if (combined_label != sylvan_false) {
                    mtpndd_node_t *subResult = NULL;
                    if (mtpndd_and_rec(entry_a->child, entry_b->child, &subResult) == MTPNDD_SUCCESS) {
                        mtpndd_add_edge(res_edges, subResult, combined_label);
                    }
                }
            }
        }
    } else {
        // Different fields
        if (a->field->level > b->field->level) {
            mtpndd_t *temp = a;
            a = b;
            b = temp;
        }

        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            mtpndd_node_t *subResult = NULL;
            if (mtpndd_and_rec(entry_a->child, b, &subResult) == MTPNDD_SUCCESS) {
                mtpndd_add_edge(res_edges, subResult, entry_a->label);
            }
        }
    }

    if (mtpndd_mk(a->field->id, res_edges, &res_node) != MTPNDD_SUCCESS) {
        // Error during node creation
        // Free res_edges
        edge_bucket_entry_t *entry;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(res_edges, entry) {
            sylvan_deref(entry->label);
            free(entry);
        }
        free(res_edges->buckets);
        free(res_edges);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    if (mtpndd_gc_protect_add(res_node) != MTPNDD_SUCCESS) {
        // Error during gc protect
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    // TODO: add and cache

    *result = res_node;
    return MTPNDD_SUCCESS;
}

mtpndd_t *mtpndd_or(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (mtpndd_or_rec(a, b, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_error_t mtpndd_or_rec(mtpndd_t *a, mtpndd_t *b, mtpndd_t **result) {
    if (mtpndd_is_true(a) || mtpndd_is_false(b)) {
        *result = a;
        return MTPNDD_SUCCESS;
    } else if (mtpndd_is_true(b) || mtpndd_is_false(a) || a == b) {
        *result = b;
        return MTPNDD_SUCCESS;
    }

    // TODO: or cache

    mtpndd_t *res_node = NULL;
    mtpndd_edge_t *res_edges = (mtpndd_edge_t *)malloc(sizeof(mtpndd_edge_t));
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    EDGE_MAP_INIT(res_edges);

    if (a->field == b->field) {
        // Same field, combine edges
        mtpndd_edge_t *residualA = (mtpndd_edge_t *)malloc(sizeof(mtpndd_edge_t));
        if (!residualA) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        EDGE_MAP_DEEP_CLONE(a->edges, residualA);
        mtpndd_edge_t *residualB = (mtpndd_edge_t *)malloc(sizeof(mtpndd_edge_t));
        if (!residualB) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        EDGE_MAP_DEEP_CLONE(b->edges, residualB);
        // already sylvan_ref when EDGE_MAP_DEEP_CLONE

        edge_bucket_entry_t *entry_a;
        edge_bucket_entry_t *entry_b;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            FOR_EACH_ENTRY_IN_ALL_BUCKETS(b->edges, entry_b) {
                long intersect = sylvan_ref(sylvan_and(entry_a->label, entry_b->label));
                if (intersect != sylvan_false) {
                    // update residual
                    long notIntersect = sylvan_ref(sylvan_not(intersect));
                    long oldResidual = find_edge_entry(residualA, entry_a->child)->label;
                    mtpndd_add_edge(residualA, entry_a->child, sylvan_ref(sylvan_and(oldResidual, notIntersect)));
                    sylvan_deref(oldResidual);
                    oldResidual = find_edge_entry(residualB, entry_b->child)->label;
                    mtpndd_add_edge(residualB, entry_b->child, sylvan_ref(sylvan_and(oldResidual, notIntersect)));
                    sylvan_deref(oldResidual);
                    sylvan_deref(notIntersect);
                    // the descendant of the new edge
                    mtpndd_node_t *subResult = NULL;
                    if (mtpndd_or_rec(entry_a->child, entry_b->child, &subResult) == MTPNDD_SUCCESS) {
                        mtpndd_add_edge(res_edges, subResult, intersect);
                    }
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
            if (entry_res->label != sylvan_false) {
                mtpndd_add_edge(res_edges, entry_res->child, entry_res->label);
            }
        }
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(residualB, entry_res) {
            if (entry_res->label != sylvan_false) {
                mtpndd_add_edge(res_edges, entry_res->child, entry_res->label);
            }
        }
    } else {
        // Different fields
        if (a->field->level > b->field->level) {
            mtpndd_t *temp = a;
            a = b;
            b = temp;
        }

        long residualB = sylvan_true;
        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            long notIntersect = sylvan_ref(sylvan_not(entry_a->label));
            long temp = residualB;
            residualB = sylvan_ref(sylvan_and(residualB, notIntersect));
            sylvan_deref(temp);
            sylvan_deref(notIntersect);
            mtpndd_node_t *subResult = NULL;
            if (mtpndd_or_rec(entry_a->child, b, &subResult) == MTPNDD_SUCCESS) {
                mtpndd_add_edge(res_edges, subResult, sylvan_ref(entry_a->label));
            }
        }
        if (residualB != sylvan_false) {
            mtpndd_add_edge(res_edges, b, residualB);
        }
    }

    mtpndd_mk(a->field->id, res_edges, &res_node);
    mtpndd_gc_protect_add(res_node);

    // TODO: add or cache

    *result = res_node;
    return MTPNDD_SUCCESS;
}

mtpndd_t *mtpndd_not(mtpndd_t *a) {
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (mtpndd_not_rec(a, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_error_t mtpndd_not_rec(mtpndd_t *a, mtpndd_t **result) {
    if (mtpndd_is_true(a)) {
        *result = &MTPNDD_FALSE;
        return MTPNDD_SUCCESS;
    } else if (mtpndd_is_false(a)) {
        *result = &MTPNDD_TRUE;
        return MTPNDD_SUCCESS;
    }

    // TODO: not cache

    mtpndd_edge_t *res_edges = (mtpndd_edge_t *)malloc(sizeof(mtpndd_edge_t));
    if (!res_edges) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    EDGE_MAP_INIT(res_edges);

    long residual = sylvan_true;
    mtpndd_t *res_node = NULL;
    edge_bucket_entry_t *entry_a;
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
        long notIntersect = sylvan_ref(sylvan_not(entry_a->label));
        long temp = residual;
        residual = sylvan_ref(sylvan_and(residual, notIntersect));
        sylvan_deref(temp);
        sylvan_deref(notIntersect);
        mtpndd_node_t *subResult = NULL;
        if (mtpndd_not_rec(entry_a->child, &subResult) == MTPNDD_SUCCESS) {
            mtpndd_add_edge(res_edges, subResult, sylvan_ref(entry_a->label));
        }
    }
    if (residual != sylvan_false) {
        mtpndd_add_edge(res_edges, &MTPNDD_TRUE, residual);
    }
    mtpndd_mk(a->field->id, res_edges, &res_node);
    mtpndd_gc_protect_add(res_node);

    // TODO: add not cache

    *result = res_node;
    return MTPNDD_SUCCESS;
}

// diff = a AND (NOT b)
mtpndd_t *mtpndd_diff(mtpndd_t *a, mtpndd_t *b) {
    mtpndd_gc_protect_clear();
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
    mtpndd_gc_protect_clear();
    mtpndd_t *result = NULL;
    if (mtpndd_exist_rec(a, field, &result) != MTPNDD_SUCCESS) {
        return NULL;
    }
    return result;
}

mtpndd_error_t mtpndd_exist_rec(mtpndd_t *a, uint32_t field, mtpndd_t **result) {
    if (mtpndd_is_terminal(a)) {
        *result = a;
        return MTPNDD_SUCCESS;
    }

    mtpndd_t *res_node = &MTPNDD_FALSE;
    if (a->field->id == field) {
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            mtpndd_or_rec(res_node, entry_a->child, &res_node);
        }
    } else {
        // Keep this field
        mtpndd_edge_t *res_edges = (mtpndd_edge_t *)malloc(sizeof(mtpndd_edge_t));
        if (!res_edges) {
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
            return MTPNDD_ERROR_OUT_OF_MEMORY;
        }
        EDGE_MAP_INIT(res_edges);

        edge_bucket_entry_t *entry_a;
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(a->edges, entry_a) {
            mtpndd_t *subResult = NULL;
            if (mtpndd_exist_rec(entry_a->child, field, &subResult) == MTPNDD_SUCCESS) {
                mtpndd_add_edge(res_edges, subResult, sylvan_ref(entry_a->label));
            }
        }
        mtpndd_mk(a->field->id, res_edges, &res_node);
    }
    mtpndd_gc_protect_add(res_node);
    *result = res_node;
    return MTPNDD_SUCCESS;
}