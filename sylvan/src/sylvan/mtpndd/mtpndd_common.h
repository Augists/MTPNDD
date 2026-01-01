// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_COMMON_H
#define MTPNDD_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

#ifdef ENABLE_RECORDING
#include <time.h>
#endif

struct mtpndd_node_s;
struct mtpndd_edge_s;
struct mtpndd_nodetable_s;
struct mtpndd_op_cache_s;
struct mtpndd_gc_protect_s;
struct gc_protect_entry_s;

typedef uint64_t mtpndd_bdd_t;
typedef struct mtpndd_node_s mtpndd_node_t;
typedef mtpndd_node_t mtpndd_t;
typedef struct mtpndd_edge_s mtpndd_edge_t;
typedef struct mtpndd_nodetable_s mtpndd_nodetable_t;
typedef struct mtpndd_op_cache_s mtpndd_op_cache_t;
typedef struct mtpndd_gc_protect_s mtpndd_gc_protect_t;
typedef struct gc_protect_entry_s gc_protect_entry_t;

size_t mtpndd_hash_node_identity(const mtpndd_node_t *node);

static inline size_t mtpndd_hash_u64(uint64_t key) {
    uint64_t value = key;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return (size_t)value;
}

/********************************
 * Error handling system
 ********************************/
typedef enum mtpndd_error_e {
    MTPNDD_SUCCESS = 0,             // Success
    MTPNDD_ERROR_INVALID_PARAM,     // Invalid parameter
    MTPNDD_ERROR_INITIALIZE_FAILED, // Initialization failed
    MTPNDD_ERROR_NOT_INITIALIZED,   // Not initialized
    MTPNDD_ERROR_IS_INITIALIZED,    // Already initialized
    MTPNDD_ERROR_OUT_OF_MEMORY,     // Out of memory
    MTPNDD_ERROR_INVALID_FIELD,     // Invalid field
    MTPNDD_ERROR_NULL_POINTER,      // Null pointer
    MTPNDD_ERROR_CAPACITY_EXCEEDED, // Capacity exceeded
    MTPNDD_ERROR_PARALLEL_INIT,     // Parallel initialization failed
    MTPNDD_ERROR_BDD_OPERATION,     // BDD operation failed
    MTPNDD_ERROR_THREAD_SAFETY,     // Thread safety error

    MTPNDD_ERROR_UNKNOWN            // Unknown error
} mtpndd_error_t;

extern const char* mtpndd_error_messages[];

typedef struct mtpndd_error_info_s {
    mtpndd_error_t code;
    const char *message;
    const char *function;
    int line;
} mtpndd_error_info_t;

const char* mtpndd_error_string(mtpndd_error_t error);
void mtpndd_set_error(mtpndd_error_t error, const char *function, int line);
mtpndd_error_info_t mtpndd_get_last_error();
void mtpndd_clear_error();

#define MTPNDD_SET_ERROR(error) mtpndd_set_error(error, __FUNCTION__, __LINE__)
#define MTPNDD_RETURN_ERROR(error) do { MTPNDD_SET_ERROR(error); return error; } while(0)

#define MTPNDD_CHECK_PARAM(condition, error) \
    do { if (!(condition)) { MTPNDD_RETURN_ERROR(error); } } while(0)
#define MTPNDD_CHECK_NULL(ptr, error) \
    do { if ((ptr) == NULL) { MTPNDD_RETURN_ERROR(error); } } while(0)
#define MTPNDD_CHECK_INIT() \
    do { if (!mtpndd_is_initialized()) { MTPNDD_RETURN_ERROR(MTPNDD_ERROR_NOT_INITIALIZED); } } while(0)

/********************************
 * Default sizing helpers
 ********************************/
#ifdef LARGE_NODETABLE
#define MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT 65537
#define MTPNDD_DEFAULT_GC_BUCKET_COUNT 65537
#else
#define MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT 1024
#define MTPNDD_DEFAULT_GC_BUCKET_COUNT 1024
#endif

#define MTPNDD_DEFAULT_EDGE_BUCKET_COUNT 8
#define MTPNDD_DEFAULT_NODE_SLAB_CAPACITY 1024
#define MTPNDD_DEFAULT_EDGE_ENTRY_SLAB_CAPACITY 4096
#define MTPNDD_DEFAULT_NODETABLE_ENTRY_SLAB_CAPACITY 2048
#define MTPNDD_DEFAULT_EDGE_MAP_SLAB_CAPACITY 2048
#define MTPNDD_DEFAULT_GC_PROTECT_ENTRY_SLAB_CAPACITY 1024

/********************************
 * Global config definitions
 ********************************/
typedef struct mtpndd_pal_config_s {
    int32_t n_workers;
    size_t lace_dqsize;
    size_t bdd_nodetable_size;
    size_t mtpndd_nodetable_size;
    size_t op_cache_size;
    double quick_growth_threshold;
    size_t edge_bucket_count;
    size_t nodetable_bucket_count;
    size_t gc_bucket_count;
    size_t node_slab_capacity;
    size_t edge_entry_slab_capacity;
    size_t nodetable_entry_slab_capacity;
    size_t edge_map_slab_capacity;
    size_t gc_protect_entry_slab_capacity;
} mtpndd_pal_config_t;

extern mtpndd_pal_config_t g_mtpndd_pal_config;

typedef struct mtpndd_stats_s {
    uint64_t node_count;
#ifdef ENABLE_RECORDING
    uint64_t max_edges_per_node;
    uint64_t bdd_nodes_converted;
    uint64_t cache_lookup_hits;
    uint64_t cache_lookup_misses;
    uint64_t cache_store_overwrites;
    uint64_t cache_store_total;
    uint64_t gc_runs;
    uint64_t nodes_created_total;
    uint64_t nodes_reused_total;
    uint64_t nodes_collected_last;
    uint64_t edge_insert_total;
    uint64_t edge_collision_total;
    uint64_t nodetable_collision_total;
    uint64_t edge_lock_spin_total;
    uint64_t edge_lock_wait_time_ns;
    uint64_t gc_pause_time_ns;
    uint64_t edge_entry_total;
    uint64_t bdd_nodes_processed_total;
    uint64_t node_pool_acquire_total;
    uint64_t node_pool_release_total;
    uint64_t node_pool_slab_total;
    uint64_t edge_entry_pool_acquire_total;
    uint64_t edge_entry_pool_release_total;
    uint64_t edge_entry_pool_slab_total;
    uint64_t nodetable_entry_pool_acquire_total;
    uint64_t nodetable_entry_pool_release_total;
    uint64_t nodetable_entry_pool_slab_total;
    uint64_t edge_map_pool_acquire_total;
    uint64_t edge_map_pool_release_total;
    uint64_t edge_map_pool_slab_total;
    uint64_t edge_map_rehash_total;
    uint64_t edge_map_max_buckets;
    uint64_t nodetable_rehash_total;
    uint64_t nodetable_max_buckets;
    uint64_t and_time_ns;
    uint64_t or_time_ns;
    uint64_t not_time_ns;
    uint64_t and_fastpath_ns;
    uint64_t and_cache_hit_ns;
    uint64_t and_build_edges_ns;
    uint64_t and_same_field_ns;
    uint64_t and_same_outer_loop_ns;
    uint64_t and_same_inner_loop_ns;
    uint64_t and_same_label_load_ns;
    uint64_t and_same_bdd_op_ns;
    uint64_t and_same_add_edge_ns;
    uint64_t and_diff_field_ns;
    uint64_t and_mk_ns;
    uint64_t and_mk_call_ns;
    uint64_t and_mk_gc_protect_ns;
    uint64_t and_mk_cache_store_ns;
    uint64_t and_mk_other_ns;
    uint64_t gc_protect_hash_ns;
    uint64_t gc_protect_lookup_ns;
    uint64_t gc_protect_alloc_ns;
    uint64_t gc_protect_record_ns;
    uint64_t gc_protect_link_ns;
    uint64_t gc_protect_lookup_steps_total;
    uint64_t gc_protect_lookup_max_steps;
    uint64_t gc_protect_lookup_hits;
    uint64_t gc_protect_lookup_misses;
    uint64_t mk_hash_ns;
    uint64_t mk_lookup_ns;
    uint64_t mk_fast_return_ns;
    uint64_t mk_reuse_cleanup_ns;
    uint64_t mk_ref_children_ns;
    uint64_t mk_gc_or_grow_ns;
    uint64_t mk_alloc_node_ns;
    uint64_t mk_alloc_entry_ns;
    uint64_t mk_bucket_scan_ns;
    uint64_t mk_link_ns;
    uint64_t mk_collision_cleanup_ns;
    uint64_t mk_other_ns;
    uint64_t mk_total_ns;
#endif
} mtpndd_stats_t;

extern mtpndd_stats_t g_mtpndd_stats;

#ifdef ENABLE_RECORDING
static inline void mtpndd_stat_add(uint64_t *field, uint64_t value) {
    __atomic_add_fetch(field, value, __ATOMIC_RELAXED);
}

static inline void mtpndd_stat_set(uint64_t *field, uint64_t value) {
    __atomic_store_n(field, value, __ATOMIC_RELAXED);
}

static inline void mtpndd_stat_max(uint64_t *field, uint64_t value) {
    uint64_t current = __atomic_load_n(field, __ATOMIC_RELAXED);
    while (value > current && !__atomic_compare_exchange_n(field, &current, value, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        /* retry with updated current */
    }
}

static inline uint64_t mtpndd_timespec_diff_ns(const struct timespec *start_ts, const struct timespec *end_ts) {
    uint64_t sec = (uint64_t)end_ts->tv_sec - (uint64_t)start_ts->tv_sec;
    int64_t nsec = end_ts->tv_nsec - start_ts->tv_nsec;
    return sec * 1000000000ull + (uint64_t)nsec;
}

#define MTPNDD_STAT_ADD(field, value) mtpndd_stat_add(&g_mtpndd_stats.field, (uint64_t)(value))
#define MTPNDD_STAT_SET(field, value) mtpndd_stat_set(&g_mtpndd_stats.field, (uint64_t)(value))
#define MTPNDD_STAT_MAX(field, value) mtpndd_stat_max(&g_mtpndd_stats.field, (uint64_t)(value))
#define MTPNDD_RECORD_TIME_START(var) struct timespec var = {0}; clock_gettime(CLOCK_MONOTONIC, &(var))
#define MTPNDD_RECORD_TIME_END(field, start_var) \
    do { \
        struct timespec _mtpndd_time_end = {0}; \
        clock_gettime(CLOCK_MONOTONIC, &_mtpndd_time_end); \
        MTPNDD_STAT_ADD(field, mtpndd_timespec_diff_ns(&(start_var), &_mtpndd_time_end)); \
    } while(0)
#else
#define MTPNDD_STAT_ADD(field, value) ((void)0)
#define MTPNDD_STAT_SET(field, value) ((void)0)
#define MTPNDD_STAT_MAX(field, value) ((void)0)
#define MTPNDD_RECORD_TIME_START(var) ((void)0)
#define MTPNDD_RECORD_TIME_END(field, start_var) ((void)0)
#endif

typedef struct mtpndd_field_info_s {
    uint32_t field_id;
    uint32_t bit_width;
    uint32_t start_var;

    // For internal use
    mtpndd_bdd_t *bdd_vars;
    mtpndd_bdd_t *bdd_not_vars;
    mtpndd_node_t **mtpndd_vars;
    mtpndd_node_t **mtpndd_not_vars;
} mtpndd_field_info_t;

extern mtpndd_field_info_t MTPNDD_TERMINAL_FIELD;

typedef struct mtpndd_config_s {
    uint32_t field_count;
    uint32_t field_capacity;
    mtpndd_field_info_t **field_info;

    mtpndd_nodetable_t **node_tables_by_field;

    mtpndd_op_cache_t *and_cache;
    mtpndd_op_cache_t *or_cache;
    mtpndd_op_cache_t *not_cache;

    mtpndd_gc_protect_t *gcProtect;
} mtpndd_config_t;

extern mtpndd_config_t g_mtpndd_config;

/********************************
 * MTPNDD field management
 ********************************/
// only append field
mtpndd_error_t mtpndd_declare_field(uint32_t bit_width);
mtpndd_field_info_t* mtpndd_get_field_info(uint32_t field_id);

/********************************
 * MTPNDD get node
 ********************************/
mtpndd_t *mtpndd_get_var(uint32_t field, uint32_t var_index);
mtpndd_t *mtpndd_get_not_var(uint32_t field, uint32_t var_index);
mtpndd_bdd_t mtpndd_get_bdd_var(uint32_t field, uint32_t var_index);
mtpndd_bdd_t mtpndd_get_bdd_not_var(uint32_t field, uint32_t var_index);

/********************************
 * Global status control
 ********************************/
mtpndd_stats_t *mtpndd_get_stats();

bool mtpndd_is_initialized();

mtpndd_error_t mtpndd_init(mtpndd_pal_config_t *config);
mtpndd_error_t mtpndd_quit();

/********************************
 * GC hooks
 ********************************/
// GC hook signature: pointer to void hook(void)
typedef void (*mtpndd_gc_hook_t)(void);
void mtpndd_gc_hook_pregc(mtpndd_gc_hook_t hook);
void mtpndd_gc_hook_postgc(mtpndd_gc_hook_t hook);
void mtpndd_gc_run_prehooks(void);
void mtpndd_gc_run_posthooks(void);

/********************************
 * GC protection hash set
 ********************************/
/**
 * Why not use a gc_protect_label and container_of for gc protection hash set in mtpndd_node_t?
 * It will save time when removing.
 * But it will waste space for gc_protect_label in every node.
 */
struct mtpndd_gc_protect_s {
    atomic_size_t gc_protect_count;
    gc_protect_entry_t **buckets;
    pthread_rwlock_t *bucket_locks;
    void *bucket_storage;
    size_t bucket_count;
    size_t *used_bucket_indices;
    size_t used_bucket_count;
    size_t used_bucket_capacity;
};

struct gc_protect_entry_s {
    struct gc_protect_entry_s *next;
    struct gc_protect_entry_s *prev;
    mtpndd_node_t *node;
};

#define GC_PROTECT_INIT(gcp) do { \
        atomic_init(&(gcp)->gc_protect_count, 0); \
        (gcp)->bucket_count = g_mtpndd_pal_config.gc_bucket_count; \
        size_t _gc_bucket_cnt = (gcp)->bucket_count; \
        if (_gc_bucket_cnt == 0) { \
            _gc_bucket_cnt = MTPNDD_DEFAULT_GC_BUCKET_COUNT; \
            (gcp)->bucket_count = _gc_bucket_cnt; \
        } \
        size_t _buckets_bytes = sizeof(gc_protect_entry_t *) * _gc_bucket_cnt; \
        size_t _locks_bytes = sizeof(pthread_rwlock_t) * _gc_bucket_cnt; \
        (gcp)->bucket_storage = calloc(1, _buckets_bytes + _locks_bytes); \
        if (!(gcp)->bucket_storage) { \
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__); \
            return MTPNDD_ERROR_OUT_OF_MEMORY; \
        } \
        (gcp)->buckets = (gc_protect_entry_t **)(gcp)->bucket_storage; \
        (gcp)->bucket_locks = (pthread_rwlock_t *)((char *)(gcp)->bucket_storage + _buckets_bytes); \
        for (size_t i = 0; i < _gc_bucket_cnt; i++) { \
            if (pthread_rwlock_init(&(gcp)->bucket_locks[i], NULL) != 0) { \
                for (size_t j = 0; j < i; j++) { \
                    pthread_rwlock_destroy(&(gcp)->bucket_locks[j]); \
                } \
                free((gcp)->bucket_storage); \
                (gcp)->bucket_storage = NULL; \
                (gcp)->bucket_locks = NULL; \
                (gcp)->buckets = NULL; \
                mtpndd_set_error(MTPNDD_ERROR_THREAD_SAFETY, __func__, __LINE__); \
                return MTPNDD_ERROR_THREAD_SAFETY; \
            } \
        } \
    } while(0)
#define GC_PROTECT_CLEAR(gcp) do { \
        size_t _gc_bucket_cnt = (gcp)->bucket_count ? (gcp)->bucket_count : g_mtpndd_pal_config.gc_bucket_count; \
        if (_gc_bucket_cnt == 0) { \
            _gc_bucket_cnt = MTPNDD_DEFAULT_GC_BUCKET_COUNT; \
        } \
        if ((gcp)->buckets && (gcp)->bucket_locks) { \
            for (size_t i = 0; i < _gc_bucket_cnt; i++) { \
                pthread_rwlock_t *_lock = &((gcp)->bucket_locks[i]); \
                if (pthread_rwlock_wrlock(_lock) != 0) { \
                    mtpndd_set_error(MTPNDD_ERROR_THREAD_SAFETY, __func__, __LINE__); \
                    continue; \
                } \
                gc_protect_entry_t *entry = (gcp)->buckets[i]; \
                while (entry) { \
                    gc_protect_entry_t *next_entry = entry->next; \
                    mtpndd_memory_release_gc_protect_entry(entry); \
                    entry = next_entry; \
                } \
                (gcp)->buckets[i] = NULL; \
                pthread_rwlock_unlock(_lock); \
            } \
        } else if ((gcp)->buckets) { \
            for (size_t i = 0; i < _gc_bucket_cnt; i++) { \
                gc_protect_entry_t *entry = (gcp)->buckets[i]; \
                while (entry) { \
                    gc_protect_entry_t *next_entry = entry->next; \
                    mtpndd_memory_release_gc_protect_entry(entry); \
                    entry = next_entry; \
                } \
                (gcp)->buckets[i] = NULL; \
            } \
        } \
        atomic_store_explicit(&(gcp)->gc_protect_count, 0, memory_order_relaxed); \
    } while(0)

#define GC_PROTECT_BUCKET_LOCK(gcp, bucket_idx) (&((gcp)->bucket_locks[bucket_idx]))
#define GC_PROTECT_BUCKET_RDLOCK(gcp, bucket_idx) pthread_rwlock_rdlock(GC_PROTECT_BUCKET_LOCK((gcp), (bucket_idx)))
#define GC_PROTECT_BUCKET_WRLOCK(gcp, bucket_idx) pthread_rwlock_wrlock(GC_PROTECT_BUCKET_LOCK((gcp), (bucket_idx)))
#define GC_PROTECT_BUCKET_UNLOCK(gcp, bucket_idx) pthread_rwlock_unlock(GC_PROTECT_BUCKET_LOCK((gcp), (bucket_idx)))
static inline size_t gc_protect_hash_ptr_impl(const mtpndd_gc_protect_t *gcp, const mtpndd_node_t *key) {
    size_t hash = mtpndd_hash_node_identity(key);
    size_t bucket_cnt = (gcp && gcp->bucket_count) ? gcp->bucket_count : g_mtpndd_pal_config.gc_bucket_count;
    if (bucket_cnt == 0) {
        bucket_cnt = MTPNDD_DEFAULT_GC_BUCKET_COUNT;
    }
    return bucket_cnt ? (size_t)(hash % bucket_cnt) : 0;
}

#define GC_PROTECT_HASH_VAL(gcp, key) gc_protect_hash_ptr_impl((gcp), (key))

#define GC_PROTECT_ENTRY_EQUAL(entry, key) ((entry->node) == (key))

static inline gc_protect_entry_t *gc_protect_bucket_find(mtpndd_gc_protect_t *gcp, size_t bucket_idx, mtpndd_node_t *key) {
    if (!gcp || !gcp->buckets || bucket_idx >= gcp->bucket_count) {
        return NULL;
    }
    gc_protect_entry_t *entry = gcp->buckets[bucket_idx];
#ifdef ENABLE_RECORDING
    uint64_t steps = 0;
#endif
    while (entry && !GC_PROTECT_ENTRY_EQUAL(entry, key)) {
#ifdef ENABLE_RECORDING
        steps++;
#endif
        entry = entry->next;
    }
#ifdef ENABLE_RECORDING
    if (entry) {
        steps++;
    }
    if (steps) {
        MTPNDD_STAT_ADD(gc_protect_lookup_steps_total, steps);
        MTPNDD_STAT_MAX(gc_protect_lookup_max_steps, steps);
    }
    if (entry) {
        MTPNDD_STAT_ADD(gc_protect_lookup_hits, 1);
    } else {
        MTPNDD_STAT_ADD(gc_protect_lookup_misses, 1);
    }
#endif
    return entry;
}

void mtpndd_gc_protect_clear();
void mtpndd_gc_protect_add(mtpndd_t *node);

#endif // MTPNDD_COMMON_H
