// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_nodetable.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "sylvan.h"
#include "mtpndd_operation_cache.h"

mtpndd_nodetable_t g_mtpndd_nodetable = {0};

// Atomic flag to prevent GC recursion
static _Atomic bool g_mtpndd_gc_running = false;

void mtpndd_gc_before_sylvan(WorkerP *worker, Task *task) {
    // Unused parameters (required by Sylvan GC hook signature)
    (void)worker;
    (void)task;

    // Use atomic CAS to prevent recursive GC calls
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_mtpndd_gc_running, &expected, true)) {
        return;  // Another GC already running
    }

    // Clear MTPNDD operation caches
    mtpndd_op_cache_clear(g_mtpndd_config.and_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.or_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.not_cache);

    atomic_store(&g_mtpndd_gc_running, false);
}

// Cache-line-aware probing (Sylvan-style): probe all slots in the current cache line first,
// then jump to the next cache line using a key-dependent step.
#define MTPNDD_HASH_CL_SLOTS (MTPNDD_CACHELINE_BYTES / sizeof(uint64_t))
_Static_assert(MTPNDD_HASH_CL_SLOTS >= 2, "MTPNDD_HASH_CL_SLOTS too small");
_Static_assert((MTPNDD_HASH_CL_SLOTS & (MTPNDD_HASH_CL_SLOTS - 1)) == 0, "MTPNDD_HASH_CL_SLOTS must be power of two");

static const size_t MTPNDD_HASH_CL_MASK = ~((size_t)MTPNDD_HASH_CL_SLOTS - 1);
static const size_t MTPNDD_HASH_CL_MASK_R = (size_t)MTPNDD_HASH_CL_SLOTS - 1;

static inline size_t mtpndd_hash_probe_threshold(size_t table_size) {
    if (table_size == 0) return 0;
    // Sylvan: threshold = 192 - 2*clzll(table_size)
    uint64_t v = (uint64_t)table_size;
    size_t threshold = 192u - 2u * (size_t)__builtin_clzll(v);
    size_t line_count = table_size / (size_t)MTPNDD_HASH_CL_SLOTS;
    if (line_count == 0) line_count = 1;
    if (threshold > line_count) threshold = line_count;
    if (threshold == 0) threshold = 1;
    return threshold;
}

static inline size_t mtpndd_hash_probe_next(size_t idx) {
    return (idx & MTPNDD_HASH_CL_MASK) | ((idx + 1) & MTPNDD_HASH_CL_MASK_R);
}

static inline uint64_t mtpndd_hash_probe_step(uint64_t hash_rehash) {
    return (((hash_rehash >> 20) | 1ULL) * (uint64_t)MTPNDD_HASH_CL_SLOTS);
}

static size_t mtpndd_next_pow2(size_t v) {
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    if (sizeof(size_t) == 8) {
        v |= v >> 32;
    }
    return v + 1;
}

static size_t mtpndd_prev_pow2(size_t v) {
    if (v == 0) return 0;
    uint64_t x = (uint64_t)v;
    return (size_t)(1ULL << (63u - (uint32_t)__builtin_clzll(x)));
}

static void *mtpndd_aligned_zalloc(size_t alignment, size_t size) {
    if (size == 0) {
        return NULL;
    }
    void *ptr = NULL;
    size_t rounded = (size + alignment - 1) & ~(alignment - 1);
    int rc = posix_memalign(&ptr, alignment, rounded);
    if (rc != 0) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    memset(ptr, 0, rounded);
    return ptr;
}

static void *mtpndd_aligned_realloc_zextend(void *old_ptr, size_t alignment, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        free(old_ptr);
        return NULL;
    }
    void *new_ptr = mtpndd_aligned_zalloc(alignment, new_size);
    if (!new_ptr) {
        return NULL;
    }
    if (old_ptr && old_size > 0) {
        size_t copy_size = old_size < new_size ? old_size : new_size;
        memcpy(new_ptr, old_ptr, copy_size);
        free(old_ptr);
    }
    return new_ptr;
}

static uint64_t mtpndd_node_struct_hash(uint32_t field_id, const mtpndd_edge_record_t *edges, uint32_t edge_num) {
    uint64_t h = 1469598103934665603ULL;
    h ^= (uint64_t)field_id;
    h *= 1099511628211ULL;
    for (uint32_t i = 0; i < edge_num; ++i) {
        h ^= (uint64_t)edges[i].child;
        h *= 1099511628211ULL;
        h ^= (uint64_t)edges[i].label;
        h *= 1099511628211ULL;
    }
    // Finalize with a strong mixing function so low bits are usable for indexing (Sylvan-style).
    return (uint64_t)mtpndd_hash_u64(h);
}

static inline uint64_t mtpndd_hash_slot_hash(uint64_t hash_rehash) {
    return hash_rehash & MTPNDD_NODETABLE_SLOT_MASK_HASH;
}

static inline uint64_t mtpndd_hash_slot_pack(uint64_t hash_rehash, mtpndd_t idx) {
    return mtpndd_hash_slot_hash(hash_rehash) | (idx & MTPNDD_NODETABLE_SLOT_MASK_INDEX);
}

static inline uint64_t mtpndd_hash_slot_unpack_hash(uint64_t packed) {
    return packed & MTPNDD_NODETABLE_SLOT_MASK_HASH;
}

static inline mtpndd_t mtpndd_hash_slot_unpack_idx(uint64_t packed) {
    return (mtpndd_t)(packed & MTPNDD_NODETABLE_SLOT_MASK_INDEX);
}

// Store the free-list "next" pointer inside a freed node record without growing the record size:
// - edge_num==0 marks the record as free
// - ref_count/edge_array_idx are repurposed to store next (high/low 32 bits)
static inline mtpndd_t mtpndd_node_record_free_next(const mtpndd_node_record_t *rec) {
    return ((mtpndd_t)rec->ref_count << 32) | (mtpndd_t)rec->edge_array_idx;
}

static inline void mtpndd_node_record_set_free_next(mtpndd_node_record_t *rec, mtpndd_t next) {
    rec->ref_count = (uint32_t)(next >> 32);
    rec->edge_array_idx = (uint32_t)next;
}

static size_t mtpndd_hash_find_empty_slot(uint64_t *hash_table,
                                         size_t mask,
                                         size_t *probe_threshold,
                                         uint64_t hash_rehash)
{
    uint64_t step = mtpndd_hash_probe_step(hash_rehash);
    size_t idx = (size_t)hash_rehash & mask;
    size_t last = idx;
    size_t i = 0;

    while (hash_table[idx] != 0) {
        idx = mtpndd_hash_probe_next(idx);
        if (idx == last) {
            if (++i == *probe_threshold) {
                // Rehashing must not fail: extend the probe sequence (Sylvan-style).
                (*probe_threshold)++;
            }
            hash_rehash += step;
            last = idx = (size_t)hash_rehash & mask;
        }
    }
    return idx;
}

static bool mtpndd_node_edges_equal(const mtpndd_nodetable_t *table,
                                   const mtpndd_node_record_t *node,
                                   const mtpndd_edge_record_t *edges,
                                   uint32_t edge_num)
{
    if (node->edge_num != edge_num) return false;
    if (edge_num == 0) return true;
    const uint32_t base = node->edge_array_idx;
    for (uint32_t i = 0; i < edge_num; ++i) {
        const mtpndd_edge_record_t stored = table->edge_pool.data[base + i];
        if (stored.child != edges[i].child || stored.label != edges[i].label) {
            return false;
        }
    }
    return true;
}

static bool mtpndd_nodetable_rehash(mtpndd_nodetable_t *table, size_t new_capacity) {
    // Phase 3B: Acquire rehash_mutex for single-threaded rehash
    pthread_mutex_lock(&table->rehash_mutex);

    if (table->hash_capacity_max && new_capacity > table->hash_capacity_max) {
        new_capacity = table->hash_capacity_max;
    }
    size_t cap = mtpndd_next_pow2(new_capacity);
    if (table->hash_capacity_max && cap > table->hash_capacity_max) {
        cap = mtpndd_prev_pow2(table->hash_capacity_max);
    }
    if (cap < 8) cap = 8;
    if (cap <= table->hash_capacity) {
        pthread_mutex_unlock(&table->rehash_mutex);
        return true;
    }

    // Phase 3B: Acquire all bucket locks (stop-the-world for hash table)
    for (size_t i = 0; i < MTPNDD_FIXED_LOCK_COUNT; ++i) {
        pthread_spin_lock(&table->bucket_locks[i]);
    }

    uint64_t *new_hash = (uint64_t *)mtpndd_aligned_zalloc(MTPNDD_CACHELINE_BYTES, cap * sizeof(uint64_t));
    if (!new_hash) {
        // Release all locks on failure
        for (size_t i = 0; i < MTPNDD_FIXED_LOCK_COUNT; ++i) {
            pthread_spin_unlock(&table->bucket_locks[i]);
        }
        pthread_mutex_unlock(&table->rehash_mutex);
        return false;
    }

    size_t new_mask = cap - 1;
    size_t probe_threshold = mtpndd_hash_probe_threshold(cap);
    for (size_t i = 0; i < table->hash_capacity; ++i) {
        uint64_t packed = table->hash[i];
        mtpndd_t idx = mtpndd_hash_slot_unpack_idx(packed);
        if (idx == 0) continue;
        const mtpndd_node_record_t *node = &table->data[(size_t)idx];
        const mtpndd_edge_record_t *edges = &table->edge_pool.data[node->edge_array_idx];
        uint64_t hash = mtpndd_node_struct_hash(node->field_id, edges, node->edge_num);
        size_t slot = mtpndd_hash_find_empty_slot(new_hash, new_mask, &probe_threshold, hash);
        new_hash[slot] = mtpndd_hash_slot_pack(hash, idx);
    }

    free(table->hash);
    table->hash = new_hash;
    table->hash_capacity = cap;
    table->hash_mask = new_mask;
    table->hash_probe_threshold = probe_threshold;
    // hash_count unchanged

    // Phase 3B: bucket_locks array stays fixed size (never reallocated)
    // bucket_lock_count remains MTPNDD_FIXED_LOCK_COUNT

    // Release all bucket locks
    for (size_t i = 0; i < MTPNDD_FIXED_LOCK_COUNT; ++i) {
        pthread_spin_unlock(&table->bucket_locks[i]);
    }
    pthread_mutex_unlock(&table->rehash_mutex);

    return true;
}

void mtpndd_nodetable_init(mtpndd_nodetable_t *table,
                           size_t node_capacity_min,
                           size_t node_capacity_max,
                           size_t hash_capacity_min,
                           size_t hash_capacity_max,
                           size_t edge_capacity_hint)
{
    if (!table) {
        return;
    }
    memset(table, 0, sizeof(*table));

    if (node_capacity_max < node_capacity_min) node_capacity_max = node_capacity_min;
    if (hash_capacity_max < hash_capacity_min) hash_capacity_max = hash_capacity_min;

    size_t data_cap = node_capacity_min ? node_capacity_min : 1024;
    if (data_cap < 8) data_cap = 8;
    table->data = (mtpndd_node_record_t *)mtpndd_aligned_zalloc(MTPNDD_CACHELINE_BYTES, data_cap * sizeof(mtpndd_node_record_t));
    if (!table->data) {
        return;
    }
    table->data_capacity = data_cap;
    table->data_capacity_max = node_capacity_max;
    if (table->data_capacity_max < table->data_capacity) {
        table->data_capacity_max = table->data_capacity;
    }
    table->data_size = 2; // reserve 0/1 for terminals
    table->free_list_head = 0;

    size_t hash_cap = mtpndd_next_pow2(hash_capacity_min ? hash_capacity_min : data_cap * 2);
    if (hash_cap < 8) hash_cap = 8;
    table->hash = (uint64_t *)mtpndd_aligned_zalloc(MTPNDD_CACHELINE_BYTES, hash_cap * sizeof(uint64_t));
    if (!table->hash) {
        free(table->data);
        table->data = NULL;
        table->data_capacity = 0;
        return;
    }
    table->hash_capacity = hash_cap;
    table->hash_capacity_max = hash_capacity_max;
    if (table->hash_capacity_max < table->hash_capacity) {
        table->hash_capacity_max = table->hash_capacity;
    }
    table->hash_mask = hash_cap - 1;
    atomic_store_explicit(&table->hash_count, 0, memory_order_relaxed);  // Phase 3: atomic
    table->hash_probe_threshold = mtpndd_hash_probe_threshold(hash_cap);

    // Phase 3B: Initialize concurrency control with fixed-size lock array
    table->bucket_lock_count = MTPNDD_FIXED_LOCK_COUNT;
    table->bucket_locks = (pthread_spinlock_t *)malloc(MTPNDD_FIXED_LOCK_COUNT * sizeof(pthread_spinlock_t));
    if (!table->bucket_locks) {
        free(table->hash);
        free(table->data);
        table->hash = NULL;
        table->data = NULL;
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return;
    }
    for (size_t i = 0; i < MTPNDD_FIXED_LOCK_COUNT; ++i) {
        pthread_spin_init(&table->bucket_locks[i], PTHREAD_PROCESS_PRIVATE);
    }
    pthread_mutex_init(&table->rehash_mutex, NULL);
    pthread_mutex_init(&table->freelist_mutex, NULL);
    pthread_mutex_init(&table->dataarray_mutex, NULL);

    mtpndd_edge_array_pool_init(&table->edge_pool, edge_capacity_hint);
}

static size_t mtpndd_nodetable_live_nodes(const mtpndd_nodetable_t *table) {
    if (!table || !table->data) return 0;
    size_t live = 0;
    for (mtpndd_t idx = 2; idx < table->data_size; ++idx) {
        const mtpndd_node_record_t *node = &table->data[(size_t)idx];
        if (node->edge_num != 0) {
            live++;
        }
    }
    return live;
}

static size_t mtpndd_nodetable_live_edges(const mtpndd_nodetable_t *table) {
    if (!table || !table->data) return 0;
    size_t edges = 0;
    for (mtpndd_t idx = 2; idx < table->data_size; ++idx) {
        const mtpndd_node_record_t *node = &table->data[(size_t)idx];
        if (node->edge_num != 0) {
            edges += node->edge_num;
        }
    }
    return edges;
}

static bool mtpndd_nodetable_rebuild_hash(mtpndd_nodetable_t *table, size_t target_capacity) {
    if (table->hash_capacity_max && target_capacity > table->hash_capacity_max) {
        target_capacity = table->hash_capacity_max;
    }
    size_t cap = mtpndd_next_pow2(target_capacity);
    if (table->hash_capacity_max && cap > table->hash_capacity_max) {
        cap = mtpndd_prev_pow2(table->hash_capacity_max);
    }
    if (cap < 8) cap = 8;

    uint64_t *new_hash = (uint64_t *)mtpndd_aligned_zalloc(MTPNDD_CACHELINE_BYTES, cap * sizeof(uint64_t));
    if (!new_hash) {
        return false;
    }

    size_t new_mask = cap - 1;
    size_t probe_threshold = mtpndd_hash_probe_threshold(cap);
    size_t count = 0;
    for (mtpndd_t idx = 2; idx < table->data_size; ++idx) {
        const mtpndd_node_record_t *node = &table->data[(size_t)idx];
        if (node->edge_num == 0) {
            continue;
        }
        const mtpndd_edge_record_t *edges = &table->edge_pool.data[node->edge_array_idx];
        uint64_t hash = mtpndd_node_struct_hash(node->field_id, edges, node->edge_num);
        size_t slot = mtpndd_hash_find_empty_slot(new_hash, new_mask, &probe_threshold, hash);
        new_hash[slot] = mtpndd_hash_slot_pack(hash, idx);
        count++;
    }

    free(table->hash);
    table->hash = new_hash;
    table->hash_capacity = cap;
    table->hash_mask = new_mask;
    table->hash_count = count;
    table->hash_probe_threshold = probe_threshold;
    return true;
}

static bool mtpndd_edge_pool_compact(mtpndd_nodetable_t *table, size_t live_edges) {
    if (!table || !table->edge_pool.data) {
        return true;
    }
    if (live_edges == 0) {
        free(table->edge_pool.data);
        table->edge_pool.data = NULL;
        table->edge_pool.capacity = 0;
        table->edge_pool.size = 0;
        return true;
    }

    size_t new_capacity = mtpndd_next_pow2(live_edges);
    if (new_capacity < 1024) new_capacity = 1024;
    mtpndd_edge_record_t *new_data =
            (mtpndd_edge_record_t *)mtpndd_aligned_zalloc(MTPNDD_CACHELINE_BYTES, new_capacity * sizeof(mtpndd_edge_record_t));
    if (!new_data) {
        return false;
    }

    size_t write = 0;
    for (mtpndd_t idx = 2; idx < table->data_size; ++idx) {
        mtpndd_node_record_t *node = &table->data[(size_t)idx];
        if (node->edge_num == 0) {
            continue;
        }
        size_t count = node->edge_num;
        size_t base = node->edge_array_idx;
        memcpy(new_data + write, table->edge_pool.data + base, count * sizeof(mtpndd_edge_record_t));
        if (write > UINT32_MAX) {
            free(new_data);
            MTPNDD_SET_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
            return false;
        }
        node->edge_array_idx = (uint32_t)write;
        write += count;
    }

    free(table->edge_pool.data);
    table->edge_pool.data = new_data;
    table->edge_pool.capacity = new_capacity;
    table->edge_pool.size = write;
    return true;
}

void mtpndd_nodetable_destroy(mtpndd_nodetable_t *table) {
    if (!table) {
        return;
    }
    if (table->data && table->edge_pool.data) {
        for (mtpndd_t idx = 2; idx < table->data_size; ++idx) {
            mtpndd_node_record_t *node = &table->data[(size_t)idx];
            if (node->edge_num == 0) {
                continue;
            }
            const uint32_t base = node->edge_array_idx;
            for (uint32_t i = 0; i < node->edge_num; ++i) {
                sylvan_deref(table->edge_pool.data[base + i].label);
            }
            node->edge_num = 0;
        }
    }
    mtpndd_edge_array_pool_destroy(&table->edge_pool);
    free(table->hash);
    free(table->data);

    // Phase 3B: Cleanup concurrency control
    if (table->bucket_locks) {
        for (size_t i = 0; i < table->bucket_lock_count; ++i) {
            pthread_spin_destroy(&table->bucket_locks[i]);
        }
        free(table->bucket_locks);
    }
    pthread_mutex_destroy(&table->rehash_mutex);
    pthread_mutex_destroy(&table->freelist_mutex);
    pthread_mutex_destroy(&table->dataarray_mutex);

    memset(table, 0, sizeof(*table));
}

size_t mtpndd_nodetable_collect_garbage(mtpndd_nodetable_t *table) {
    if (!table || !table->data || !table->edge_pool.data) {
        return 0;
    }

    // Seed deletion list with nodes that are unreferenced and not pinned.
    mtpndd_t *stack = NULL;
    size_t stack_cap = 0;
    size_t stack_len = 0;

    for (mtpndd_t idx = 2; idx < table->data_size; ++idx) {
        const mtpndd_node_record_t *node = &table->data[(size_t)idx];
        if (node->edge_num == 0) continue; // already free
        if (node->ref_count != 0) continue;
        stack_len++;
        if (stack_len > stack_cap) {
            size_t new_cap = stack_cap ? (stack_cap * 2) : 1024;
            mtpndd_t *new_stack = (mtpndd_t *)realloc(stack, new_cap * sizeof(mtpndd_t));
            if (!new_stack) {
                free(stack);
                MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                return 0;
            }
            stack = new_stack;
            stack_cap = new_cap;
        }
        stack[stack_len - 1] = idx;
    }

    size_t collected = 0;

    while (stack_len > 0) {
        mtpndd_t idx = stack[--stack_len];
        mtpndd_node_record_t *node = &table->data[(size_t)idx];
        if (node->edge_num == 0) continue;
        if (node->ref_count != 0) continue;
        if (node->ref_count == MTPNDD_REFCOUNT_PROTECTED) continue;

        uint32_t base = node->edge_array_idx;
        uint32_t edge_num = node->edge_num;

        for (uint32_t i = 0; i < edge_num; ++i) {
            mtpndd_edge_record_t e = table->edge_pool.data[base + i];
            // drop label reference held by this node
            sylvan_deref(e.label);

            // decrement child refcount (parent held a reference)
            if (e.child >= 2) {
                mtpndd_node_record_t *child = &table->data[(size_t)e.child];
                if (child->ref_count != MTPNDD_REFCOUNT_PROTECTED && child->ref_count > 0) {
                    child->ref_count -= 1;
                    if (child->ref_count == 0 && child->edge_num != 0) {
                        stack_len++;
                        if (stack_len > stack_cap) {
                            size_t new_cap = stack_cap ? (stack_cap * 2) : 1024;
                            mtpndd_t *new_stack = (mtpndd_t *)realloc(stack, new_cap * sizeof(mtpndd_t));
                            if (!new_stack) {
                                free(stack);
                                MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                                return collected;
                            }
                            stack = new_stack;
                            stack_cap = new_cap;
                        }
                        stack[stack_len - 1] = e.child;
                    }
                }
            }
        }

        // Add idx to free-list (reuse by mk). Store next pointer in edge_array_idx.
        node->field_id = 0;
        node->edge_num = 0;
        mtpndd_node_record_set_free_next(node, table->free_list_head);
        table->free_list_head = idx;

        collected++;
    }

    free(stack);

    // Rebuild hash table to remove dead nodes (Sylvan-style: rebuild hash only; keep data[] stable).
    size_t live_nodes = mtpndd_nodetable_live_nodes(table);
    size_t min_hash = g_mtpndd_pal_config.nodetable_bucket_count ? g_mtpndd_pal_config.nodetable_bucket_count : 8;
    size_t target_hash = live_nodes ? (live_nodes * 2) : 8;
    if (target_hash < min_hash) target_hash = min_hash;
    (void)mtpndd_nodetable_rebuild_hash(table, target_hash);

    // Edge pool compaction heuristic (Sylvan-style: free large blocks; no sub-range free in C).
    size_t live_edges = mtpndd_nodetable_live_edges(table);
    if (table->edge_pool.size > 0 && live_edges <= table->edge_pool.size) {
        double frag = 1.0 - ((double)live_edges / (double)table->edge_pool.size);
        double threshold = g_mtpndd_pal_config.quick_growth_threshold;
        if (threshold <= 0.0) threshold = 0.1;
        if (frag >= threshold) {
            (void)mtpndd_edge_pool_compact(table, live_edges);
        }
    }

    return collected;
}

// TODO: check if we should ref and deref all label_BDD of node
void mtpndd_ref(mtpndd_t node) {
    if (node < 2) {
        return;
    }
    mtpndd_node_record_t *rec = mtpndd_node_record(&g_mtpndd_nodetable, node);
    if (!rec) {
        return;
    }
    // CRITICAL FIX: Atomic increment for concurrent access
    uint32_t old_count = atomic_load_explicit(&rec->ref_count, memory_order_relaxed);
    if (old_count == MTPNDD_REFCOUNT_PROTECTED) {
        return;
    }
    atomic_fetch_add_explicit(&rec->ref_count, 1, memory_order_relaxed);
}

void mtpndd_deref(mtpndd_t node) {
    if (node < 2) {
        return;
    }
    mtpndd_node_record_t *rec = mtpndd_node_record(&g_mtpndd_nodetable, node);
    if (!rec) {
        return;
    }
    // CRITICAL FIX: Atomic decrement for concurrent access
    uint32_t old_count = atomic_load_explicit(&rec->ref_count, memory_order_relaxed);
    if (old_count == MTPNDD_REFCOUNT_PROTECTED) {
        return;
    }
    if (old_count > 0) {
        atomic_fetch_sub_explicit(&rec->ref_count, 1, memory_order_relaxed);
    }
}

void mtpndd_protect(mtpndd_t node) {
    if (node < 2) {
        return;
    }
    mtpndd_node_record_t *rec = mtpndd_node_record(&g_mtpndd_nodetable, node);
    if (!rec) {
        return;
    }
    atomic_store_explicit(&rec->ref_count, MTPNDD_REFCOUNT_PROTECTED, memory_order_relaxed);
}

void mtpndd_unprotect(mtpndd_t node) {
    if (node < 2) {
        return;
    }
    mtpndd_node_record_t *rec = mtpndd_node_record(&g_mtpndd_nodetable, node);
    if (!rec) {
        return;
    }
    uint32_t current = atomic_load_explicit(&rec->ref_count, memory_order_relaxed);
    if (current == MTPNDD_REFCOUNT_PROTECTED) {
        atomic_store_explicit(&rec->ref_count, 0, memory_order_relaxed);
    }
}

mtpndd_t mtpndd_mk(uint32_t field_id, mtpndd_edge_builder_t *builder) {
    if (!builder) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NULL_POINTER);
        return MTPNDD_INVALID;
    }
    if (!mtpndd_edge_builder_finalize(builder)) {
        return MTPNDD_INVALID;
    }

    uint32_t edge_num = (uint32_t)builder->count;
    if (edge_num == 0) {
        builder->count = 0;
        return MTPNDD_FALSE;
    }
    if (edge_num == 1) {
        const mtpndd_edge_record_t e = builder->edges[0];
        if (e.label == sylvan_true) {
            sylvan_deref(e.label);
            builder->count = 0;
            return e.child;
        }
    }

    mtpndd_nodetable_t *table = &g_mtpndd_nodetable;

    // Phase 3B: Check if GC needed BEFORE acquiring any locks
    size_t current_data_size = atomic_load_explicit(&table->data_size, memory_order_relaxed);
    if (table->free_list_head == 0 && current_data_size >= table->data_capacity) {
        // Try GC first to avoid doing it while holding locks
        (void)mtpndd_gc_collect();
    }

    // Lookup / find insertion slot with cache-line-aware probing (Sylvan-style).
    uint64_t hash = 0;
    uint64_t hash_bits = 0;
    size_t slot = 0;
    size_t lock_idx = 0;  // Phase 3B: bucket lock index
    bool lock_held = false;

    for (int attempt = 0; attempt < 3; ++attempt) {
        // Rehash when load factor too high (may double up to hash_capacity_max).
        size_t current_count = atomic_load_explicit(&table->hash_count, memory_order_relaxed);
        if (table->hash_capacity > 0 && (current_count + 1) * 10 >= table->hash_capacity * 7) {
            size_t target = table->hash_capacity * 2;
            if (table->hash_capacity_max && target > table->hash_capacity_max) target = table->hash_capacity_max;
            if (target > table->hash_capacity) {
                (void)mtpndd_nodetable_rehash(table, target);
            }
        }

        hash = mtpndd_node_struct_hash(field_id, builder->edges, edge_num);
        hash_bits = mtpndd_hash_slot_hash(hash);
        uint64_t hash_rehash = hash;
        const uint64_t step = mtpndd_hash_probe_step(hash_rehash);
        slot = (size_t)hash_rehash & table->hash_mask;

        // Phase 3B: Acquire bucket lock based on initial slot (covers entire probe sequence)
        // Use modulo with fixed lock count to avoid reallocation issues
        lock_idx = slot % MTPNDD_FIXED_LOCK_COUNT;
        pthread_spin_lock(&table->bucket_locks[lock_idx]);
        lock_held = true;

        size_t last = slot;
        size_t i = 0;
        const size_t threshold = table->hash_probe_threshold ? table->hash_probe_threshold : mtpndd_hash_probe_threshold(table->hash_capacity);

        for (;;) {
            uint64_t packed = table->hash[slot];
            if (packed == 0) {
                // Empty slot found.
                break;
            }

            if (mtpndd_hash_slot_unpack_hash(packed) == hash_bits) {
                mtpndd_t existing_idx = mtpndd_hash_slot_unpack_idx(packed);
                mtpndd_node_record_t *existing = mtpndd_node_record(table, existing_idx);
                if (existing && existing->field_id == field_id
                        && mtpndd_node_edges_equal(table, existing, builder->edges, edge_num)) {
                    // reuse - release lock before returning
                    pthread_spin_unlock(&table->bucket_locks[lock_idx]);
                    lock_held = false;
                    for (uint32_t e = 0; e < edge_num; ++e) {
                        sylvan_deref(builder->edges[e].label);
                    }
                    builder->count = 0;
#ifdef ENABLE_RECORDING
                    MTPNDD_STAT_ADD(nodes_reused_total, 1);
#endif
                    return existing_idx;
                }
            }

            // next slot in probe sequence
            slot = mtpndd_hash_probe_next(slot);
            if (slot == last) {
                if (++i == threshold) {
                    // Probe sequence exhausted: release lock, try grow/rebuild and restart
                    pthread_spin_unlock(&table->bucket_locks[lock_idx]);
                    lock_held = false;
                    bool progressed = false;
                    size_t target = table->hash_capacity * 2;
                    if (table->hash_capacity_max && target > table->hash_capacity_max) target = table->hash_capacity_max;
                    if (target > table->hash_capacity) {
                        progressed = mtpndd_nodetable_rehash(table, target);
                    }
                    if (!progressed) {
                        (void)mtpndd_gc_collect();
                    }
                    goto retry_probe;
                }
                hash_rehash += step;
                last = slot = (size_t)hash_rehash & table->hash_mask;
            }
        }
        // slot is empty (insertion candidate)
        break;

    retry_probe:
        continue;
    }
    if (table->hash[slot] != 0) {
        // Failed to find empty slot - release lock if held
        if (lock_held) {
            pthread_spin_unlock(&table->bucket_locks[lock_idx]);
            lock_held = false;
        }
        MTPNDD_SET_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
        return MTPNDD_INVALID;
    }

    // Phase 3B: Allocate node idx with proper locking
    // Use freelist_mutex for free list access and dataarray_mutex for growth
    mtpndd_t new_idx = 0;

    // Try allocate from free list (protected by freelist_mutex)
    pthread_mutex_lock(&table->freelist_mutex);
    if (table->free_list_head != 0) {
        mtpndd_t idx = table->free_list_head;
        mtpndd_node_record_t *free_rec = &table->data[(size_t)idx];
        table->free_list_head = mtpndd_node_record_free_next(free_rec);
        new_idx = idx;
        pthread_mutex_unlock(&table->freelist_mutex);
    } else {
        pthread_mutex_unlock(&table->freelist_mutex);

        // Allocate from data array (protected by dataarray_mutex)
        pthread_mutex_lock(&table->dataarray_mutex);
        current_data_size = atomic_load_explicit(&table->data_size, memory_order_relaxed);

        // Check if data array needs growth
        if (current_data_size >= table->data_capacity) {
            size_t new_cap = table->data_capacity ? table->data_capacity * 2 : 1024;
            if (table->data_capacity_max && new_cap > table->data_capacity_max) {
                new_cap = table->data_capacity_max;
            }
            if (new_cap <= table->data_capacity) {
                pthread_mutex_unlock(&table->dataarray_mutex);
                if (lock_held) {
                    pthread_spin_unlock(&table->bucket_locks[lock_idx]);
                    lock_held = false;
                }
                MTPNDD_SET_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
                return MTPNDD_INVALID;
            }
            size_t old_bytes = table->data_capacity * sizeof(mtpndd_node_record_t);
            size_t new_bytes = new_cap * sizeof(mtpndd_node_record_t);
            mtpndd_node_record_t *new_data = (mtpndd_node_record_t *)mtpndd_aligned_realloc_zextend(
                    table->data, MTPNDD_CACHELINE_BYTES, old_bytes, new_bytes);
            if (!new_data) {
                pthread_mutex_unlock(&table->dataarray_mutex);
                if (lock_held) {
                    pthread_spin_unlock(&table->bucket_locks[lock_idx]);
                    lock_held = false;
                }
                MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
                return MTPNDD_INVALID;
            }
            table->data = new_data;
            table->data_capacity = new_cap;
        }

        // Atomically allocate node index
        new_idx = (mtpndd_t)atomic_fetch_add_explicit(&table->data_size, 1, memory_order_relaxed);
        pthread_mutex_unlock(&table->dataarray_mutex);
    }

    if (new_idx > (mtpndd_t)MTPNDD_NODETABLE_SLOT_MASK_INDEX) {
        if (lock_held) {
            pthread_spin_unlock(&table->bucket_locks[lock_idx]);
            lock_held = false;
        }
        MTPNDD_SET_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
        return MTPNDD_INVALID;
    }

    uint32_t edge_start = mtpndd_edge_array_pool_alloc(&table->edge_pool, edge_num);
    if (edge_start == UINT32_MAX) {
        // Edge pool allocation failed - release locks
        if (lock_held) {
            pthread_spin_unlock(&table->bucket_locks[lock_idx]);
            lock_held = false;
        }
        return MTPNDD_INVALID;
    }

    // store edges into pool
    for (uint32_t i = 0; i < edge_num; ++i) {
        // TODO: memcpy?
        table->edge_pool.data[edge_start + i] = builder->edges[i];
    }

    // parent holds a reference on all children
    for (uint32_t i = 0; i < edge_num; ++i) {
        mtpndd_t child = builder->edges[i].child;
        if (child >= 2) {
            mtpndd_ref(child);
        }
    }

    mtpndd_node_record_t *node = &table->data[(size_t)new_idx];
    node->field_id = field_id;
    atomic_store_explicit(&node->ref_count, 0, memory_order_relaxed);  // CRITICAL: Atomic init
    node->edge_array_idx = edge_start;
    node->edge_num = edge_num;

    table->hash[slot] = mtpndd_hash_slot_pack(hash, new_idx);
    size_t new_count = atomic_fetch_add_explicit(&table->hash_count, 1, memory_order_relaxed) + 1;
    g_mtpndd_stats.node_count = (uint64_t)new_count;

    // Phase 3B: Release bucket lock before returning
    if (lock_held) {
        pthread_spin_unlock(&table->bucket_locks[lock_idx]);
        lock_held = false;
    }

    builder->count = 0;

#ifdef ENABLE_RECORDING
    MTPNDD_STAT_ADD(nodes_created_total, 1);
    MTPNDD_STAT_MAX(max_edges_per_node, edge_num);
#endif
    return new_idx;
}
