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
 * Global config definitions
 ********************************/
typedef struct mtpndd_pal_config_s {
    int32_t n_workers;
    size_t lace_dqsize;
    size_t bdd_nodetable_size;
    size_t mtpndd_nodetable_size;
    size_t op_cache_size;
    double quick_growth_threshold;
} mtpndd_pal_config_t;

extern mtpndd_pal_config_t g_mtpndd_pal_config;

typedef struct mtpndd_stats_s {
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t bdd_node_count;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t gc_count;
    double parallel_efficiency;
} mtpndd_stats_t;

extern mtpndd_stats_t g_mtpndd_stats;

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
// bool mtpndd_enable_reordering(bool enable);

mtpndd_error_t mtpndd_init(mtpndd_pal_config_t *config);
mtpndd_error_t mtpndd_quit();

/********************************
 * GC protection hash set
 ********************************/
/**
 * Why not use a gc_protect_label and container_of for gc protection hash set in mtpndd_node_t?
 * It will save time when removing.
 * But it will waste space for gc_protect_label in every node.
 */
#ifdef LARGE_NODETABLE
#define GC_PROTECT_BUCKET_CNT 65537
#else
#define GC_PROTECT_BUCKET_CNT 1024
#endif

struct mtpndd_gc_protect_s {
    atomic_size_t gc_protect_count;
    gc_protect_entry_t **buckets;
    pthread_rwlock_t *bucket_locks;
};

struct gc_protect_entry_s {
    struct gc_protect_entry_s *next;
    struct gc_protect_entry_s *prev;
    mtpndd_node_t *node;
};

#define GC_PROTECT_INIT(gcp) do { \
        atomic_init(&(gcp)->gc_protect_count, 0); \
        (gcp)->buckets = (gc_protect_entry_t **)calloc(GC_PROTECT_BUCKET_CNT, sizeof(gc_protect_entry_t *)); \
        if (!(gcp)->buckets) { \
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__); \
            return MTPNDD_ERROR_OUT_OF_MEMORY; \
        } \
        (gcp)->bucket_locks = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t) * GC_PROTECT_BUCKET_CNT); \
        if (!(gcp)->bucket_locks) { \
            free((gcp)->buckets); \
            (gcp)->buckets = NULL; \
            mtpndd_set_error(MTPNDD_ERROR_OUT_OF_MEMORY, __func__, __LINE__); \
            return MTPNDD_ERROR_OUT_OF_MEMORY; \
        } \
        for (size_t i = 0; i < GC_PROTECT_BUCKET_CNT; i++) { \
            if (pthread_rwlock_init(&(gcp)->bucket_locks[i], NULL) != 0) { \
                for (size_t j = 0; j < i; j++) { \
                    pthread_rwlock_destroy(&(gcp)->bucket_locks[j]); \
                } \
                free((gcp)->bucket_locks); \
                (gcp)->bucket_locks = NULL; \
                free((gcp)->buckets); \
                (gcp)->buckets = NULL; \
                mtpndd_set_error(MTPNDD_ERROR_THREAD_SAFETY, __func__, __LINE__); \
                return MTPNDD_ERROR_THREAD_SAFETY; \
            } \
        } \
    } while(0)
#define GC_PROTECT_CLEAR(gcp) do { \
        if ((gcp)->buckets && (gcp)->bucket_locks) { \
            for (size_t i = 0; i < GC_PROTECT_BUCKET_CNT; i++) { \
                pthread_rwlock_t *_lock = &((gcp)->bucket_locks[i]); \
                if (pthread_rwlock_wrlock(_lock) != 0) { \
                    mtpndd_set_error(MTPNDD_ERROR_THREAD_SAFETY, __func__, __LINE__); \
                    continue; \
                } \
                gc_protect_entry_t *entry = (gcp)->buckets[i]; \
                while (entry) { \
                    gc_protect_entry_t *next_entry = entry->next; \
                    free(entry); \
                    entry = next_entry; \
                } \
                (gcp)->buckets[i] = NULL; \
                pthread_rwlock_unlock(_lock); \
            } \
        } else if ((gcp)->buckets) { \
            for (size_t i = 0; i < GC_PROTECT_BUCKET_CNT; i++) { \
                gc_protect_entry_t *entry = (gcp)->buckets[i]; \
                while (entry) { \
                    gc_protect_entry_t *next_entry = entry->next; \
                    free(entry); \
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

static inline size_t gc_protect_hash_ptr_impl(const mtpndd_node_t *key) {
    size_t hash = mtpndd_hash_node_identity(key);
#if ((GC_PROTECT_BUCKET_CNT & (GC_PROTECT_BUCKET_CNT - 1)) == 0)
    return (size_t)(hash & (GC_PROTECT_BUCKET_CNT - 1));
#else
    return (size_t)(hash % GC_PROTECT_BUCKET_CNT);
#endif
}

#define GC_PROTECT_HASH_VAL(key) gc_protect_hash_ptr_impl(key)

#define GC_PROTECT_ENTRY_EQUAL(entry, key) ((entry->node) == (key))

#define FOR_EACH_ENTRY_IN_GC_PROTECT_BUCKET(gcp, bucket_idx, entry) \
    for (entry = gcp->buckets[bucket_idx]; \
        entry; \
        entry = entry->next)
#define FOR_EACH_ENTRY_IN_ALL_GC_PROTECT_BUCKETS(gcp, entry) \
    for (size_t _bkt = 0; _bkt < GC_PROTECT_BUCKET_CNT; _bkt++) \
        FOR_EACH_ENTRY_IN_GC_PROTECT_BUCKET(gcp, _bkt, entry)

static inline gc_protect_entry_t *gc_protect_bucket_find(mtpndd_gc_protect_t *gcp, size_t bucket_idx, mtpndd_node_t *key) {
    gc_protect_entry_t *entry = gcp->buckets[bucket_idx];
    while (entry && !GC_PROTECT_ENTRY_EQUAL(entry, key)) {
        entry = entry->next;
    }
    return entry;
}

void mtpndd_gc_protect_clear();
mtpndd_error_t mtpndd_gc_protect_add(mtpndd_t *node);
mtpndd_error_t mtpndd_gc_protect_remove(mtpndd_t *node);
bool mtpndd_gc_protect_contains(mtpndd_t *node);

#endif // MTPNDD_COMMON_H
