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

#ifdef ENABLE_RECORDING
#include <time.h>
#endif

struct mtpndd_nodetable_s;
struct mtpndd_op_cache_s;
struct mtpndd_gc_protect_s;

// MTPNDD node identifier (index into nodetable data array)
typedef uint64_t mtpndd_t;
// Sylvan BDD node identifier
typedef uint64_t mtpndd_bdd_t;

typedef struct mtpndd_nodetable_s mtpndd_nodetable_t;
typedef struct mtpndd_op_cache_s mtpndd_op_cache_t;
typedef struct mtpndd_gc_protect_s mtpndd_gc_protect_t;

static const mtpndd_t MTPNDD_FALSE = 0;
static const mtpndd_t MTPNDD_TRUE = 1;
static const mtpndd_t MTPNDD_INVALID = UINT64_MAX;

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

    // Sylvan unique table / computed cache sizing. If `sylvan_memory_cap` is non-zero,
    // mtpndd_init will use `sylvan_set_limits` with the provided ratio parameters.
    // Otherwise it uses fixed sizes via `sylvan_set_sizes`.
    size_t sylvan_memory_cap;
    int32_t sylvan_table_ratio;
    int32_t sylvan_initial_ratio;
    int32_t sylvan_granularity;

    size_t bdd_nodetable_size;
    // MTPNDD nodetable sizing (counts, not bytes)
    // - mtpndd_nodetable_size: initial/min node record capacity (data[])
    // - mtpndd_nodetable_max_size: maximum node record capacity (data[]), growth may double up to this limit
    size_t mtpndd_nodetable_size;
    size_t mtpndd_nodetable_max_size;
    // op_cache_size: Sylvan computed cache size when using `sylvan_set_sizes` (must be power-of-two).
    // mtpndd_op_cache_size: MTPNDD-level operation cache size (and/or/not), independent from Sylvan.
    size_t op_cache_size;
    size_t mtpndd_op_cache_size;
    double quick_growth_threshold;
    size_t edge_bucket_count;
    // nodetable_bucket_count: initial/min hash slot capacity (hash[])
    // nodetable_bucket_max_count: maximum hash slot capacity (hash[])
    size_t nodetable_bucket_count;
    size_t nodetable_bucket_max_count;
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
    uint64_t recording_enabled;
    uint64_t max_edges_per_node;
    uint64_t bdd_nodes_converted;
    uint64_t cache_lookup_hits;
    uint64_t cache_lookup_misses;
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

#ifndef MTPNDD_CACHELINE_BYTES
#define MTPNDD_CACHELINE_BYTES 64
#endif

typedef struct mtpndd_field_info_s {
    uint32_t field_id;
    uint32_t bit_width;
    uint32_t start_var;

    // For internal use
    mtpndd_bdd_t *bdd_vars;
    mtpndd_bdd_t *bdd_not_vars;
    mtpndd_t *mtpndd_vars;
    mtpndd_t *mtpndd_not_vars;
} mtpndd_field_info_t;

extern mtpndd_field_info_t MTPNDD_TERMINAL_FIELD;

typedef struct mtpndd_config_s {
    uint32_t field_count;
    uint32_t field_capacity;
    mtpndd_field_info_t **field_info;

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
mtpndd_t mtpndd_get_var(uint32_t field, uint32_t var_index);
mtpndd_t mtpndd_get_not_var(uint32_t field, uint32_t var_index);
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
// Serial GC-protect set for keeping intermediate nodes alive across GC.
struct mtpndd_gc_protect_s {
    mtpndd_t *slots;    // open addressing, 0 means empty; stores (node_idx+1)
    size_t capacity;    // power of two
    size_t mask;
    size_t count;
};

void mtpndd_gc_protect_clear(void);
void mtpndd_gc_protect_add(mtpndd_t node);
void mtpndd_gc_protect_remove(mtpndd_t node);
bool mtpndd_gc_protect_contains(mtpndd_t node);

/********************************
 * Garbage collection
 ********************************/
/**
 * Stop-the-world GC for the serial MTPNDD runtime.
 *
 * - Clears operation caches (to avoid stale idx after node reclamation).
 * - Reclaims nodes whose `ref_count==0` and are not protected (incl. field vars) and not in gcProtect.
 * - Optionally compacts edge_array_pool when fragmentation is high.
 *
 * Returns number of reclaimed nodes (0 on no-op / failure).
 */
size_t mtpndd_gc_collect(void);

#endif // MTPNDD_COMMON_H
