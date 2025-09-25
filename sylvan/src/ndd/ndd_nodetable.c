// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd_nodetable.h"
#include <stdlib.h>

// Global node table (bucketed by field)
static hash_table_t **g_node_tables_by_field = NULL;
static uint32_t g_node_tables_capacity = 0;

int ndd_nodetable_init(uint32_t initial_fields_capacity)
{
    if (g_node_tables_by_field) return 0;
    if (initial_fields_capacity == 0) initial_fields_capacity = 16;
    g_node_tables_by_field = (hash_table_t**)calloc(initial_fields_capacity, sizeof(hash_table_t*));
    if (!g_node_tables_by_field) return -1;
    g_node_tables_capacity = initial_fields_capacity;
    return 0;
}

void ndd_nodetable_shutdown()
{
    if (!g_node_tables_by_field) return;
    for (uint32_t i = 0; i < g_node_tables_capacity; ++i) {
        hash_table_t *bucket = g_node_tables_by_field[i];
        if (bucket) {
            for (size_t b = 0; b < bucket->bucket_count; ++b) {
                hash_entry_t *e = bucket->buckets[b];
                while (e) {
                    hash_table_t *second = (hash_table_t*)e->value;
                    if (second) hash_table_destroy(second);
                    e = e->next;
                }
            }
            hash_table_destroy(bucket);
        }
    }
    free(g_node_tables_by_field);
    g_node_tables_by_field = NULL;
    g_node_tables_capacity = 0;
}

int ndd_nodetable_ensure_field(uint32_t field_id)
{
    if (!g_node_tables_by_field) return -1;
    if (field_id >= g_node_tables_capacity) {
        uint32_t new_cap = g_node_tables_capacity;
        while (field_id >= new_cap) new_cap *= 2;
        hash_table_t **new_arr = (hash_table_t**)realloc(g_node_tables_by_field, new_cap * sizeof(hash_table_t*));
        if (!new_arr) return -1;
        for (uint32_t i = g_node_tables_capacity; i < new_cap; ++i) new_arr[i] = NULL;
        g_node_tables_by_field = new_arr;
        g_node_tables_capacity = new_cap;
    }
    if (!g_node_tables_by_field[field_id]) {
        g_node_tables_by_field[field_id] = hash_table_create(128, (hash_t(*)(const void*))int_hash, NULL);
        if (!g_node_tables_by_field[field_id]) return -1;
    }
    return 0;
}

// Helper: unordered edge set hash
static hash_t ndd_edges_hash_unordered(hash_table_t *edges_map)
{
    if (!edges_map) return 0;
    hash_t acc = 1469598103u;
    for (size_t b = 0; b < edges_map->bucket_count; ++b) {
        hash_entry_t *e = edges_map->buckets[b];
        while (e) {
            ndd_t desc = (ndd_t)e->key;
            ndd_bdd_t lbl = *(ndd_bdd_t*)e->value;
            hash_t h1 = ptr_hash(desc);
            hash_t h2 = int_hash((uint64_t)lbl);
            acc ^= h1 + 0x9e3779b9 + (acc<<6) + (acc>>2);
            acc ^= h2 + 0x9e3779b9 + (acc<<6) + (acc>>2);
            e = e->next;
        }
    }
    return acc;
}

// Helper: edge set equality (unordered)
static int ndd_edges_equal(hash_table_t *a, hash_table_t *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->size != b->size) return 0;
    for (size_t i = 0; i < a->bucket_count; ++i) {
        hash_entry_t *e = a->buckets[i];
        while (e) {
            ndd_t desc = (ndd_t)e->key;
            ndd_bdd_t *lbl_a = (ndd_bdd_t*)e->value;
            ndd_bdd_t *lbl_b = (ndd_bdd_t*)hash_table_get(b, desc);
            if (!lbl_b) return 0;
            if (*lbl_a != *lbl_b) return 0;
            e = e->next;
        }
    }
    return 1;
}

ndd_t ndd_intern_node(ndd_t node)
{
    if (!node) return node;
    uint32_t f = node->field;
    if (ndd_nodetable_ensure_field(f) != 0) return node;
    hash_table_t *bucket = g_node_tables_by_field[f];
    hash_t eh = ndd_edges_hash_unordered(node->edges_map);
    hash_table_t *second = (hash_table_t*)hash_table_get(bucket, (void*)(uintptr_t)eh);
    if (!second) {
        second = hash_table_create(8, ptr_hash, ptr_compare);
        if (!second) return node;
        hash_table_put(bucket, (void*)(uintptr_t)eh, second);
    }
    for (size_t b = 0; b < second->bucket_count; ++b) {
        hash_entry_t *e = second->buckets[b];
        while (e) {
            ndd_t cand = (ndd_t)e->value;
            if (ndd_edges_equal(cand->edges_map, node->edges_map)) {
                return cand;
            }
            e = e->next;
        }
    }
    hash_table_put(second, node, node);
    return node;
}


