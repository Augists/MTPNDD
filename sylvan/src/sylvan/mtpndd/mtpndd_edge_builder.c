// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_edge_builder.h"

#include <stdlib.h>
#include <string.h>

#include "sylvan.h"

static size_t mtpndd_next_capacity(size_t current, size_t required) {
    size_t cap = current ? current : 16;
    while (cap < required) {
        size_t next = cap << 1;
        if (next <= cap) {
            return required;
        }
        cap = next;
    }
    return cap;
}

void mtpndd_edge_builder_init(mtpndd_edge_builder_t *builder, size_t initial_capacity) {
    if (!builder) {
        return;
    }
    memset(builder, 0, sizeof(*builder));
    if (initial_capacity == 0) {
        return;
    }
    builder->edges = (mtpndd_edge_record_t *)malloc(initial_capacity * sizeof(mtpndd_edge_record_t));
    if (!builder->edges) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return;
    }
    builder->capacity = initial_capacity;
    builder->count = 0;
}

void mtpndd_edge_builder_reset(mtpndd_edge_builder_t *builder) {
    if (!builder) {
        return;
    }
    for (size_t i = 0; i < builder->count; ++i) {
        sylvan_deref(builder->edges[i].label);
    }
    builder->count = 0;
}

void mtpndd_edge_builder_destroy(mtpndd_edge_builder_t *builder) {
    if (!builder) {
        return;
    }
    mtpndd_edge_builder_reset(builder);
    free(builder->edges);
    builder->edges = NULL;
    builder->capacity = 0;
}

bool mtpndd_edge_builder_push(mtpndd_edge_builder_t *builder, mtpndd_t child, mtpndd_bdd_t label) {
    if (!builder) {
        sylvan_deref(label);
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NULL_POINTER);
        return false;
    }
    if (child == MTPNDD_FALSE || label == sylvan_false) {
        sylvan_deref(label);
        return true;
    }
    size_t needed = builder->count + 1;
    if (unlikely(needed > builder->capacity)) {
        size_t new_capacity = mtpndd_next_capacity(builder->capacity, needed);
        mtpndd_edge_record_t *new_edges =
                (mtpndd_edge_record_t *)realloc(builder->edges, new_capacity * sizeof(mtpndd_edge_record_t));
        if (!new_edges) {
            sylvan_deref(label);
            MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
            return false;
        }
        builder->edges = new_edges;
        builder->capacity = new_capacity;
    }
    builder->edges[builder->count].child = child;
    builder->edges[builder->count].label = label;
    builder->count++;
    return true;
}

static int edge_child_cmp(const void *lhs, const void *rhs) {
    const mtpndd_edge_record_t *a = (const mtpndd_edge_record_t *)lhs;
    const mtpndd_edge_record_t *b = (const mtpndd_edge_record_t *)rhs;
    if (a->child < b->child) return -1;
    if (a->child > b->child) return 1;
    return 0;
}

bool mtpndd_edge_builder_finalize(mtpndd_edge_builder_t *builder) {
    if (!builder) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NULL_POINTER);
        return false;
    }
    if (builder->count <= 1) {
        return true;
    }

    qsort(builder->edges, builder->count, sizeof(mtpndd_edge_record_t), edge_child_cmp);

    size_t write = 0;
    size_t read = 0;
    while (read < builder->count) {
        mtpndd_t child = builder->edges[read].child;
        mtpndd_bdd_t acc = builder->edges[read].label;
        read++;

        while (read < builder->count && builder->edges[read].child == child) {
            mtpndd_bdd_t merged = sylvan_ref(sylvan_or(acc, builder->edges[read].label));
            sylvan_deref(acc);
            sylvan_deref(builder->edges[read].label);
            acc = merged;
            read++;
        }

        if (acc != sylvan_false && child != MTPNDD_FALSE) {
            builder->edges[write].child = child;
            builder->edges[write].label = acc;
            write++;
        } else {
            sylvan_deref(acc);
        }
    }

    builder->count = write;
    return true;
}

