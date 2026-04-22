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
#include "mtpndd_leaf_table.h"
#include "sylvan.h"
#include "sylvan_common.h"
#include "lace.h"
#include <stdio.h>
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
#include <time.h>
#endif

static void gc_internal(void);
static void grow_internal(void);
static bool mtpndd_nodetable_rehash(mtpndd_nodetable_t *table, size_t new_slot_count);

void mtpndd_gc_run_prehooks(void);
void mtpndd_gc_run_posthooks(void);

static void gcOrGrow(void);
static size_t mtpndd_gc_sweep(void);

static _Atomic bool g_mtpndd_gc_running = false;

/* ------------------------------------------------------------------ *
 * Slot probing helpers
 * ------------------------------------------------------------------ */

/* Spin briefly while another inserter publishes its slot. */
static inline void mtpndd_ot_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

void mtpndd_gc_before_sylvan(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_mtpndd_gc_running, &expected, true)) {
        // avoid gc loop
        return;
    }

    gc_internal();

    mtpndd_op_cache_clear(g_mtpndd_config.and_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.or_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.not_cache);

    atomic_store(&g_mtpndd_gc_running, false);
}

/**
 * Sylvan GC mark callback: walk all live MTPNDD nodes and mark their
 * BDD edge labels so that Sylvan's clear-and-mark phase preserves them.
 */
void mtpndd_gc_mark_bdd_labels(WorkerP *__lace_worker, Task *__lace_dq_head) {
    (void)__lace_dq_head;

    for (uint32_t field = 1; field <= g_mtpndd_config.field_count; ++field) {
        mtpndd_nodetable_t *nodetable = g_mtpndd_config.node_tables_by_field[field];
        if (!nodetable || !nodetable->slots) continue;

        size_t slot_count = nodetable->slot_count;
        for (size_t i = 0; i < slot_count; ++i) {
            mtpndd_edge_t *slot_edges = atomic_load_explicit(
                    &nodetable->slots[i].edges, memory_order_relaxed);
            if (!mtpndd_ot_is_live(slot_edges)) continue;

            mtpndd_node_t *node = nodetable->slots[i].node;
            if (!node) continue;

            mtpndd_edge_t *edges = node->edges;
            if (edges && edges->buckets) {
                size_t edge_bucket_count = edges->bucket_count
                    ? edges->bucket_count
                    : g_mtpndd_pal_config.edge_bucket_count;
                for (size_t eb = 0; eb < edge_bucket_count; ++eb) {
                    edge_bucket_entry_t *edge_entry = edges->buckets[eb];
                    while (edge_entry) {
                        mtpndd_bdd_t label = atomic_load_explicit(
                                &edge_entry->label, memory_order_relaxed);
                        if (label != sylvan_false && label != sylvan_true) {
                            CALL(mtbdd_gc_mark_rec, label);
                        }
                        edge_entry = edge_entry->next;
                    }
                }
            }
        }
    }
}


mtpndd_nodetable_t *mtpndd_nodetable_declare_field() {
    mtpndd_nodetable_t *table = (mtpndd_nodetable_t *)malloc(sizeof(mtpndd_nodetable_t));
    if (!table) return NULL;
    memset(table, 0, sizeof(mtpndd_nodetable_t));

    size_t slot_cnt = g_mtpndd_pal_config.nodetable_bucket_count;
    if (slot_cnt == 0) {
        slot_cnt = MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
    }
    // Cap initial slot count to limit virtual address space allocation.
    if (slot_cnt > (1u << 23)) slot_cnt = (1u << 23);
    if (slot_cnt < 16) slot_cnt = 16;
    slot_cnt = mtpndd_round_up_pow2(slot_cnt);
    table->slot_count = slot_cnt;
    table->mask = slot_cnt - 1;

    pthread_mutex_init(&table->rehash_mutex, NULL);

    table->slots = (mtpndd_nodetable_slot_t *)calloc(slot_cnt, sizeof(mtpndd_nodetable_slot_t));
    if (!table->slots) {
        pthread_mutex_destroy(&table->rehash_mutex);
        free(table);
        return NULL;
    }
    atomic_init(&table->entry_count, 0);
    atomic_init(&table->tombstone_count, 0);
    table->load_threshold = slot_cnt - (slot_cnt >> 2); /* 75% */

    return table;
}

void mtpndd_nodetable_free(mtpndd_nodetable_t *table) {
    if (!table) return;
    if (table->slots) {
        free(table->slots);
        table->slots = NULL;
    }
    pthread_mutex_destroy(&table->rehash_mutex);
    free(table);
}

/* ------------------------------------------------------------------ *
 * Lock-free lookup
 * ------------------------------------------------------------------ */
mtpndd_node_t *find_node_in_nodetable(mtpndd_nodetable_t *nodetable, mtpndd_edge_t *edges) {
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    struct timespec phase_start = {0};
    struct timespec phase_end = {0};
    clock_gettime(CLOCK_MONOTONIC, &phase_start);
#endif

    if (!nodetable || !nodetable->slots || !edges) {
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
        clock_gettime(CLOCK_MONOTONIC, &phase_end);
        MTPNDD_STAT_ADD(nodetable_bucket_scan_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
        MTPNDD_STAT_ADD(nodetable_lookup_misses, 1);
#endif
        return NULL;
    }

    uint64_t cached_hash = edges->cached_hash;
    size_t mask = nodetable->mask;
    size_t idx = (size_t)(cached_hash & mask);

    mtpndd_node_t *found = NULL;
    size_t max_probes = nodetable->slot_count;
    for (size_t probes = 0; probes < max_probes; ++probes) {
        mtpndd_edge_t *cur = atomic_load_explicit(
                &nodetable->slots[idx].edges, memory_order_acquire);
        if (cur == NULL) {
            /* definitively empty -> miss. */
            break;
        }
        if (cur == MTPNDD_OT_PENDING) {
            mtpndd_ot_cpu_relax();
            continue; /* re-read same slot */
        }
        if (cur == MTPNDD_OT_TOMBSTONE) {
            idx = (idx + 1) & mask;
            continue;
        }
        /* live entry */
        if (nodetable->slots[idx].cached_hash == cached_hash) {
            if (cur == edges || nodetable_edges_equal(cur, edges)) {
                found = nodetable->slots[idx].node;
                break;
            }
        }
        idx = (idx + 1) & mask;
    }

#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    clock_gettime(CLOCK_MONOTONIC, &phase_end);
    MTPNDD_STAT_ADD(nodetable_bucket_scan_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
    if (found) {
        MTPNDD_STAT_ADD(nodetable_lookup_hits, 1);
    } else {
        MTPNDD_STAT_ADD(nodetable_lookup_misses, 1);
    }
#endif
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
    if (node->ref_count == UINT32_MAX) {
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
    if (node->ref_count == UINT32_MAX) {
        return MTPNDD_SUCCESS;
    }
    atomic_fetch_sub(&node->ref_count, 1);
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_protect(mtpndd_t *node) {
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_NULL_POINTER, __func__, __LINE__);
        return MTPNDD_ERROR_NULL_POINTER;
    }
    atomic_init(&node->ref_count, UINT32_MAX);
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

/* ------------------------------------------------------------------ *
 * CAS-based insert
 * ------------------------------------------------------------------ */
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
        size_t bucket_cnt = (edges && edges->buckets) ? (edges->bucket_count ? edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count) : 0;
        for (size_t i = 0; i < bucket_cnt && !only_entry; ++i) {
            edge_bucket_entry_t *head = edges->buckets[i];
            if (head) {
                only_entry = head;
            }
        }
        if (only_entry && atomic_load_explicit(&only_entry->label, memory_order_acquire) == sylvan_true) {
            *result = only_entry->child;
            return;
        }
    }

    edges->cached_hash = mtpndd_edge_map_compute_hash(edges);

    mtpndd_nodetable_t *nodetable = g_mtpndd_config.node_tables_by_field[field];

    /* Fast path: lookup first. */
    mtpndd_node_t *existing = find_node_in_nodetable(nodetable, edges);
    edge_bucket_entry_t *entry = NULL;
    if (existing) {
        /* Caller-supplied edges map is redundant; drop label refs it carries. */
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            sylvan_deref(label);
        }
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
        MTPNDD_STAT_ADD(nodes_reused_total, 1);
#endif
        *result = existing;
        return;
    }

    /* Slow path: try to insert a new node.
     * 1. ref children   2. maybe GC   3. alloc node   4. probe+CAS slots. */
    FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
        if (!mtpndd_is_terminal(entry->child)) {
            mtpndd_ref(entry->child);
        }
    }

    size_t live_before = atomic_load_explicit(&nodetable->entry_count, memory_order_relaxed);
    size_t tomb_before = atomic_load_explicit(&nodetable->tombstone_count, memory_order_relaxed);
    bool need_gc = (g_mtpndd_node_count >= g_mtpndd_pal_config.mtpndd_nodetable_size)
                || (live_before + tomb_before >= nodetable->load_threshold);
    if (need_gc) {
        gcOrGrow();
        /* gcOrGrow may have rehashed; nodetable pointer is still valid, but
         * mask/slots have been swapped. find_node_in_nodetable above may have
         * returned NULL based on the old layout. Re-lookup under the new one. */
        nodetable = g_mtpndd_config.node_tables_by_field[field];
        existing = find_node_in_nodetable(nodetable, edges);
        if (existing) {
            FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
                if (!mtpndd_is_terminal(entry->child)) {
                    mtpndd_deref(entry->child);
                }
                mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
                sylvan_deref(label);
            }
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
            MTPNDD_STAT_ADD(nodes_reused_total, 1);
#endif
            *result = existing;
            return;
        }
    }

    mtpndd_node_t *node = mtpndd_memory_acquire_node();
    if (!node) {
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return;
    }
    node->field_id = field;
    node->edges = edges;
    atomic_init(&node->ref_count, 0);

    uint64_t cached_hash = edges->cached_hash;
    size_t mask = nodetable->mask;
    size_t idx = (size_t)(cached_hash & mask);

    /* Probe for either a matching live slot (duplicate inserted by
     * another thread) or an empty/tombstone slot we can claim. */
    size_t max_probes = nodetable->slot_count;
    bool done = false;
    mtpndd_node_t *duplicate_of = NULL;
    for (size_t probes = 0; probes < max_probes && !done; ++probes) {
        mtpndd_edge_t *cur = atomic_load_explicit(
                &nodetable->slots[idx].edges, memory_order_acquire);

        if (cur == MTPNDD_OT_PENDING) {
            mtpndd_ot_cpu_relax();
            continue;
        }

        if (cur == NULL || cur == MTPNDD_OT_TOMBSTONE) {
            mtpndd_edge_t *expected = cur;
            if (atomic_compare_exchange_strong_explicit(
                        &nodetable->slots[idx].edges,
                        &expected, MTPNDD_OT_PENDING,
                        memory_order_acq_rel, memory_order_acquire)) {
                /* Won the slot. Publish. */
                nodetable->slots[idx].node = node;
                nodetable->slots[idx].cached_hash = cached_hash;
                atomic_store_explicit(
                        &nodetable->slots[idx].edges, edges,
                        memory_order_release);
                if (cur == MTPNDD_OT_TOMBSTONE) {
                    atomic_fetch_sub_explicit(&nodetable->tombstone_count, 1, memory_order_relaxed);
                }
                atomic_fetch_add_explicit(&nodetable->entry_count, 1, memory_order_relaxed);
                done = true;
                break;
            }
            /* Lost CAS: re-read this slot. */
            continue;
        }

        /* live entry */
        if (nodetable->slots[idx].cached_hash == cached_hash) {
            if (cur == edges || nodetable_edges_equal(cur, edges)) {
                duplicate_of = nodetable->slots[idx].node;
                done = true;
                break;
            }
        }
        idx = (idx + 1) & mask;
    }

    if (!done) {
        /* Table full (probe wrapped) — this shouldn't happen if load
         * stays under threshold. Fall back to triggering GC/grow. */
        MTPNDD_LOG_ERROR("[MTPNDD ERROR] nodetable probe overflow for field=%u — forcing gcOrGrow\n", field);
        gcOrGrow();
        /* Retry after grow. For simplicity, give up if still impossible. */
        nodetable = g_mtpndd_config.node_tables_by_field[field];
        mask = nodetable->mask;
        idx = (size_t)(cached_hash & mask);
        max_probes = nodetable->slot_count;
        for (size_t probes = 0; probes < max_probes && !done; ++probes) {
            mtpndd_edge_t *cur = atomic_load_explicit(
                    &nodetable->slots[idx].edges, memory_order_acquire);
            if (cur == MTPNDD_OT_PENDING) { mtpndd_ot_cpu_relax(); continue; }
            if (cur == NULL || cur == MTPNDD_OT_TOMBSTONE) {
                mtpndd_edge_t *expected = cur;
                if (atomic_compare_exchange_strong_explicit(
                            &nodetable->slots[idx].edges,
                            &expected, MTPNDD_OT_PENDING,
                            memory_order_acq_rel, memory_order_acquire)) {
                    nodetable->slots[idx].node = node;
                    nodetable->slots[idx].cached_hash = cached_hash;
                    atomic_store_explicit(
                            &nodetable->slots[idx].edges, edges,
                            memory_order_release);
                    if (cur == MTPNDD_OT_TOMBSTONE) {
                        atomic_fetch_sub_explicit(&nodetable->tombstone_count, 1, memory_order_relaxed);
                    }
                    atomic_fetch_add_explicit(&nodetable->entry_count, 1, memory_order_relaxed);
                    done = true;
                    break;
                }
                continue;
            }
            if (nodetable->slots[idx].cached_hash == cached_hash) {
                if (cur == edges || nodetable_edges_equal(cur, edges)) {
                    duplicate_of = nodetable->slots[idx].node;
                    done = true;
                    break;
                }
            }
            idx = (idx + 1) & mask;
        }
    }

    if (duplicate_of) {
        /* Another thread inserted an equivalent node first. Undo our
         * speculative children-ref adds and drop our node allocation. */
        mtpndd_memory_release_node(node);
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            if (!mtpndd_is_terminal(entry->child)) {
                mtpndd_deref(entry->child);
            }
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            sylvan_deref(label);
        }
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
        MTPNDD_STAT_ADD(nodetable_collision_total, 1);
#endif
        *result = duplicate_of;
        return;
    }

    if (!done) {
        /* Still couldn't insert after grow — hard failure. */
        mtpndd_memory_release_node(node);
        FOR_EACH_ENTRY_IN_ALL_BUCKETS(edges, entry) {
            if (!mtpndd_is_terminal(entry->child)) {
                mtpndd_deref(entry->child);
            }
            mtpndd_bdd_t label = atomic_load_explicit(&entry->label, memory_order_relaxed);
            sylvan_deref(label);
        }
        mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__);
        return;
    }

    __atomic_add_fetch(&g_mtpndd_node_count, 1, __ATOMIC_RELAXED);
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    MTPNDD_STAT_ADD(nodes_created_total, 1);
#endif
    *result = node;
}

/* ------------------------------------------------------------------ *
 * GC sweep and rehash (runs stop-the-world inside Sylvan NEWFRAME)
 * ------------------------------------------------------------------ */
static void gcOrGrow(void) {
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    struct timespec gc_timer_start = {0};
    clock_gettime(CLOCK_MONOTONIC, &gc_timer_start);
    mtpndd_log_memory_pools("pre-gc");
#endif

    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_mtpndd_gc_running, &expected, true)) {
        return;
    }

    gc_internal();

    if (g_mtpndd_pal_config.mtpndd_nodetable_size - g_mtpndd_node_count
            < g_mtpndd_pal_config.quick_growth_threshold * g_mtpndd_pal_config.mtpndd_nodetable_size) {
        grow_internal();
    }

    /* If any per-table tombstone/live ratio is high, still rehash that
     * table to keep probe chains short. */
    for (uint32_t field = 1; field <= g_mtpndd_config.field_count; ++field) {
        mtpndd_nodetable_t *t = g_mtpndd_config.node_tables_by_field[field];
        if (!t) continue;
        size_t live = atomic_load_explicit(&t->entry_count, memory_order_relaxed);
        size_t tomb = atomic_load_explicit(&t->tombstone_count, memory_order_relaxed);
        if (live + tomb >= t->load_threshold) {
            size_t target = t->slot_count;
            /* Grow only if live alone is over half of threshold; otherwise
             * just rehash to the same size to drop tombstones. */
            if (live * 2 >= t->load_threshold) {
                target = t->slot_count * 2;
            }
            pthread_mutex_lock(&t->rehash_mutex);
            (void)mtpndd_nodetable_rehash(t, target);
            pthread_mutex_unlock(&t->rehash_mutex);
        }
    }

    mtpndd_op_cache_clear(g_mtpndd_config.and_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.or_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.not_cache);

    sylvan_gc();

    atomic_store(&g_mtpndd_gc_running, false);

#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    mtpndd_log_memory_pools("post-gc");
    struct timespec gc_timer_end = {0};
    clock_gettime(CLOCK_MONOTONIC, &gc_timer_end);
    MTPNDD_STAT_ADD(gc_pause_time_ns, mtpndd_timespec_diff_ns(&gc_timer_start, &gc_timer_end));
#endif
}

static void gc_internal(void) {
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    __atomic_add_fetch(&g_mtpndd_stats.gc_runs, 1, __ATOMIC_RELAXED);
#endif
    mtpndd_gc_run_prehooks();

    size_t reclaimed = mtpndd_gc_sweep();
    size_t leaves_reclaimed = mtpndd_leaf_gc();

    if (reclaimed > 0) {
        __atomic_sub_fetch(&g_mtpndd_node_count, reclaimed, __ATOMIC_RELAXED);
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
        MTPNDD_STAT_SET(nodes_collected_last, reclaimed);
#endif
    }
    (void)leaves_reclaimed;

    mtpndd_gc_run_posthooks();
}

/* Called only during GC stop-the-world — no concurrent readers/writers. */
static size_t mtpndd_gc_sweep(void) {
    size_t reclaimed = 0;

    for (uint32_t field = 1; field <= g_mtpndd_config.field_count; ++field) {
        mtpndd_nodetable_t *nodetable = g_mtpndd_config.node_tables_by_field[field];
        if (!nodetable || !nodetable->slots) continue;

        size_t slot_count = nodetable->slot_count;
        for (size_t i = 0; i < slot_count; ++i) {
            mtpndd_nodetable_slot_t *slot = &nodetable->slots[i];
            mtpndd_edge_t *cur = atomic_load_explicit(&slot->edges, memory_order_relaxed);
            if (!mtpndd_ot_is_live(cur)) continue;

            mtpndd_node_t *node = slot->node;
            uint32_t refc = atomic_load_explicit(&node->ref_count, memory_order_relaxed);
            if (refc != 0) continue;

            /* Release children refs and free the edge map + node. */
            mtpndd_edge_t *edges = node->edges;
            if (edges && edges->buckets) {
                size_t edge_bucket_count = edges->bucket_count ? edges->bucket_count : g_mtpndd_pal_config.edge_bucket_count;
                for (size_t eb = 0; eb < edge_bucket_count; ++eb) {
                    for (edge_bucket_entry_t *ee = edges->buckets[eb]; ee; ee = ee->next) {
                        mtpndd_node_t *child = ee->child;
                        if (child && !mtpndd_is_terminal(child)) {
                            mtpndd_deref(child);
                        }
                    }
                }
            }
            mtpndd_edge_map_free(edges);
            mtpndd_memory_release_node(node);

            atomic_store_explicit(&slot->edges, MTPNDD_OT_TOMBSTONE, memory_order_relaxed);
            slot->node = NULL;
            slot->cached_hash = 0;
            atomic_fetch_add_explicit(&nodetable->tombstone_count, 1, memory_order_relaxed);
            size_t prev = atomic_load_explicit(&nodetable->entry_count, memory_order_relaxed);
            if (prev > 0) {
                atomic_fetch_sub_explicit(&nodetable->entry_count, 1, memory_order_relaxed);
            }
            reclaimed++;
        }
    }

    return reclaimed;
}

/* Rebuild slot array. Caller must hold rehash_mutex. Intended to run
 * under GC stop-the-world, so no concurrent readers/writers are allowed. */
static bool mtpndd_nodetable_rehash(mtpndd_nodetable_t *table, size_t new_slot_count) {
    if (!table || new_slot_count == 0) return false;
    if (new_slot_count < 16) new_slot_count = 16;
    new_slot_count = mtpndd_round_up_pow2(new_slot_count);

    mtpndd_nodetable_slot_t *new_slots = (mtpndd_nodetable_slot_t *)calloc(
            new_slot_count, sizeof(mtpndd_nodetable_slot_t));
    if (!new_slots) return false;

    size_t new_mask = new_slot_count - 1;
    size_t old_count = table->slot_count;
    size_t live = 0;
    for (size_t i = 0; i < old_count; ++i) {
        mtpndd_edge_t *cur = atomic_load_explicit(&table->slots[i].edges, memory_order_relaxed);
        if (!mtpndd_ot_is_live(cur)) continue;

        uint64_t h = table->slots[i].cached_hash;
        size_t idx = (size_t)(h & new_mask);
        while (atomic_load_explicit(&new_slots[idx].edges, memory_order_relaxed) != NULL) {
            idx = (idx + 1) & new_mask;
        }
        new_slots[idx].node = table->slots[i].node;
        new_slots[idx].cached_hash = h;
        atomic_store_explicit(&new_slots[idx].edges, cur, memory_order_relaxed);
        live++;
    }

    free(table->slots);
    table->slots = new_slots;
    table->slot_count = new_slot_count;
    table->mask = new_mask;
    table->load_threshold = new_slot_count - (new_slot_count >> 2);
    atomic_store_explicit(&table->entry_count, live, memory_order_relaxed);
    atomic_store_explicit(&table->tombstone_count, 0, memory_order_relaxed);
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    MTPNDD_STAT_ADD(nodetable_rehash_total, 1);
    MTPNDD_STAT_MAX(nodetable_max_buckets, new_slot_count);
#endif
    return true;
}

static void grow_internal(void) {
    size_t new_capacity = g_mtpndd_pal_config.mtpndd_nodetable_size * 2;
    for (uint32_t field = 1; field <= g_mtpndd_config.field_count; ++field) {
        mtpndd_nodetable_t *table = g_mtpndd_config.node_tables_by_field[field];
        if (!table) continue;
        size_t new_slot_count = table->slot_count ? table->slot_count * 2
                                                  : MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
        pthread_mutex_lock(&table->rehash_mutex);
        bool ok = mtpndd_nodetable_rehash(table, new_slot_count);
        pthread_mutex_unlock(&table->rehash_mutex);
        if (!ok) {
            MTPNDD_LOG_ERROR("[MTPNDD ERROR] nodetable rehash failed for field=%u (slot target=%zu)\n",
                    field, new_slot_count);
            return;
        }
    }
    g_mtpndd_pal_config.mtpndd_nodetable_size = new_capacity;
    size_t config_bucket = g_mtpndd_pal_config.nodetable_bucket_count;
    if (config_bucket == 0) config_bucket = MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
    g_mtpndd_pal_config.nodetable_bucket_count = config_bucket * 2;
}
