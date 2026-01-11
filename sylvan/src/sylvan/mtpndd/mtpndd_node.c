// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_node.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lace.h>
#ifdef ENABLE_RECORDING
#include <time.h>
#endif

#include "mtpndd_edge_builder.h"
#include "mtpndd_nodetable.h"
#include "mtpndd_operation_cache.h"

#include "sylvan.h"
#include "sylvan_mtbdd.h"

/********************************
 * Temp refs - lightweight protection for intermediate results
 ********************************/
typedef struct {
    mtpndd_t *items;
    size_t count;
    size_t capacity;
} mtpndd_temp_ref_list_t;

static inline void mtpndd_temp_refs_init(mtpndd_temp_ref_list_t *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static inline bool mtpndd_temp_refs_push(mtpndd_temp_ref_list_t *list, mtpndd_t node) {
    if (!list) return false;
    if (node < 2) return true; // skip MTPNDD_FALSE/TRUE

    if (list->count == list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 32;
        mtpndd_t *next = (mtpndd_t *)realloc(list->items, new_cap * sizeof(mtpndd_t));
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

static inline void mtpndd_temp_refs_release(mtpndd_temp_ref_list_t *list) {
    if (!list || !list->items) return;
    for (size_t i = 0; i < list->count; ++i) {
        mtpndd_deref(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static inline mtpndd_node_record_t mtpndd_node_read(mtpndd_t idx) {
    return g_mtpndd_nodetable.data[(size_t)idx];
}

static inline mtpndd_edge_record_t mtpndd_node_edge(mtpndd_t node, uint32_t i) {
    // NOTE: edge_pool may be compacted during GC, which updates `edge_array_idx` in-place.
    // Avoid caching node records (and especially edge_array_idx) across recursive calls.
    mtpndd_node_record_t rec = mtpndd_node_read(node);
    return mtpndd_edge_at(rec.edge_array_idx + i);
}

static mtpndd_t mtpndd_and_rec(mtpndd_t a, mtpndd_t b, mtpndd_temp_ref_list_t *temp_refs);
static mtpndd_t mtpndd_or_rec(mtpndd_t a, mtpndd_t b, mtpndd_temp_ref_list_t *temp_refs);
static mtpndd_t mtpndd_not_rec(mtpndd_t a, mtpndd_temp_ref_list_t *temp_refs);
static mtpndd_t mtpndd_exist_rec(mtpndd_t a, uint32_t field, mtpndd_temp_ref_list_t *temp_refs);

/********************************
 * AND
 ********************************/
static mtpndd_t mtpndd_and_rec(mtpndd_t a, mtpndd_t b, mtpndd_temp_ref_list_t *temp_refs) {
    if (mtpndd_is_false(a) || mtpndd_is_true(b)) {
        return a;
    }
    if (mtpndd_is_false(b) || mtpndd_is_true(a) || a == b) {
        return b;
    }

    // cache key canonicalization for commutative op
    mtpndd_t lhs = a;
    mtpndd_t rhs = b;
    if (lhs > rhs) {
        mtpndd_t tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }
    mtpndd_op_cache_t *and_cache = g_mtpndd_config.and_cache;
    mtpndd_t cached = mtpndd_op_cache_lookup_binary(and_cache, lhs, rhs);
    if (cached != MTPNDD_INVALID) {
        MTPNDD_STAT_ADD(cache_lookup_hits, 1);
        return cached;
    }
    MTPNDD_STAT_ADD(cache_lookup_misses, 1);

    const mtpndd_node_record_t na = mtpndd_node_read(a);
    const mtpndd_node_record_t nb = mtpndd_node_read(b);

    mtpndd_edge_builder_t builder = {0};
    size_t estimate = 0;
    if (na.field_id == nb.field_id) {
        estimate = (size_t)na.edge_num * (size_t)nb.edge_num;
    } else {
        estimate = na.edge_num;
    }
    mtpndd_edge_builder_init(&builder, estimate ? estimate : 4);
    if (!builder.edges) {
        return MTPNDD_INVALID;
    }

    if (na.field_id == nb.field_id) {
        for (uint32_t ia = 0; ia < na.edge_num; ++ia) {
            const mtpndd_edge_record_t ea = mtpndd_node_edge(a, ia);
            for (uint32_t ib = 0; ib < nb.edge_num; ++ib) {
                const mtpndd_edge_record_t eb = mtpndd_node_edge(b, ib);
                mtpndd_bdd_t label = sylvan_ref(sylvan_and(ea.label, eb.label));
                if (label == sylvan_false) {
                    sylvan_deref(label);
                    continue;
                }
                mtpndd_t child = mtpndd_and_rec(ea.child, eb.child, temp_refs);
                if (child == MTPNDD_INVALID) {
                    sylvan_deref(label);
                    mtpndd_edge_builder_destroy(&builder);
                    return MTPNDD_INVALID;
                }
                // Protect intermediate result immediately
                if (!mtpndd_temp_refs_push(temp_refs, child)) {
                    sylvan_deref(label);
                    mtpndd_edge_builder_destroy(&builder);
                    MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                    return MTPNDD_INVALID;
                }
                if (!mtpndd_edge_builder_push(&builder, child, label)) {
                    mtpndd_edge_builder_destroy(&builder);
                    return MTPNDD_INVALID;
                }
            }
        }
    } else {
        // ensure a is the smaller field
        mtpndd_t top = a;
        mtpndd_t other = b;
        mtpndd_node_record_t top_node = na;
        if (na.field_id > nb.field_id) {
            top = b;
            other = a;
            top_node = nb;
        }

        for (uint32_t i = 0; i < top_node.edge_num; ++i) {
            const mtpndd_edge_record_t e = mtpndd_node_edge(top, i);
            mtpndd_t child = mtpndd_and_rec(e.child, other, temp_refs);
            if (child == MTPNDD_INVALID) {
                mtpndd_edge_builder_destroy(&builder);
                return MTPNDD_INVALID;
            }
            // Protect intermediate result immediately
            if (!mtpndd_temp_refs_push(temp_refs, child)) {
                mtpndd_edge_builder_destroy(&builder);
                MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                return MTPNDD_INVALID;
            }
            if (!mtpndd_edge_builder_push(&builder, child, sylvan_ref(e.label))) {
                mtpndd_edge_builder_destroy(&builder);
                return MTPNDD_INVALID;
            }
        }
        a = top;
    }

    mtpndd_t res = mtpndd_mk(mtpndd_node_read(a).field_id, &builder);
    mtpndd_edge_builder_destroy(&builder);
    if (res == MTPNDD_INVALID) {
        return MTPNDD_INVALID;
    }
    // No need for gc_protect_add - result immediately returned or cached
    mtpndd_op_cache_store_binary(and_cache, lhs, rhs, res);
    return res;
}

/********************************
 * OR
 ********************************/
static mtpndd_edge_record_t *mtpndd_residual_find(mtpndd_edge_record_t *edges, uint32_t edge_num, mtpndd_t child) {
    uint32_t lo = 0;
    uint32_t hi = edge_num;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        mtpndd_t v = edges[mid].child;
        if (v < child) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < edge_num && edges[lo].child == child) {
        return &edges[lo];
    }
    return NULL;
}

static inline void mtpndd_residual_apply_mask(mtpndd_edge_record_t *edge, mtpndd_bdd_t mask) {
    if (!edge) return;
    if (edge->label == sylvan_false) return;
    mtpndd_bdd_t updated = sylvan_ref(sylvan_and(edge->label, mask));
    sylvan_deref(edge->label);
    edge->label = updated;
}

static mtpndd_t mtpndd_or_rec(mtpndd_t a, mtpndd_t b, mtpndd_temp_ref_list_t *temp_refs) {
    if (mtpndd_is_true(a) || mtpndd_is_false(b)) {
        return a;
    }
    if (mtpndd_is_true(b) || mtpndd_is_false(a) || a == b) {
        return b;
    }

    // cache key canonicalization for commutative op
    mtpndd_t lhs = a;
    mtpndd_t rhs = b;
    if (lhs > rhs) {
        mtpndd_t tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }
    mtpndd_op_cache_t *or_cache = g_mtpndd_config.or_cache;
    mtpndd_t cached = mtpndd_op_cache_lookup_binary(or_cache, lhs, rhs);
    if (cached != MTPNDD_INVALID) {
        MTPNDD_STAT_ADD(cache_lookup_hits, 1);
        return cached;
    }
    MTPNDD_STAT_ADD(cache_lookup_misses, 1);

    const mtpndd_node_record_t na = mtpndd_node_read(a);
    const mtpndd_node_record_t nb = mtpndd_node_read(b);

    mtpndd_edge_builder_t builder = {0};
    size_t estimate = 0;
    if (na.field_id == nb.field_id) {
        estimate = (size_t)na.edge_num * (size_t)nb.edge_num + na.edge_num + nb.edge_num;
    } else {
        estimate = na.edge_num + 1;
    }
    mtpndd_edge_builder_init(&builder, estimate ? estimate : 4);
    if (!builder.edges) {
        return MTPNDD_INVALID;
    }

    uint32_t result_field = na.field_id;

    if (na.field_id == nb.field_id) {
        // residual copies (sorted by child)
        mtpndd_edge_record_t *residualA = NULL;
        mtpndd_edge_record_t *residualB = NULL;
        if (na.edge_num > 0) {
            residualA = (mtpndd_edge_record_t *)malloc(sizeof(mtpndd_edge_record_t) * na.edge_num);
        }
        if (nb.edge_num > 0) {
            residualB = (mtpndd_edge_record_t *)malloc(sizeof(mtpndd_edge_record_t) * nb.edge_num);
        }
        if ((na.edge_num > 0 && !residualA) || (nb.edge_num > 0 && !residualB)) {
            free(residualA);
            free(residualB);
            mtpndd_edge_builder_destroy(&builder);
            MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
            return MTPNDD_INVALID;
        }
        for (uint32_t i = 0; i < na.edge_num; ++i) {
            mtpndd_edge_record_t e = mtpndd_node_edge(a, i);
            residualA[i].child = e.child;
            residualA[i].label = sylvan_ref(e.label);
        }
        for (uint32_t i = 0; i < nb.edge_num; ++i) {
            mtpndd_edge_record_t e = mtpndd_node_edge(b, i);
            residualB[i].child = e.child;
            residualB[i].label = sylvan_ref(e.label);
        }

        for (uint32_t ia = 0; ia < na.edge_num; ++ia) {
            const mtpndd_edge_record_t ea = mtpndd_node_edge(a, ia);
            for (uint32_t ib = 0; ib < nb.edge_num; ++ib) {
                const mtpndd_edge_record_t eb = mtpndd_node_edge(b, ib);
                mtpndd_bdd_t intersect = sylvan_ref(sylvan_and(ea.label, eb.label));
                if (intersect == sylvan_false) {
                    sylvan_deref(intersect);
                    continue;
                }

                mtpndd_bdd_t not_intersect = sylvan_ref(sylvan_not(intersect));
                mtpndd_residual_apply_mask(mtpndd_residual_find(residualA, na.edge_num, ea.child), not_intersect);
                mtpndd_residual_apply_mask(mtpndd_residual_find(residualB, nb.edge_num, eb.child), not_intersect);
                sylvan_deref(not_intersect);

                mtpndd_t child = mtpndd_or_rec(ea.child, eb.child, temp_refs);
                if (child == MTPNDD_INVALID) {
                    sylvan_deref(intersect);
                    for (uint32_t i = 0; i < na.edge_num; ++i) sylvan_deref(residualA[i].label);
                    for (uint32_t i = 0; i < nb.edge_num; ++i) sylvan_deref(residualB[i].label);
                    free(residualA);
                    free(residualB);
                    mtpndd_edge_builder_destroy(&builder);
                    return MTPNDD_INVALID;
                }
                if (!mtpndd_temp_refs_push(temp_refs, child)) {
                    sylvan_deref(intersect);
                    for (uint32_t i = 0; i < na.edge_num; ++i) sylvan_deref(residualA[i].label);
                    for (uint32_t i = 0; i < nb.edge_num; ++i) sylvan_deref(residualB[i].label);
                    free(residualA);
                    free(residualB);
                    mtpndd_edge_builder_destroy(&builder);
                    MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                    return MTPNDD_INVALID;
                }
                if (!mtpndd_edge_builder_push(&builder, child, intersect)) {
                    for (uint32_t i = 0; i < na.edge_num; ++i) sylvan_deref(residualA[i].label);
                    for (uint32_t i = 0; i < nb.edge_num; ++i) sylvan_deref(residualB[i].label);
                    free(residualA);
                    free(residualB);
                    mtpndd_edge_builder_destroy(&builder);
                    return MTPNDD_INVALID;
                }
            }
        }

        // add residuals
        for (uint32_t i = 0; i < na.edge_num; ++i) {
            if (residualA[i].label != sylvan_false) {
                mtpndd_bdd_t moved = residualA[i].label;
                residualA[i].label = sylvan_false;
                if (!mtpndd_edge_builder_push(&builder, residualA[i].child, moved)) {
                    for (uint32_t j = i + 1; j < na.edge_num; ++j) sylvan_deref(residualA[j].label);
                    for (uint32_t j = 0; j < nb.edge_num; ++j) sylvan_deref(residualB[j].label);
                    free(residualA);
                    free(residualB);
                    mtpndd_edge_builder_destroy(&builder);
                    return MTPNDD_INVALID;
                }
            } else {
                sylvan_deref(residualA[i].label);
            }
        }
        for (uint32_t i = 0; i < nb.edge_num; ++i) {
            if (residualB[i].label != sylvan_false) {
                mtpndd_bdd_t moved = residualB[i].label;
                residualB[i].label = sylvan_false;
                if (!mtpndd_edge_builder_push(&builder, residualB[i].child, moved)) {
                    for (uint32_t j = i + 1; j < nb.edge_num; ++j) sylvan_deref(residualB[j].label);
                    free(residualA);
                    free(residualB);
                    mtpndd_edge_builder_destroy(&builder);
                    return MTPNDD_INVALID;
                }
            } else {
                sylvan_deref(residualB[i].label);
            }
        }

        free(residualA);
        free(residualB);
    } else {
        // ensure a is the smaller field
        mtpndd_t top = a;
        mtpndd_t other = b;
        mtpndd_node_record_t top_node = na;
        if (na.field_id > nb.field_id) {
            top = b;
            other = a;
            top_node = nb;
        }
        result_field = top_node.field_id;

        mtpndd_bdd_t residual_other = sylvan_ref(sylvan_true);
        for (uint32_t i = 0; i < top_node.edge_num; ++i) {
            const mtpndd_edge_record_t e = mtpndd_node_edge(top, i);

            mtpndd_bdd_t not_intersect = sylvan_ref(sylvan_not(e.label));
            mtpndd_bdd_t updated = sylvan_ref(sylvan_and(residual_other, not_intersect));
            sylvan_deref(residual_other);
            sylvan_deref(not_intersect);
            residual_other = updated;

            mtpndd_t child = mtpndd_or_rec(e.child, other, temp_refs);
            if (child == MTPNDD_INVALID) {
                sylvan_deref(residual_other);
                mtpndd_edge_builder_destroy(&builder);
                return MTPNDD_INVALID;
            }
            if (!mtpndd_temp_refs_push(temp_refs, child)) {
                sylvan_deref(residual_other);
                mtpndd_edge_builder_destroy(&builder);
                MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                return MTPNDD_INVALID;
            }
            if (!mtpndd_edge_builder_push(&builder, child, sylvan_ref(e.label))) {
                sylvan_deref(residual_other);
                mtpndd_edge_builder_destroy(&builder);
                return MTPNDD_INVALID;
            }
        }
        if (residual_other != sylvan_false) {
            if (!mtpndd_edge_builder_push(&builder, other, residual_other)) {
                mtpndd_edge_builder_destroy(&builder);
                return MTPNDD_INVALID;
            }
        } else {
            sylvan_deref(residual_other);
        }
        a = top;
    }

    mtpndd_t res = mtpndd_mk(result_field, &builder);
    mtpndd_edge_builder_destroy(&builder);
    if (res == MTPNDD_INVALID) {
        return MTPNDD_INVALID;
    }
    mtpndd_op_cache_store_binary(or_cache, lhs, rhs, res);
    return res;
}

/********************************
 * NOT
 ********************************/
static mtpndd_t mtpndd_not_rec(mtpndd_t a, mtpndd_temp_ref_list_t *temp_refs) {
    if (mtpndd_is_true(a)) return MTPNDD_FALSE;
    if (mtpndd_is_false(a)) return MTPNDD_TRUE;

    mtpndd_op_cache_t *not_cache = g_mtpndd_config.not_cache;
    mtpndd_t cached = mtpndd_op_cache_lookup_unary(not_cache, a);
    if (cached != MTPNDD_INVALID) {
        MTPNDD_STAT_ADD(cache_lookup_hits, 1);
        return cached;
    }
    MTPNDD_STAT_ADD(cache_lookup_misses, 1);

    const mtpndd_node_record_t na = mtpndd_node_read(a);

    mtpndd_edge_builder_t builder = {0};
    mtpndd_edge_builder_init(&builder, na.edge_num + 1);
    if (!builder.edges) {
        return MTPNDD_INVALID;
    }

    mtpndd_bdd_t residual = sylvan_ref(sylvan_true);
    for (uint32_t i = 0; i < na.edge_num; ++i) {
        const mtpndd_edge_record_t e = mtpndd_node_edge(a, i);

        mtpndd_bdd_t not_intersect = sylvan_ref(sylvan_not(e.label));
        mtpndd_bdd_t updated = sylvan_ref(sylvan_and(residual, not_intersect));
        sylvan_deref(residual);
        sylvan_deref(not_intersect);
        residual = updated;

        mtpndd_t child = mtpndd_not_rec(e.child, temp_refs);
        if (child == MTPNDD_INVALID) {
            sylvan_deref(residual);
            mtpndd_edge_builder_destroy(&builder);
            return MTPNDD_INVALID;
        }
        if (!mtpndd_temp_refs_push(temp_refs, child)) {
            sylvan_deref(residual);
            mtpndd_edge_builder_destroy(&builder);
            MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
            return MTPNDD_INVALID;
        }
        if (!mtpndd_edge_builder_push(&builder, child, sylvan_ref(e.label))) {
            sylvan_deref(residual);
            mtpndd_edge_builder_destroy(&builder);
            return MTPNDD_INVALID;
        }
    }

    if (residual != sylvan_false) {
        if (!mtpndd_edge_builder_push(&builder, MTPNDD_TRUE, residual)) {
            mtpndd_edge_builder_destroy(&builder);
            return MTPNDD_INVALID;
        }
    } else {
        sylvan_deref(residual);
    }

    mtpndd_t res = mtpndd_mk(na.field_id, &builder);
    mtpndd_edge_builder_destroy(&builder);
    if (res == MTPNDD_INVALID) {
        return MTPNDD_INVALID;
    }
    mtpndd_op_cache_store_unary(not_cache, a, res);
    return res;
}

/********************************
 * EXIST
 ********************************/
static mtpndd_t mtpndd_exist_rec(mtpndd_t a, uint32_t field, mtpndd_temp_ref_list_t *temp_refs) {
    if (mtpndd_is_terminal(a)) {
        return a;
    }
    const mtpndd_node_record_t na = mtpndd_node_read(a);

    if (na.field_id == field) {
        mtpndd_t acc = MTPNDD_FALSE;
        for (uint32_t i = 0; i < na.edge_num; ++i) {
            const mtpndd_edge_record_t e = mtpndd_node_edge(a, i);
            mtpndd_t new_acc = mtpndd_or_rec(acc, e.child, temp_refs);
            if (new_acc == MTPNDD_INVALID) {
                return MTPNDD_INVALID;
            }
            // Protect new accumulator, but only if it's not the last iteration
            // The final result will be returned without being in temp_refs
            if (i + 1 < na.edge_num) {
                if (!mtpndd_temp_refs_push(temp_refs, new_acc)) {
                    MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                    return MTPNDD_INVALID;
                }
            }
            acc = new_acc;
        }
        return acc;
    }

    mtpndd_edge_builder_t builder = {0};
    mtpndd_edge_builder_init(&builder, na.edge_num);
    if (!builder.edges) {
        return MTPNDD_INVALID;
    }
    for (uint32_t i = 0; i < na.edge_num; ++i) {
        const mtpndd_edge_record_t e = mtpndd_node_edge(a, i);
        mtpndd_t child = mtpndd_exist_rec(e.child, field, temp_refs);
        if (child == MTPNDD_INVALID) {
            mtpndd_edge_builder_destroy(&builder);
            return MTPNDD_INVALID;
        }
        if (!mtpndd_temp_refs_push(temp_refs, child)) {
            mtpndd_edge_builder_destroy(&builder);
            MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
            return MTPNDD_INVALID;
        }
        if (!mtpndd_edge_builder_push(&builder, child, sylvan_ref(e.label))) {
            mtpndd_edge_builder_destroy(&builder);
            return MTPNDD_INVALID;
        }
    }

    mtpndd_t res = mtpndd_mk(na.field_id, &builder);
    mtpndd_edge_builder_destroy(&builder);
    if (res == MTPNDD_INVALID) {
        return MTPNDD_INVALID;
    }
    return res;
}

/********************************
 * Public wrappers
 ********************************/
mtpndd_t mtpndd_and(mtpndd_t a, mtpndd_t b) {
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);
    mtpndd_t result = mtpndd_and_rec(a, b, &temp_refs);
    mtpndd_temp_refs_release(&temp_refs);
    return result;
}

mtpndd_t mtpndd_or(mtpndd_t a, mtpndd_t b) {
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);
    mtpndd_t result = mtpndd_or_rec(a, b, &temp_refs);
    mtpndd_temp_refs_release(&temp_refs);
    return result;
}

mtpndd_t mtpndd_not(mtpndd_t a) {
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);
    mtpndd_t result = mtpndd_not_rec(a, &temp_refs);
    mtpndd_temp_refs_release(&temp_refs);
    return result;
}

mtpndd_t mtpndd_diff(mtpndd_t a, mtpndd_t b) {
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);
    mtpndd_t not_b = mtpndd_not_rec(b, &temp_refs);
    if (not_b == MTPNDD_INVALID) {
        mtpndd_temp_refs_release(&temp_refs);
        return MTPNDD_INVALID;
    }
    mtpndd_t result = mtpndd_and_rec(a, not_b, &temp_refs);
    mtpndd_temp_refs_release(&temp_refs);
    return result;
}

mtpndd_t mtpndd_exist(mtpndd_t a, uint32_t field) {
    mtpndd_temp_ref_list_t temp_refs;
    mtpndd_temp_refs_init(&temp_refs);
    mtpndd_t result = mtpndd_exist_rec(a, field, &temp_refs);
    mtpndd_temp_refs_release(&temp_refs);
    return result;
}

/********************************
 * MTPNDD <-> MTBDD conversion
 ********************************/
typedef struct {
    mtpndd_t *keys;
    mtpndd_bdd_t *values;
    size_t capacity;
    size_t count;
} mtpndd_to_mtbdd_cache_t;

static mtpndd_error_t mtpndd_to_mtbdd_cache_init(mtpndd_to_mtbdd_cache_t *cache, size_t initial_capacity) {
    cache->capacity = 1;
    while (cache->capacity < initial_capacity) {
        cache->capacity <<= 1;
    }
    cache->count = 0;
    cache->keys = (mtpndd_t *)calloc(cache->capacity, sizeof(mtpndd_t));
    cache->values = (mtpndd_bdd_t *)calloc(cache->capacity, sizeof(mtpndd_bdd_t));
    if (!cache->keys || !cache->values) {
        free(cache->keys);
        free(cache->values);
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    return MTPNDD_SUCCESS;
}

static void mtpndd_to_mtbdd_cache_destroy(mtpndd_to_mtbdd_cache_t *cache) {
    if (!cache || !cache->keys) return;
    for (size_t i = 0; i < cache->capacity; ++i) {
        if (cache->keys[i] != 0) {
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

static bool mtpndd_to_mtbdd_cache_lookup(mtpndd_to_mtbdd_cache_t *cache, mtpndd_t key, mtpndd_bdd_t *value_out) {
    if (!cache || !cache->keys || cache->count == 0) return false;
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64(key) & mask;
    while (cache->keys[idx] != 0) {
        if (cache->keys[idx] == key) {
            *value_out = sylvan_ref(cache->values[idx]);
            return true;
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

static bool mtpndd_to_mtbdd_cache_rehash(mtpndd_to_mtbdd_cache_t *cache) {
    size_t new_capacity = cache->capacity << 1;
    mtpndd_t *new_keys = (mtpndd_t *)calloc(new_capacity, sizeof(mtpndd_t));
    mtpndd_bdd_t *new_values = (mtpndd_bdd_t *)calloc(new_capacity, sizeof(mtpndd_bdd_t));
    if (!new_keys || !new_values) {
        free(new_keys);
        free(new_values);
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return false;
    }
    size_t new_mask = new_capacity - 1;
    for (size_t i = 0; i < cache->capacity; ++i) {
        if (cache->keys[i] == 0) continue;
        mtpndd_t key = cache->keys[i];
        mtpndd_bdd_t val = cache->values[i];
        size_t idx = mtpndd_hash_u64(key) & new_mask;
        while (new_keys[idx] != 0) idx = (idx + 1) & new_mask;
        new_keys[idx] = key;
        new_values[idx] = val;
    }
    free(cache->keys);
    free(cache->values);
    cache->keys = new_keys;
    cache->values = new_values;
    cache->capacity = new_capacity;
    return true;
}

static bool mtpndd_to_mtbdd_cache_insert(mtpndd_to_mtbdd_cache_t *cache, mtpndd_t key, mtpndd_bdd_t value) {
    if ((cache->count + 1) * 10 >= cache->capacity * 7) {
        if (!mtpndd_to_mtbdd_cache_rehash(cache)) {
            return false;
        }
    }
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64(key) & mask;
    while (cache->keys[idx] != 0) {
        idx = (idx + 1) & mask;
    }
    cache->keys[idx] = key;
    cache->values[idx] = sylvan_ref(value);
    cache->count++;
    return true;
}

static mtpndd_error_t mtpndd_to_mtbdd_rec(mtpndd_t node, mtpndd_to_mtbdd_cache_t *cache, mtpndd_bdd_t *result) {
    if (mtpndd_is_true(node)) {
        *result = sylvan_ref(sylvan_true);
        return MTPNDD_SUCCESS;
    }
    if (mtpndd_is_false(node)) {
        *result = sylvan_ref(sylvan_false);
        return MTPNDD_SUCCESS;
    }

    mtpndd_bdd_t cached;
    if (mtpndd_to_mtbdd_cache_lookup(cache, node, &cached)) {
        *result = cached;
        return MTPNDD_SUCCESS;
    }

    const mtpndd_node_record_t n = mtpndd_node_read(node);
    mtpndd_bdd_t acc = sylvan_ref(sylvan_false);
    for (uint32_t i = 0; i < n.edge_num; ++i) {
        const mtpndd_edge_record_t e = mtpndd_node_edge(node, i);
        if (e.label == sylvan_false) continue;

        mtpndd_bdd_t child_bdd = sylvan_false;
        mtpndd_error_t status = mtpndd_to_mtbdd_rec(e.child, cache, &child_bdd);
        if (status != MTPNDD_SUCCESS) {
            sylvan_deref(acc);
            return status;
        }
        mtpndd_bdd_t conjunct = sylvan_ref(sylvan_and(e.label, child_bdd));
        sylvan_deref(child_bdd);
        if (conjunct != sylvan_false) {
            mtpndd_bdd_t combined = sylvan_ref(sylvan_or(acc, conjunct));
            sylvan_deref(acc);
            sylvan_deref(conjunct);
            acc = combined;
        } else {
            sylvan_deref(conjunct);
        }
    }

    if (!mtpndd_to_mtbdd_cache_insert(cache, node, acc)) {
        sylvan_deref(acc);
        return mtpndd_get_last_error().code;
    }
    *result = acc;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_to_mtbdd(mtpndd_t node, mtpndd_bdd_t *result) {
    MTPNDD_CHECK_INIT();
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
    uint64_t nodecount = sylvan_nodecount(tmp);
    __atomic_store_n(&g_mtpndd_stats.bdd_nodes_converted, nodecount, __ATOMIC_RELAXED);
    MTPNDD_STAT_ADD(bdd_nodes_processed_total, nodecount);
#endif
    mtpndd_bdd_t final = sylvan_ref(tmp);
    mtpndd_to_mtbdd_cache_destroy(&cache);
    sylvan_deref(tmp);
    *result = final;
    return MTPNDD_SUCCESS;
}

typedef struct {
    mtpndd_bdd_t *keys;
    mtpndd_t *values;
    size_t capacity;
    size_t count;
} mtbdd_to_mtpndd_cache_t;

static mtpndd_error_t mtbdd_to_mtpndd_cache_init(mtbdd_to_mtpndd_cache_t *cache, size_t initial_capacity) {
    cache->capacity = 1;
    while (cache->capacity < initial_capacity) {
        cache->capacity <<= 1;
    }
    cache->count = 0;
    cache->keys = (mtpndd_bdd_t *)calloc(cache->capacity, sizeof(mtpndd_bdd_t));
    cache->values = (mtpndd_t *)calloc(cache->capacity, sizeof(mtpndd_t));
    if (!cache->keys || !cache->values) {
        free(cache->keys);
        free(cache->values);
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }
    return MTPNDD_SUCCESS;
}

static void mtbdd_to_mtpndd_cache_destroy(mtbdd_to_mtpndd_cache_t *cache) {
    if (!cache) return;
    free(cache->keys);
    free(cache->values);
    cache->keys = NULL;
    cache->values = NULL;
    cache->capacity = 0;
    cache->count = 0;
}

static bool mtbdd_to_mtpndd_cache_lookup(mtbdd_to_mtpndd_cache_t *cache, mtpndd_bdd_t key, mtpndd_t *value_out) {
    if (!cache || !cache->keys || cache->count == 0) return false;
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64(key) & mask;
    while (cache->keys[idx] != 0) {
        if (cache->keys[idx] == key) {
            *value_out = cache->values[idx];
            return true;
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

static bool mtbdd_to_mtpndd_cache_rehash(mtbdd_to_mtpndd_cache_t *cache) {
    size_t new_capacity = cache->capacity << 1;
    mtpndd_bdd_t *new_keys = (mtpndd_bdd_t *)calloc(new_capacity, sizeof(mtpndd_bdd_t));
    mtpndd_t *new_values = (mtpndd_t *)calloc(new_capacity, sizeof(mtpndd_t));
    if (!new_keys || !new_values) {
        free(new_keys);
        free(new_values);
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return false;
    }
    size_t new_mask = new_capacity - 1;
    for (size_t i = 0; i < cache->capacity; ++i) {
        if (cache->keys[i] == 0) continue;
        mtpndd_bdd_t key = cache->keys[i];
        mtpndd_t val = cache->values[i];
        size_t idx = mtpndd_hash_u64(key) & new_mask;
        while (new_keys[idx] != 0) idx = (idx + 1) & new_mask;
        new_keys[idx] = key;
        new_values[idx] = val;
    }
    free(cache->keys);
    free(cache->values);
    cache->keys = new_keys;
    cache->values = new_values;
    cache->capacity = new_capacity;
    return true;
}

static bool mtbdd_to_mtpndd_cache_insert(mtbdd_to_mtpndd_cache_t *cache, mtpndd_bdd_t key, mtpndd_t value) {
    if ((cache->count + 1) * 10 >= cache->capacity * 7) {
        if (!mtbdd_to_mtpndd_cache_rehash(cache)) {
            return false;
        }
    }
    size_t mask = cache->capacity - 1;
    size_t idx = mtpndd_hash_u64(key) & mask;
    while (cache->keys[idx] != 0) {
        idx = (idx + 1) & mask;
    }
    cache->keys[idx] = key;
    cache->values[idx] = value;
    cache->count++;
    return true;
}

static mtpndd_field_info_t *mtpndd_find_field_by_var(uint32_t var) {
    for (uint32_t i = 1; i <= g_mtpndd_config.field_count; ++i) {
        mtpndd_field_info_t *field = g_mtpndd_config.field_info[i];
        if (!field) continue;
        uint32_t start = field->start_var;
        uint32_t end = start + field->bit_width;
        if (var >= start && var < end) {
            return field;
        }
    }
    return NULL;
}

static mtpndd_error_t mtbdd_to_mtpndd_rec(mtpndd_bdd_t bdd, mtbdd_to_mtpndd_cache_t *cache, mtpndd_t *result);

static mtpndd_error_t mtbdd_to_mtpndd_collect(
        mtpndd_bdd_t current,
        const mtpndd_field_info_t *field,
        uint32_t offset,
        mtpndd_bdd_t cube,
        mtbdd_to_mtpndd_cache_t *cache,
        mtpndd_edge_builder_t *builder)
{
    if (current == sylvan_false) {
        return MTPNDD_SUCCESS;
    }

    uint32_t field_end = field->start_var + field->bit_width;
    if (offset >= field->bit_width || mtbdd_isleaf(current) ||
        (!mtbdd_isleaf(current) && sylvan_var(current) >= field_end)) {
        mtpndd_t child = MTPNDD_INVALID;
        mtpndd_error_t status = mtbdd_to_mtpndd_rec(current, cache, &child);
        if (status != MTPNDD_SUCCESS) {
            return status;
        }
        if (!mtpndd_edge_builder_push(builder, child, sylvan_ref(cube))) {
            return mtpndd_get_last_error().code;
        }
        return MTPNDD_SUCCESS;
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

    mtpndd_bdd_t cube_low = sylvan_ref(sylvan_and(cube, literal_neg));
    mtpndd_error_t status = mtbdd_to_mtpndd_collect(low_child, field, offset + 1, cube_low, cache, builder);
    sylvan_deref(cube_low);
    if (status != MTPNDD_SUCCESS) {
        return status;
    }

    mtpndd_bdd_t cube_high = sylvan_ref(sylvan_and(cube, literal_pos));
    status = mtbdd_to_mtpndd_collect(high_child, field, offset + 1, cube_high, cache, builder);
    sylvan_deref(cube_high);
    return status;
}

static mtpndd_error_t mtbdd_to_mtpndd_rec(mtpndd_bdd_t bdd, mtbdd_to_mtpndd_cache_t *cache, mtpndd_t *result) {
    if (bdd == sylvan_false) {
        *result = MTPNDD_FALSE;
        return MTPNDD_SUCCESS;
    }
    if (bdd == sylvan_true) {
        *result = MTPNDD_TRUE;
        return MTPNDD_SUCCESS;
    }

    mtpndd_t cached = MTPNDD_INVALID;
    if (mtbdd_to_mtpndd_cache_lookup(cache, bdd, &cached)) {
        *result = cached;
        return MTPNDD_SUCCESS;
    }

    uint32_t var = sylvan_var(bdd);
    mtpndd_field_info_t *field = mtpndd_find_field_by_var(var);
    if (!field) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return MTPNDD_ERROR_INVALID_FIELD;
    }

    mtpndd_edge_builder_t builder = {0};
    mtpndd_edge_builder_init(&builder, 16);
    if (!builder.edges) {
        return MTPNDD_ERROR_OUT_OF_MEMORY;
    }

    mtpndd_error_t status = mtbdd_to_mtpndd_collect(bdd, field, 0, sylvan_true, cache, &builder);
    if (status != MTPNDD_SUCCESS) {
        mtpndd_edge_builder_destroy(&builder);
        return status;
    }

    mtpndd_t node = mtpndd_mk(field->field_id, &builder);
    mtpndd_edge_builder_destroy(&builder);
    if (node == MTPNDD_INVALID) {
        return mtpndd_get_last_error().code;
    }

    if (!mtbdd_to_mtpndd_cache_insert(cache, bdd, node)) {
        return mtpndd_get_last_error().code;
    }
    *result = node;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtbdd_to_mtpndd(mtpndd_bdd_t bdd, mtpndd_t *result) {
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

    mtpndd_t tmp = MTPNDD_INVALID;
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
        if (!field) continue;
        size_t end = (size_t)field->start_var + field->bit_width;
        if (end > total) {
            total = end;
        }
    }
    return total;
}

double mtpndd_satcount(mtpndd_t node) {
    MTPNDD_CHECK_INIT();
    mtpndd_bdd_t bdd = sylvan_false;
    mtpndd_error_t status = mtpndd_to_mtbdd(node, &bdd);
    if (status != MTPNDD_SUCCESS) {
        return 0.0;
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
    mtpndd_t node;
    size_t id;
    struct mtpndd_dot_visit_s *next;
} mtpndd_dot_visit_t;

typedef struct {
    mtpndd_dot_visit_t *visited;
    size_t next_id;
    bool error;
} mtpndd_dot_ctx_t;

static void mtpndd_dot_ctx_cleanup(mtpndd_dot_ctx_t *ctx) {
    if (!ctx) return;
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

static size_t mtpndd_dot_ctx_get_id(mtpndd_dot_ctx_t *ctx, mtpndd_t node, bool *is_new) {
    if (is_new) *is_new = false;
    for (mtpndd_dot_visit_t *entry = ctx->visited; entry; entry = entry->next) {
        if (entry->node == node) {
            return entry->id;
        }
    }
    mtpndd_dot_visit_t *entry = (mtpndd_dot_visit_t *)malloc(sizeof(mtpndd_dot_visit_t));
    if (!entry) {
        ctx->error = true;
        return 0;
    }
    entry->node = node;
    entry->id = ctx->next_id++;
    entry->next = ctx->visited;
    ctx->visited = entry;
    if (is_new) *is_new = true;
    return entry->id;
}

static void mtpndd_dot_emit_node(FILE *out, mtpndd_t node, size_t id) {
    if (mtpndd_is_true(node)) {
        fprintf(out, "  n%zu [shape=box,label=\"TRUE\"];\n", id);
        return;
    }
    if (mtpndd_is_false(node)) {
        fprintf(out, "  n%zu [shape=box,label=\"FALSE\"];\n", id);
        return;
    }
    mtpndd_node_record_t rec = mtpndd_node_read(node);
    fprintf(out, "  n%zu [label=\"f=%u idx=%" PRIu64 "\"];\n", id, rec.field_id, (uint64_t)node);
}

static void mtpndd_dot_emit_edges(FILE *out, mtpndd_dot_ctx_t *ctx, mtpndd_t node, size_t from_id) {
    if (mtpndd_is_terminal(node)) {
        return;
    }
    mtpndd_node_record_t rec = mtpndd_node_read(node);
    for (uint32_t i = 0; i < rec.edge_num; ++i) {
        mtpndd_edge_record_t e = mtpndd_edge_at(rec.edge_array_idx + i);
        bool child_new = false;
        size_t child_id = mtpndd_dot_ctx_get_id(ctx, e.child, &child_new);
        if (ctx->error) return;
        if (child_new) {
            mtpndd_dot_emit_node(out, e.child, child_id);
        }
        fprintf(out, "  n%zu -> n%zu [label=\"%lu\"];\n",
                from_id, child_id, (unsigned long)e.label);
        if (child_new) {
            mtpndd_dot_emit_edges(out, ctx, e.child, child_id);
            if (ctx->error) return;
        }
    }
}

void mtpndd_fprint_dot(FILE *out, mtpndd_t root) {
    if (!out) return;
    mtpndd_dot_ctx_t ctx = {0};
    fprintf(out, "digraph MTPNDD {\n");
    bool is_new = false;
    size_t root_id = mtpndd_dot_ctx_get_id(&ctx, root, &is_new);
    if (!ctx.error) {
        mtpndd_dot_emit_node(out, root, root_id);
        mtpndd_dot_emit_edges(out, &ctx, root, root_id);
    }
    fprintf(out, "}\n");
    mtpndd_dot_ctx_cleanup(&ctx);
}

void mtpndd_print_dot(mtpndd_t root) {
    mtpndd_fprint_dot(stdout, root);
}
