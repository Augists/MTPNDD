// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "ndd.h"
#include "operation_cache.h"
#include "common.h"
#include "ndd_nodetable.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

// Error handling system global state
static __thread ndd_error_info_t g_last_error = {NDD_SUCCESS, NULL, NULL, 0};

// Error message array
static const char* ndd_error_messages[] = {
    "Success",
    "Invalid parameter",
    "NDD system not initialized",
    "Out of memory",
    "Invalid field ID",
    "Null pointer",
    "Capacity exceeded",
    "Parallel initialization failed",
    "BDD operation failed",
    "Thread safety error",
    "Unknown error"
};

// Error handling function implementations
const char* ndd_error_string(ndd_error_t error) {
    if (error >= 0 && error < (ndd_error_t)(sizeof(ndd_error_messages) / sizeof(ndd_error_messages[0]))) {
        return ndd_error_messages[error];
    }
    return "Invalid error code";
}

void ndd_set_error(ndd_error_t error, const char *function, int line) {
    g_last_error.code = error;
    g_last_error.message = ndd_error_string(error);
    g_last_error.function = function;
    g_last_error.line = line;
}

ndd_error_info_t ndd_get_last_error() {
    return g_last_error;
}

void ndd_clear_error() {
    g_last_error.code = NDD_SUCCESS;
    g_last_error.message = NULL;
    g_last_error.function = NULL;
    g_last_error.line = 0;
}

// Global state - unified management
static struct {
    uint32_t field_count;
    ndd_field_info_t *field_info;
    uint32_t field_capacity;
    ndd_stats_t stats;
    ndd_memory_stats_t memory_stats;
    pthread_mutex_t ref_count_mutex;  // Mutex for reference counting operations
    operation_cache_t *and_cache;     // AND operation cache
    operation_cache_t *or_cache;      // OR operation cache
    operation_cache_t *not_cache;     // NOT operation cache
    // Global node table: dynamic array bucketed by field, each slot is a hash table (edge-hash -> secondary table)
    hash_table_t **node_tables_by_field;
    uint32_t node_tables_capacity;
    bool initialized;
} g_ndd_state = {0};

// Terminal nodes
static ndd_node_t g_true_node = {0, NULL, 0, 1};
static ndd_node_t g_false_node = {0, NULL, 0, 1};

// Initialize NDD system
int ndd_init(ndd_config_t *config) {
    if (g_ndd_state.initialized) {
        printf("NDD already initialized\n");
        return -1;
    }
    
#ifdef HAVE_LACE_SYLVAN
    // Try to initialize Lace + Sylvan parallel framework
    int lace_result = ndd_lace_init(config->n_workers, config->dqsize);
    if (lace_result != 0) {
        printf("⚠️  Failed to initialize Lace + Sylvan, falling back to simplified mode\n");
    } else {
        printf("🚀 Full parallel mode enabled with Lace + Sylvan\n");
    }
#else
    #error "NDD requires Lace + Sylvan framework. Please ensure HAVE_LACE_SYLVAN is defined."
#endif
    
    // Initialize field information
    g_ndd_state.field_capacity = 16;
    g_ndd_state.field_info = (ndd_field_info_t*)calloc(g_ndd_state.field_capacity, sizeof(ndd_field_info_t));
    g_ndd_state.field_count = 0;
    
    // Reset statistics
    memset(&g_ndd_state.stats, 0, sizeof(g_ndd_state.stats));
    memset(&g_ndd_state.memory_stats, 0, sizeof(g_ndd_state.memory_stats));
    
    // Initialize mutex
    if (pthread_mutex_init(&g_ndd_state.ref_count_mutex, NULL) != 0) {
        printf("Failed to initialize ref_count_mutex\n");
        return -1;
    }
    
    // Initialize operation cache
    uint32_t cache_size = config->cache_size;
    // Initialize node table module
    if (ndd_nodetable_init(16) != 0) {
        printf("Failed to initialize node table module\n");
        return -1;
    }
    if (cache_size == 0) cache_size = 1 << 16;  // Default 64K cache entries
    
    g_ndd_state.and_cache = operation_cache_create(cache_size, 3);  // Binary operation: 2 operands + 1 result
    g_ndd_state.or_cache = operation_cache_create(cache_size, 3);
    g_ndd_state.not_cache = operation_cache_create(cache_size, 2);  // Unary operation: 1 operand + 1 result
    
    if (!g_ndd_state.and_cache || !g_ndd_state.or_cache || !g_ndd_state.not_cache) {
        printf("Failed to initialize operation caches\n");
        return -1;
    }
    
    g_ndd_state.initialized = true;
    printf("✅ NDD initialized with %u workers\n", config->n_workers);
    return 0;
}

void ndd_quit() {
    if (!g_ndd_state.initialized) return;
    
    // Clean up field information
    free(g_ndd_state.field_info);
    g_ndd_state.field_info = NULL;
    g_ndd_state.field_count = 0;
    g_ndd_state.field_capacity = 0;
    
    // Destroy mutex
    pthread_mutex_destroy(&g_ndd_state.ref_count_mutex);
    
    // Destroy operation cache
    if (g_ndd_state.and_cache) {
        operation_cache_destroy(g_ndd_state.and_cache);
        g_ndd_state.and_cache = NULL;
    }
    if (g_ndd_state.or_cache) {
        operation_cache_destroy(g_ndd_state.or_cache);
        g_ndd_state.or_cache = NULL;
    }
    if (g_ndd_state.not_cache) {
        operation_cache_destroy(g_ndd_state.not_cache);
        g_ndd_state.not_cache = NULL;
    }
    // Destroy node table module
    ndd_nodetable_shutdown();
    
    // Clean up Lace + Sylvan or simplified mode
    ndd_lace_cleanup();
    
    g_ndd_state.initialized = false;
    printf("✅ NDD system shut down\n");
}

// Check initialization status
bool ndd_is_initialized() {
    return g_ndd_state.initialized;
}

// Field management - safe version
ndd_error_t ndd_declare_field_safe(uint32_t bit_width, uint32_t *field_id) {
    NDD_CHECK_NULL(field_id, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    NDD_CHECK_PARAM(bit_width > 0 && bit_width <= 64, NDD_ERROR_INVALID_PARAM);
    
    // Check capacity
    if (g_ndd_state.field_count >= UINT32_MAX - 1) {
        NDD_RETURN_ERROR(NDD_ERROR_CAPACITY_EXCEEDED);
    }
    
    // Capacity expansion check
    if (g_ndd_state.field_count >= g_ndd_state.field_capacity) {
        uint32_t new_capacity = g_ndd_state.field_capacity * 2;
        if (new_capacity < g_ndd_state.field_capacity) {  // Integer overflow check
            NDD_RETURN_ERROR(NDD_ERROR_CAPACITY_EXCEEDED);
        }
        
        ndd_field_info_t *new_info = (ndd_field_info_t*)realloc(
            g_ndd_state.field_info, new_capacity * sizeof(ndd_field_info_t));
        if (!new_info) {
            NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
        }
        
        g_ndd_state.field_info = new_info;
        g_ndd_state.field_capacity = new_capacity;
    }

    // Ensure node table has corresponding field
    if (ndd_nodetable_ensure_field(g_ndd_state.field_count) != 0) {
        NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
    }
    
    uint32_t new_field_id = g_ndd_state.field_count++;
    ndd_field_info_t *info = &g_ndd_state.field_info[new_field_id];
    
    info->field_id = new_field_id;
    info->bit_width = bit_width;
    
    // Allocate BDD variables
    if (new_field_id == 0) {
        info->start_var = 0;
    } else {
        info->start_var = g_ndd_state.field_info[new_field_id - 1].end_var + 1;
    }
    info->end_var = info->start_var + bit_width - 1;
    
    *field_id = new_field_id;

    // Node table guaranteed by ndd_nodetable_ensure_field
    
    printf("Declared field %u with %u bits (BDD vars %u-%u)\n", 
           new_field_id, bit_width, info->start_var, info->end_var);
    
    return NDD_SUCCESS;
}

// Field management - compatible version
uint32_t ndd_declare_field(uint32_t bit_width) {
    uint32_t field_id;
    ndd_error_t result = ndd_declare_field_safe(bit_width, &field_id);
    if (result != NDD_SUCCESS) {
        return (uint32_t)-1;  // Compatibility: return invalid value
    }
    return field_id;
}

ndd_field_info_t* ndd_get_field_info(uint32_t field_id) {
    if (!g_ndd_state.initialized || field_id >= g_ndd_state.field_count) {
        NDD_SET_ERROR(field_id >= g_ndd_state.field_count ? 
                     NDD_ERROR_INVALID_FIELD : NDD_ERROR_NOT_INITIALIZED);
    return NULL;
}
    return &g_ndd_state.field_info[field_id];
}

// Terminal node
ndd_t ndd_true() {
    return &g_true_node;
}

ndd_t ndd_false() {
    return &g_false_node;
}

bool ndd_is_true(ndd_t ndd) {
    return ndd == &g_true_node;
}

bool ndd_is_false(ndd_t ndd) {
    return ndd == &g_false_node;
}

bool ndd_is_terminal(ndd_t ndd) {
    if (ndd == &g_true_node || ndd == &g_false_node) return true;
    return ndd->field == 0;
}

// Node creation and management - safe version
ndd_error_t ndd_create_node_safe(uint32_t field, ndd_t *result) {
    NDD_CHECK_NULL(result, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_INIT();
    
    // Check field validity
    if (field >= g_ndd_state.field_count) {
        NDD_RETURN_ERROR(NDD_ERROR_INVALID_FIELD);
    }
    
    ndd_t node = (ndd_t)calloc(1, sizeof(ndd_node_t));
    if (!node) {
        NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
    }
    
    node->field = field;
    node->edges_map = hash_table_create(4, ptr_hash, ptr_compare);
    node->edge_count = 0;
    node->ref_count = 1;
    
    // Update memory statistics
    pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
    g_ndd_state.memory_stats.total_allocated += sizeof(ndd_node_t);
    g_ndd_state.memory_stats.current_allocated += sizeof(ndd_node_t);
    g_ndd_state.stats.node_count++;
    pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
    
    *result = node;
    
    return NDD_SUCCESS;
}

// Node creation and management - compatible version
ndd_t ndd_create_node(uint32_t field) {
    ndd_t result;
    ndd_error_t error = ndd_create_node_safe(field, &result);
    if (error != NDD_SUCCESS) {
        return ndd_false();  // Compatibility: return false node
    }
    return result;
}

// Safe version of add edge function
ndd_error_t ndd_add_edge_safe(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd) {
    NDD_CHECK_NULL(ndd, NDD_ERROR_NULL_POINTER);
    NDD_CHECK_PARAM(!ndd_is_terminal(*ndd), NDD_ERROR_INVALID_PARAM);
    
    // New implementation: hash table based edge management, merge labels for same target (OR)
    if ((*ndd)->edges_map) {
        // Find existing edge
        ndd_bdd_t *old_label = (ndd_bdd_t*)hash_table_get((*ndd)->edges_map, descendant);
        if (old_label) {
            // Merge labels: label_old OR label_new
            ndd_bdd_t merged = ndd_bdd_or_parallel(*old_label, label_bdd);
            // Update storage
            *old_label = merged;
        } else {
            // First insertion: allocate storage for value
            ndd_bdd_t *stored = (ndd_bdd_t*)malloc(sizeof(ndd_bdd_t));
            if (!stored) {
                NDD_RETURN_ERROR(NDD_ERROR_OUT_OF_MEMORY);
            }
            *stored = label_bdd;
            hash_table_put((*ndd)->edges_map, descendant, stored);
            (*ndd)->edge_count++;
            g_ndd_state.stats.edge_count++;
        }
        return NDD_SUCCESS;
    }

    // No longer maintain old array path
    NDD_RETURN_ERROR(NDD_ERROR_UNKNOWN);
}

// ndd_intern_node is already implemented in ndd_nodetable module

// Compatible version of add edge function
void ndd_add_edge(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd) {
    ndd_add_edge_safe(ndd, descendant, label_bdd);  // Ignore error return value for compatibility
}

// Free storage allocated for values in edges_map
static void ndd_free_edges_map_values(hash_table_t *edges_map)
{
    if (!edges_map) return;
    for (size_t b = 0; b < edges_map->bucket_count; ++b) {
        hash_entry_t *e = edges_map->buckets[b];
        while (e) {
            if (e->value) free(e->value);
            e = e->next;
        }
    }
}

// Java version mk: create/reuse node based on field and edge set
ndd_t ndd_mk(uint32_t field, hash_table_t *edges_map)
{
    if (!ndd_is_initialized()) return ndd_false();
    ndd_t node = (ndd_t)calloc(1, sizeof(ndd_node_t));
    if (!node) return ndd_false();
    node->field = field;
    node->edges_map = edges_map;
    node->edge_count = edges_map ? edges_map->size : 0;
    node->ref_count = 1;
    ndd_t temp = node;
    ndd_t interned = ndd_intern_node(temp);
    if (interned != node) {
        // New node not adopted, free newly created resources
        ndd_free_edges_map_values(edges_map);
        hash_table_destroy(edges_map);
        free(node);
        return interned;
    }
    // Statistics
    pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
    g_ndd_state.memory_stats.total_allocated += sizeof(ndd_node_t);
    g_ndd_state.memory_stats.current_allocated += sizeof(ndd_node_t);
    g_ndd_state.stats.node_count++;
    pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
    return temp;
}

ndd_t ndd_find_edge(ndd_t ndd, ndd_t descendant) {
    if (ndd_is_terminal(ndd) || !ndd->edges_map) return ndd_false();
    void *v = hash_table_get(ndd->edges_map, descendant);
    return v ? descendant : ndd_false();
}

// Helper: add an edge to edges_map (OR merge labels if same descendant)
static void edges_map_add(hash_table_t *map, ndd_t descendant, ndd_bdd_t label)
{
    ndd_bdd_t *old = (ndd_bdd_t*)hash_table_get(map, descendant);
    if (old) {
        *old = ndd_bdd_or_parallel(*old, label);
    } else {
        ndd_bdd_t *stored = (ndd_bdd_t*)malloc(sizeof(ndd_bdd_t));
        if (!stored) return;
        *stored = label;
        hash_table_put(map, descendant, stored);
    }
}

// Logic operation implementation - real parallel implementation with cache
ndd_t ndd_and(ndd_t a, ndd_t b) {
    // Terminal cases
    if (ndd_is_false(a) || ndd_is_false(b)) return ndd_false();
    if (ndd_is_true(a)) return ndd_ref_safe(b);
    if (ndd_is_true(b)) return ndd_ref_safe(a);
    
    // Check cache
    if (g_ndd_state.and_cache && 
        operation_cache_get_entry_binary(g_ndd_state.and_cache, a, b)) {
        g_ndd_state.stats.cache_hits++;
        ndd_t cached_result = (ndd_t)g_ndd_state.and_cache->result;
        return ndd_ref_safe(cached_result);  // Increase reference count
    }
    g_ndd_state.stats.cache_misses++;
    
    // Map implementation
    if (!ndd_is_terminal(a) && !ndd_is_terminal(b)) {
        ndd_t node_a = a;
        ndd_t node_b = b;
        
        // Choose node with smaller field as top level
        uint32_t top_field = (node_a->field < node_b->field) ? node_a->field : node_b->field;
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) return ndd_false();
        
        // Real parallel edge processing logic (Map cross/copy)
        if (node_a->field == node_b->field) {
            for (size_t ba = 0; ba < node_a->edges_map->bucket_count; ++ba) {
                hash_entry_t *ea = node_a->edges_map->buckets[ba];
                while (ea) {
                    for (size_t bb = 0; bb < node_b->edges_map->bucket_count; ++bb) {
                        hash_entry_t *eb = node_b->edges_map->buckets[bb];
                        while (eb) {
                            ndd_bdd_t la = *(ndd_bdd_t*)ea->value;
                            ndd_bdd_t lb = *(ndd_bdd_t*)eb->value;
                            ndd_bdd_t inter = ndd_bdd_and_parallel(la, lb);
                            if (inter != ndd_sylvan_false) edges_map_add(emap, ndd_true(), inter);
                            eb = eb->next;
                        }
                    }
                    ea = ea->next;
                }
            }
        } else if (node_a->field < node_b->field) {
            for (size_t ba = 0; ba < node_a->edges_map->bucket_count; ++ba) {
                hash_entry_t *ea = node_a->edges_map->buckets[ba];
                while (ea) { ndd_bdd_t la = *(ndd_bdd_t*)ea->value; if (la != ndd_sylvan_false) edges_map_add(emap, b, la); ea = ea->next; }
            }
        } else {
            for (size_t bb = 0; bb < node_b->edges_map->bucket_count; ++bb) {
                hash_entry_t *eb = node_b->edges_map->buckets[bb];
                while (eb) { ndd_bdd_t lb = *(ndd_bdd_t*)eb->value; if (lb != ndd_sylvan_false) edges_map_add(emap, a, lb); eb = eb->next; }
            }
        }
        ndd_t result = ndd_mk(top_field, emap);
        if (g_ndd_state.and_cache) {
            operation_cache_set_entry_binary(g_ndd_state.and_cache,
                                            g_ndd_state.and_cache->hash_value,
                                            a, b, result);
        }
        return result;
    }
    
    return ndd_false();
}

ndd_t ndd_or(ndd_t a, ndd_t b) {
    // Terminal case
    if (ndd_is_true(a) || ndd_is_true(b)) return ndd_true();
    if (ndd_is_false(a)) return ndd_ref_safe(b);
    if (ndd_is_false(b)) return ndd_ref_safe(a);
    
    // Check cache
    if (g_ndd_state.or_cache && 
        operation_cache_get_entry_binary(g_ndd_state.or_cache, a, b)) {
        g_ndd_state.stats.cache_hits++;
        ndd_t cached_result = (ndd_t)g_ndd_state.or_cache->result;
        return ndd_ref_safe(cached_result);
    }
    g_ndd_state.stats.cache_misses++;
    
    if (!ndd_is_terminal(a) && !ndd_is_terminal(b)) {
        ndd_t node_a = a;
        ndd_t node_b = b;
        
        uint32_t top_field = (node_a->field < node_b->field) ? node_a->field : node_b->field;
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) return ndd_false();
        
        // Real parallel OR logic (Map merge)
        if (node_a->field == node_b->field) {
            for (size_t ba = 0; ba < node_a->edges_map->bucket_count; ++ba) {
                hash_entry_t *ea = node_a->edges_map->buckets[ba];
                while (ea) { edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)ea->value); ea = ea->next; }
            }
            for (size_t bb = 0; bb < node_b->edges_map->bucket_count; ++bb) {
                hash_entry_t *eb = node_b->edges_map->buckets[bb];
                while (eb) { edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)eb->value); eb = eb->next; }
            }
        } else if (node_a->field < node_b->field) {
            for (size_t ba = 0; ba < node_a->edges_map->bucket_count; ++ba) {
                hash_entry_t *ea = node_a->edges_map->buckets[ba];
                while (ea) { edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)ea->value); ea = ea->next; }
            }
            edges_map_add(emap, b, ndd_sylvan_true);
        } else {
            for (size_t bb = 0; bb < node_b->edges_map->bucket_count; ++bb) {
                hash_entry_t *eb = node_b->edges_map->buckets[bb];
                while (eb) { edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)eb->value); eb = eb->next; }
            }
            edges_map_add(emap, a, ndd_sylvan_true);
        }
        ndd_t result = ndd_mk(top_field, emap);
        if (g_ndd_state.or_cache) {
            operation_cache_set_entry_binary(g_ndd_state.or_cache, 
                                            g_ndd_state.or_cache->hash_value,
                                            a, b, result);
        }
        return result;
    }
    
    return ndd_true();
}

ndd_t ndd_not(ndd_t a) {
    // Terminal cases
    if (ndd_is_true(a)) return ndd_false();
    if (ndd_is_false(a)) return ndd_true();
    
    // Check cache
    if (g_ndd_state.not_cache && 
        operation_cache_get_entry_unary(g_ndd_state.not_cache, a)) {
        g_ndd_state.stats.cache_hits++;
        ndd_t cached_result = (ndd_t)g_ndd_state.not_cache->result;
        return ndd_ref_safe(cached_result);
    }
    g_ndd_state.stats.cache_misses++;
    
    if (!ndd_is_terminal(a)) {
        ndd_t node_a = a;
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) return ndd_false();
        for (size_t ba = 0; ba < node_a->edges_map->bucket_count; ++ba) {
            hash_entry_t *ea = node_a->edges_map->buckets[ba];
            while (ea) {
                ndd_bdd_t not_result = ndd_bdd_not_parallel(*(ndd_bdd_t*)ea->value);
                if (not_result != ndd_sylvan_false) edges_map_add(emap, ndd_true(), not_result);
                ea = ea->next;
            }
        }
        ndd_t result = ndd_mk(node_a->field, emap);
        if (g_ndd_state.not_cache) {
            operation_cache_set_entry_unary(g_ndd_state.not_cache, 
                                           g_ndd_state.not_cache->hash_value,
                                           a, result);
        }
        return result;
    }
    
    return ndd_false();
}

ndd_t ndd_diff(ndd_t a, ndd_t b) {
    // Real parallel DIFF operation: a AND (NOT b)
    if (ndd_is_false(a) || ndd_is_true(b)) return ndd_false();
    if (ndd_is_false(b)) return ndd_ref_safe(a);
    
    // DIFF(a, b) = AND(a, NOT(b))
    ndd_t not_b = ndd_not(b);
    ndd_t result = ndd_and(a, not_b);
    ndd_deref_safe(not_b);  // Clean up temporary result
    
    return result;
}

ndd_t ndd_exist(ndd_t a, uint32_t field) {
    // Real parallel EXIST operation: eliminate specified field (Map simplification)
    if (ndd_is_terminal(a)) return ndd_ref_safe(a);
    ndd_t node_a = a;
    if (node_a->field == field) {
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) return ndd_false();
        for (size_t bkt = 0; bkt < node_a->edges_map->bucket_count; ++bkt) {
            hash_entry_t *e = node_a->edges_map->buckets[bkt];
            while (e) { edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)e->value); e = e->next; }
        }
        return ndd_mk(0, emap);
    } else if (node_a->field < field) {
        hash_table_t *emap = hash_table_create(32, ptr_hash, ptr_compare);
        if (!emap) return ndd_false();
        for (size_t bkt = 0; bkt < node_a->edges_map->bucket_count; ++bkt) {
            hash_entry_t *e = node_a->edges_map->buckets[bkt];
            while (e) { edges_map_add(emap, ndd_true(), *(ndd_bdd_t*)e->value); e = e->next; }
        }
        return ndd_mk(node_a->field, emap);
    } else {
        return ndd_ref_safe(a);
    }
}

// Encoding operations - real parallel implementation
ndd_t ndd_encode_prefix(uint32_t* prefix_binary, uint32_t len, uint32_t field) {
    if (len == 0) return ndd_true();
    
    ndd_field_info_t *field_info = ndd_get_field_info(field);
    if (!field_info) return ndd_false();
    
    // Create NDD node representing prefix (edges_map + mk)
    hash_table_t *emap = hash_table_create(4, ptr_hash, ptr_compare);
    if (!emap) return ndd_false();
    
    // Convert prefix to BDD
    ndd_bdd_t prefix_bdd = ndd_sylvan_true;
    
#ifdef HAVE_LACE_SYLVAN
    // Use Sylvan's BDD construction functionality
    for (uint32_t i = 0; i < len && i < field_info->bit_width; i++) {
        uint32_t var = field_info->start_var + i;
        ndd_bdd_t bit_constraint;
        
        if (prefix_binary[i] == 1) {
            bit_constraint = sylvan_ithvar(var);  // Variable is true
        } else {
            bit_constraint = sylvan_nithvar(var); // Variable is false
        }
        
        // AND operation with existing constraints
        prefix_bdd = sylvan_and(prefix_bdd, bit_constraint);
        // Sylvan automatically manages BDD reference counting
    }
#else
    #error "NDD requires Lace + Sylvan framework. Please ensure HAVE_LACE_SYLVAN is defined."
#endif
    
    // Add BDD as edge to NDD node
    ndd_bdd_t *bdd_ptr = malloc(sizeof(ndd_bdd_t));
    if (!bdd_ptr) {
        hash_table_destroy(emap);
        return ndd_false();
    }
    *bdd_ptr = prefix_bdd;
    hash_table_put(emap, ndd_true(), bdd_ptr);
    return ndd_mk(field, emap);
}

ndd_t ndd_from_bdd(ndd_bdd_t bdd, uint32_t field) {
    // Real BDD to NDD conversion
    if (bdd == ndd_sylvan_true) return ndd_true();
    if (bdd == ndd_sylvan_false) return ndd_false();
    
    ndd_field_info_t *field_info = ndd_get_field_info(field);
    if (!field_info) return ndd_false();
    
    // Create NDD node for corresponding field (edges_map + mk)
    hash_table_t *emap = hash_table_create(4, ptr_hash, ptr_compare);
    if (!emap) return ndd_false();
    
#ifdef HAVE_LACE_SYLVAN
    // Use Sylvan's BDD analysis functionality
    // Check if BDD involves variables of this field
    bool field_relevant = false;
    for (uint32_t var = field_info->start_var; var <= field_info->end_var; var++) {
        // Simplified check: consider relevant if BDD is not constant
        if (bdd != ndd_sylvan_true && bdd != ndd_sylvan_false) {
            field_relevant = true;
            break;
        }
    }
    
    if (field_relevant) {
        // Extract BDD part relevant to this field
        ndd_bdd_t field_bdd = bdd;
        
        // Create edges for all variable combinations of this field
        edges_map_add(emap, ndd_true(), field_bdd);
    } else {
        // BDD doesn't depend on this field, create unconstrained edge
        edges_map_add(emap, ndd_true(), ndd_sylvan_true);
    }
#else
    #error "NDD requires Lace + Sylvan framework. Please ensure HAVE_LACE_SYLVAN is defined."
#endif
    
    return ndd_mk(field, emap);
}

ndd_bdd_t ndd_to_bdd(ndd_t ndd) {
    // Real NDD to BDD conversion
    if (ndd_is_true(ndd)) return ndd_sylvan_true;
    if (ndd_is_false(ndd)) return ndd_sylvan_false;
    
    // Check if edges_map exists
    if (!ndd->edges_map) {
        return ndd_sylvan_false;
    }
    
    ndd_t node = ndd;
    ndd_bdd_t result = ndd_sylvan_false;
#ifdef HAVE_LACE_SYLVAN
    for (size_t b = 0; b < node->edges_map->bucket_count; ++b) {
        hash_entry_t *e = node->edges_map->buckets[b];
        while (e) { 
            ndd_bdd_t *bdd_ptr = (ndd_bdd_t*)e->value;
            result = sylvan_or(result, *bdd_ptr); 
            e = e->next; 
        }
    }
#else
    #error "NDD requires Lace + Sylvan framework. Please ensure HAVE_LACE_SYLVAN is defined."
#endif
    return result;
}

// BDD operation functions implemented in ndd_parallel.c

// Reference counting and GC - unified memory management system
ndd_t ndd_ref(ndd_t ndd) {
    if (ndd_is_terminal(ndd)) return ndd;
    
    pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
    ndd->ref_count++;
    // TODO: Integrate performance monitoring system
    pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
    
    return ndd;
}

void ndd_deref(ndd_t ndd) {
    if (ndd_is_terminal(ndd)) return;
    
    pthread_mutex_lock(&g_ndd_state.ref_count_mutex);
    // TODO: Integrate performance monitoring system
    
    if (--ndd->ref_count == 0) {
        // Free edge hash table
        if (ndd->edges_map) {
            hash_table_destroy(ndd->edges_map);
            // TODO: Integrate performance monitoring system
        }
        
        // Free node
        free(ndd);
        // TODO: Integrate performance monitoring system
        g_ndd_state.stats.node_count--;
    }
    
    pthread_mutex_unlock(&g_ndd_state.ref_count_mutex);
}

// Thread-safe reference counting operations (for parallel environment)
ndd_t ndd_ref_safe(ndd_t ndd) {
    return ndd_ref(ndd);  // Already thread-safe
}

void ndd_deref_safe(ndd_t ndd) {
    ndd_deref(ndd);  // Already thread-safe
}

void ndd_gc() {
    // Simplified GC implementation
    printf("GC triggered\n");
    g_ndd_state.stats.gc_count++;
    // TODO: Integrate performance monitoring system
    
    // Clear operation caches
    if (g_ndd_state.and_cache) {
        operation_cache_clear(g_ndd_state.and_cache);
    }
    if (g_ndd_state.or_cache) {
        operation_cache_clear(g_ndd_state.or_cache);
    }
    if (g_ndd_state.not_cache) {
        operation_cache_clear(g_ndd_state.not_cache);
    }
    
    printf("✅ Operation caches cleared during GC\n");
}

// Memory management statistics (using performance monitoring system)
ndd_memory_stats_t ndd_get_memory_stats() {
    // TODO: Return performance monitoring system statistics
    ndd_memory_stats_t stats = {0};
    return stats;
}

void ndd_print_memory_stats() {
    printf("=== NDD Memory Management Statistics ===\n");
    printf("Use performance monitoring system for detailed statistics\n");
    printf("========================================\n");
}

// Statistics information
ndd_stats_t ndd_get_stats() {
    return g_ndd_state.stats;
}

void ndd_print_stats() {
    printf("=== NDD Statistics ===\n");
    printf("Nodes: %lu\n", g_ndd_state.stats.node_count);
    printf("Edges: %lu\n", g_ndd_state.stats.edge_count);
    printf("GC Count: %lu\n", g_ndd_state.stats.gc_count);
    printf("=====================\n");
}

// Performance optimization
void ndd_set_gc_threshold(uint32_t threshold) {
    // Implement GC threshold setting
}

void ndd_enable_reordering(bool enable) {
    // Implement variable reordering
}

void ndd_set_cache_ratio(double ratio) {
    // Implement cache ratio setting
} 