// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mtpndd_nodetable.h"
#include "mtpndd_node.h"
#include "mtpndd_memory_pool.h"
#include "mtpndd_operation_cache.h"
#include "sylvan.h"
#include "sylvan_common.h"
#include "lace.h"
#include <stdio.h>
#ifdef ENABLE_RECORDING
#include <time.h>
#endif

static void gc_internal(void);
static void grow_internal(void);
static bool mtpndd_nodetable_rehash(mtpndd_nodetable_t *table, size_t new_bucket_count);

static void gcOrGrow(void);
static size_t mtpndd_gc_collect_roots(mtpndd_node_t ***roots_out);
static void mtpndd_gc_release_roots(mtpndd_node_t **roots, size_t count);
static size_t mtpndd_gc_sweep(void);
static void mtpndd_release_node(mtpndd_nodetable_t *table, size_t bucket_idx, mtpndd_nodetable_bucket_entry_t *entry, mtpndd_node_t *node);

#ifdef ENABLE_RECORDING
static void mtpndd_log_memory_pools(const char *phase) {
    mtpndd_memory_pool_stats_t stats = {0};
    mtpndd_memory_pools_snapshot(&stats);
    fprintf(stdout,
            "[MTPNDD MEM] %s node slabs=%zu in_use=%zu slabCap=%zu | edge_entry slabs=%zu in_use=%zu | nodetable_entry slabs=%zu in_use=%zu | edge_map slabs=%zu in_use=%zu\n",
            phase ? phase : "unknown",
            stats.node_slabs, stats.node_in_use, stats.node_capacity_per_slab,
            stats.edge_entry_slabs, stats.edge_entry_in_use,
            stats.nodetable_entry_slabs, stats.nodetable_entry_in_use,
            stats.edge_map_slabs, stats.edge_map_in_use);
    fflush(stdout);
}
#endif

mtpndd_nodetable_t *mtpndd_nodetable_declare_field() {
    mtpndd_nodetable_t *table = (mtpndd_nodetable_t *)malloc(sizeof(mtpndd_nodetable_t));
    if (!table) return NULL;
    memset(table, 0, sizeof(mtpndd_nodetable_t));
    size_t bucket_cnt = g_mtpndd_pal_config.nodetable_bucket_count;
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
    for (size_t i = 0; i < bucket_cnt; i++) {
        table->buckets[i] = NULL;
    }

    return table;
}

mtpndd_node_t *find_node_in_nodetable(mtpndd_nodetable_t *nodetable, mtpndd_edge_t *edges) {
    size_t hash = NODETABLE_HASH_VAL(edges, nodetable);
    mtpndd_node_t *found = NULL;

    for (mtpndd_nodetable_bucket_entry_t *entry = nodetable->buckets[hash];
         entry; entry = entry->next) {
        if (NODETABLE_BUCKET_ENTRY_EQUAL(entry, edges)) {
            found = entry->node;
            break;
        }
    }

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

void mtpndd_mk(uint32_t field, mtpndd_edge_t *edges, mtpndd_node_t **result) {
    if (!result) {
        mtpndd_set_error(MTPNDD_ERROR_NULL_POINTER, __func__, __LINE__);
        return;
    }
    *result = NULL;
    if (edges->edge_count == 0) {
        *result = &MTPNDD_FALSE;
        return;
    } else if (edges->edge_count == 1) {
        edge_bucket_entry_t *only_entry = NULL;
        size_t bucket_cnt = (edges && edges->buckets) ? g_mtpndd_pal_config.edge_bucket_count : 0;
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
            return;
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
#ifdef ENABLE_RECORDING
        MTPNDD_STAT_ADD(nodes_reused_total, 1);
#endif
        *result = node;
        return;
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
    node = mtpndd_memory_acquire_node();
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return;
    }
    node->field_id = field;
    node->edges = edges;
    atomic_init(&node->ref_count, 0);
    // 4. insert into nodetable
    size_t hash = NODETABLE_HASH_VAL(edges, nodetable);
    mtpndd_nodetable_bucket_entry_t *new_entry = mtpndd_memory_acquire_nodetable_entry();
    if (!new_entry) {
        mtpndd_memory_release_node(node);
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return;
    }
    new_entry->edges = edges;
    new_entry->node = node;
    mtpndd_nodetable_bucket_entry_t *existing_entry = nodetable->buckets[hash];
#ifdef ENABLE_RECORDING
    bool bucket_had_entries = existing_entry != NULL;
#endif
    while (existing_entry) {
        if (NODETABLE_BUCKET_ENTRY_EQUAL(existing_entry, edges)) {
            break;
        }
        existing_entry = existing_entry->next;
    }

    if (existing_entry) {
        mtpndd_node_t *existing_node = existing_entry->node;
        mtpndd_memory_release_nodetable_entry(new_entry);
        mtpndd_memory_release_node(node);
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            if (!mtpndd_is_terminal(entry->child)) {
                mtpndd_deref(entry->child);
            }
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            sylvan_deref(label);
        }
#ifdef ENABLE_RECORDING
        if (bucket_had_entries) {
            MTPNDD_STAT_ADD(nodetable_collision_total, 1);
        }
        MTPNDD_STAT_ADD(nodes_reused_total, 1);
#endif
        *result = existing_node;
        return;
    }

    new_entry->next = nodetable->buckets[hash];
    new_entry->prev = NULL;
    if (nodetable->buckets[hash]) {
        nodetable->buckets[hash]->prev = new_entry;
    }
    nodetable->buckets[hash] = new_entry;
    __atomic_add_fetch(&g_mtpndd_stats.node_count, 1, __ATOMIC_RELAXED);
#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(nodes_created_total, 1);
    if (bucket_had_entries) {
        MTPNDD_STAT_ADD(nodetable_collision_total, 1);
    }
#endif
    *result = node;
}

static void gcOrGrow(void) {
#ifdef ENABLE_RECORDING
    struct timespec gc_timer_start = {0};
    clock_gettime(CLOCK_MONOTONIC, &gc_timer_start);

    fprintf(stdout,
            "[MTPNDD DEBUG] gcOrGrow start node_count=%zu capacity=%zu threshold=%.2f\n",
            (size_t)g_mtpndd_stats.node_count,
            (size_t)g_mtpndd_pal_config.mtpndd_nodetable_size,
            g_mtpndd_pal_config.quick_growth_threshold);
    fflush(stdout);
    mtpndd_gc_run_prehooks();
    mtpndd_log_memory_pools("pre-gc");
#endif

    bool suspended_workers = false;
    if (lace_workers() > 0) {
        lace_suspend();
        suspended_workers = true;
    }

    gc_internal();

    if (g_mtpndd_pal_config.mtpndd_nodetable_size - g_mtpndd_stats.node_count
            < g_mtpndd_pal_config.quick_growth_threshold * g_mtpndd_pal_config.mtpndd_nodetable_size) {
#ifdef ENABLE_RECORDING
        fprintf(stdout, "[MTPNDD DEBUG] triggering grow (node_count=%zu capacity=%zu)\n",
                (size_t)g_mtpndd_stats.node_count,
                (size_t)g_mtpndd_pal_config.mtpndd_nodetable_size);
        fflush(stdout);
#endif

        grow_internal();
    }

    // clear operation caches after mutating nodetables, before resuming user code
    mtpndd_op_cache_clear(g_mtpndd_config.and_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.or_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.not_cache);

    if (suspended_workers) {
        lace_resume();
    }

    sylvan_gc();

#ifdef ENABLE_RECORDING
    mtpndd_gc_run_posthooks();
    mtpndd_log_memory_pools("post-gc");
    fprintf(stdout,
            "[MTPNDD DEBUG] gcOrGrow end node_count=%zu capacity=%zu\n",
            (size_t)g_mtpndd_stats.node_count,
            (size_t)g_mtpndd_pal_config.mtpndd_nodetable_size);
    fflush(stdout);

    struct timespec gc_timer_end = {0};
    clock_gettime(CLOCK_MONOTONIC, &gc_timer_end);
    MTPNDD_STAT_ADD(gc_pause_time_ns, mtpndd_timespec_diff_ns(&gc_timer_start, &gc_timer_end));
#endif
}

static void gc_internal(void) {
#ifdef ENABLE_RECORDING
    __atomic_add_fetch(&g_mtpndd_stats.gc_runs, 1, __ATOMIC_RELAXED);
#endif
    mtpndd_gc_run_prehooks();

    mtpndd_node_t **gc_roots = NULL;
    size_t gc_root_count = mtpndd_gc_collect_roots(&gc_roots);
    size_t reclaimed = mtpndd_gc_sweep();
    mtpndd_gc_release_roots(gc_roots, gc_root_count);

    if (reclaimed > 0) {
        __atomic_sub_fetch(&g_mtpndd_stats.node_count, reclaimed, __ATOMIC_RELAXED);
        
#ifdef ENABLE_RECORDING
        MTPNDD_STAT_SET(nodes_collected_last, reclaimed);
#endif
    }
}

static size_t mtpndd_gc_collect_roots(mtpndd_node_t ***roots_out) {
    if (roots_out == NULL) {
        return 0;
    }
    *roots_out = NULL;

    mtpndd_gc_protect_t *gc_protect = g_mtpndd_config.gcProtect;
    if (!gc_protect || !gc_protect->buckets || gc_protect->bucket_count == 0) {
        return 0;
    }

    mtpndd_node_t **buffer = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t bucket_count = gc_protect->bucket_count;

    for (size_t i = 0; i < bucket_count; ++i) {
        gc_protect_entry_t *entry = gc_protect->buckets[i];
        while (entry) {
            mtpndd_node_t *node = entry->node;
            if (node) {
                mtpndd_ref(node);
                if (count == capacity) {
                    size_t new_capacity = capacity ? capacity * 2 : 64;
                    mtpndd_node_t **new_buffer = (mtpndd_node_t **)realloc(buffer, new_capacity * sizeof(mtpndd_node_t *));
                    if (!new_buffer) {
                        mtpndd_gc_release_roots(buffer, count);
                        *roots_out = NULL;
                        return 0;
                    }
                    buffer = new_buffer;
                    capacity = new_capacity;
                }
                buffer[count++] = node;
            }
            entry = entry->next;
        }
    }

    *roots_out = buffer;
    return count;
}

static void mtpndd_gc_release_roots(mtpndd_node_t **roots, size_t count) {
    if (!roots) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        mtpndd_node_t *node = roots[i];
        if (node) {
            mtpndd_deref(node);
        }
    }
    free(roots);
}

static void mtpndd_release_node(mtpndd_nodetable_t *table, size_t bucket_idx, mtpndd_nodetable_bucket_entry_t *entry, mtpndd_node_t *node) {
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        table->buckets[bucket_idx] = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }

    mtpndd_edge_t *edges = node->edges;
    if (edges && edges->buckets) {
        size_t edge_bucket_count = g_mtpndd_pal_config.edge_bucket_count;
        for (size_t eb = 0; eb < edge_bucket_count; ++eb) {
            edge_bucket_entry_t *head = edges->buckets[eb];
            if (!head) {
                continue;
            }
            edge_bucket_entry_t *edge_entry = head->next;
            while (edge_entry && edge_entry != head) {
                mtpndd_node_t *child = edge_entry->child;
                if (child && !mtpndd_is_terminal(child)) {
                    mtpndd_deref(child);
                }
                edge_entry = edge_entry->next;
            }
        }
    }
    mtpndd_edge_map_free(edges);

    mtpndd_memory_release_node(node);
    mtpndd_memory_release_nodetable_entry(entry);
}

static size_t mtpndd_gc_sweep(void) {
    size_t reclaimed = 0;

    for (uint32_t field = 1; field <= g_mtpndd_config.field_count; ++field) {
        mtpndd_nodetable_t *nodetable = g_mtpndd_config.node_tables_by_field[field];
        if (!nodetable) {
            continue;
        }

        size_t bucket_count = nodetable->nodetable_bucket_count;
        for (size_t i = 0; i < bucket_count; ++i) {

            mtpndd_nodetable_bucket_entry_t *entry = nodetable->buckets[i];
            while (entry) {
                mtpndd_nodetable_bucket_entry_t *next_entry = entry->next;
                mtpndd_node_t *node = entry->node;
                uint64_t refc = atomic_load_explicit(&node->ref_count, memory_order_relaxed);
                if (refc == 0) {
                    mtpndd_release_node(nodetable, i, entry, node);
                    reclaimed++;
                }
                entry = next_entry;
            }
        }
    }

    return reclaimed;
}

static bool mtpndd_nodetable_rehash(mtpndd_nodetable_t *table, size_t new_bucket_count) {
    if (!table || new_bucket_count == 0) {
        return false;
    }

    mtpndd_nodetable_bucket_entry_t **new_buckets =
            (mtpndd_nodetable_bucket_entry_t **)calloc(new_bucket_count, sizeof(*new_buckets));
    if (!new_buckets) {
        return false;
    }

    size_t old_count = table->nodetable_bucket_count;
    for (size_t i = 0; i < old_count; ++i) {
        mtpndd_nodetable_bucket_entry_t *entry = table->buckets[i];
        while (entry) {
            mtpndd_nodetable_bucket_entry_t *next_entry = entry->next;
            size_t hash = nodetable_hash_edges_with_bucket_count(entry->edges, new_bucket_count);
            entry->prev = NULL;
            entry->next = new_buckets[hash];
            if (entry->next) {
                entry->next->prev = entry;
            }
            new_buckets[hash] = entry;
            entry = next_entry;
        }
        table->buckets[i] = NULL;
    }

    free(table->buckets);
    table->buckets = new_buckets;
    table->nodetable_bucket_count = new_bucket_count;
    return true;
}

static void grow_internal(void) {
    size_t new_capacity = g_mtpndd_pal_config.mtpndd_nodetable_size * 2;
    for (uint32_t field = 1; field <= g_mtpndd_config.field_count; ++field) {
        mtpndd_nodetable_t *table = g_mtpndd_config.node_tables_by_field[field];
        if (!table) continue;
        size_t new_bucket_count = table->nodetable_bucket_count ? table->nodetable_bucket_count * 2
                                                                : MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
        if (!mtpndd_nodetable_rehash(table, new_bucket_count)) {
            fprintf(stderr, "[MTPNDD ERROR] nodetable rehash failed for field=%u (bucket target=%zu)\n",
                    field, new_bucket_count);
            fflush(stderr);
            return;
        }
        fprintf(stdout, "[MTPNDD DEBUG] field=%u rehashed to buckets=%zu\n",
                field, new_bucket_count);
        fflush(stdout);
    }
    g_mtpndd_pal_config.mtpndd_nodetable_size = new_capacity;
    size_t config_bucket = g_mtpndd_pal_config.nodetable_bucket_count;
    if (config_bucket == 0) {
        config_bucket = MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
    }
    g_mtpndd_pal_config.nodetable_bucket_count = config_bucket * 2;
    fprintf(stdout, "[MTPNDD DEBUG] nodetable capacity doubled to %zu (config buckets=%zu)\n",
            new_capacity, g_mtpndd_pal_config.nodetable_bucket_count);
    fflush(stdout);
}
