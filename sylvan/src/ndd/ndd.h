// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef NDD_H
#define NDD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Performance monitoring system
#include "ndd_performance_monitor.h"
// Common containers and hash tables
#include "common.h"

// Error handling system
typedef enum ndd_error_e {
    NDD_SUCCESS = 0,           // Success
    NDD_ERROR_INVALID_PARAM,   // Invalid parameter
    NDD_ERROR_NOT_INITIALIZED, // Not initialized
    NDD_ERROR_OUT_OF_MEMORY,   // Out of memory
    NDD_ERROR_INVALID_FIELD,   // Invalid field
    NDD_ERROR_NULL_POINTER,    // Null pointer
    NDD_ERROR_CAPACITY_EXCEEDED, // Capacity exceeded
    NDD_ERROR_PARALLEL_INIT,   // Parallel initialization failed
    NDD_ERROR_BDD_OPERATION,   // BDD operation failed
    NDD_ERROR_THREAD_SAFETY,   // Thread safety error
    NDD_ERROR_UNKNOWN          // Unknown error
} ndd_error_t;

// Error information structure
typedef struct ndd_error_info_s {
    ndd_error_t code;
    const char *message;
    const char *function;
    int line;
} ndd_error_info_t;

// Error handling functions
const char* ndd_error_string(ndd_error_t error);
void ndd_set_error(ndd_error_t error, const char *function, int line);
ndd_error_info_t ndd_get_last_error();
void ndd_clear_error();

// Error handling macros
#define NDD_SET_ERROR(error) ndd_set_error(error, __FUNCTION__, __LINE__)
#define NDD_RETURN_ERROR(error) do { NDD_SET_ERROR(error); return error; } while(0)
#define NDD_RETURN_NULL_ON_ERROR(error) do { NDD_SET_ERROR(error); return (ndd_t){0}; } while(0)
#define NDD_RETURN_FALSE_ON_ERROR(error) do { NDD_SET_ERROR(error); return ndd_false(); } while(0)

// Parameter validation macros
#define NDD_CHECK_PARAM(condition, error) \
    do { if (!(condition)) { NDD_RETURN_ERROR(error); } } while(0)
#define NDD_CHECK_NULL(ptr, error) \
    do { if ((ptr) == NULL) { NDD_RETURN_ERROR(error); } } while(0)
#define NDD_CHECK_INIT() \
    do { if (!ndd_is_initialized()) { NDD_RETURN_ERROR(NDD_ERROR_NOT_INITIALIZED); } } while(0)

// Parameter validation macros (return ndd_t)
#define NDD_CHECK_PARAM_NDD(condition, error) \
    do { if (!(condition)) { NDD_RETURN_NULL_ON_ERROR(error); } } while(0)
#define NDD_CHECK_NULL_NDD(ptr, error) \
    do { if ((ptr) == NULL) { NDD_RETURN_NULL_ON_ERROR(error); } } while(0)
#define NDD_CHECK_INIT_NDD() \
    do { if (!ndd_is_initialized()) { NDD_RETURN_NULL_ON_ERROR(NDD_ERROR_NOT_INITIALIZED); } } while(0)

#ifdef HAVE_LACE_SYLVAN
// Use real Sylvan BDD
#include <sylvan.h>
#include <lace.h>
typedef BDD ndd_bdd_t;
#define ndd_sylvan_true sylvan_true
#define ndd_sylvan_false sylvan_false
#else
#error "NDD requires Lace + Sylvan framework. Please ensure HAVE_LACE_SYLVAN is defined."
#endif

typedef int64_t ndd_bdd_t;

// Unified NDD node structure definition
typedef struct ndd_s {
    uint32_t field;           // Field ID
    hash_table_t *edges_map;  // BDD edge label hash table (NDD* -> BDD*)
    uint32_t edge_count;      // Current edge count
    uint32_t ref_count;       // Reference count
} ndd_node_t;

// NDD type - direct node pointer
typedef ndd_node_t* ndd_t;

// Field information
typedef struct ndd_field_info_s {
    uint32_t field_id;
    uint32_t bit_width;
    uint32_t start_var;
    uint32_t end_var;
} ndd_field_info_t;

// Configuration structure
typedef struct ndd_config_s {
    uint32_t n_workers;
    size_t dqsize;
    size_t table_size;
    size_t cache_size;
    uint32_t gc_threshold;
} ndd_config_t;

// Statistics information
typedef struct ndd_stats_s {
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t bdd_node_count;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t gc_count;
    double parallel_efficiency;
} ndd_stats_t;

// Global state management
extern uint32_t g_field_count;
extern ndd_field_info_t *g_field_info;
extern uint32_t g_field_capacity;

// Initialization and cleanup
int ndd_init(ndd_config_t *config);
void ndd_quit();
bool ndd_is_initialized();  // Check initialization status

// Field management
ndd_error_t ndd_declare_field_safe(uint32_t bit_width, uint32_t *field_id);  // Safe version
uint32_t ndd_declare_field(uint32_t bit_width);  // Compatible version
ndd_field_info_t* ndd_get_field_info(uint32_t field_id);

// Terminal nodes
ndd_t ndd_true();
ndd_t ndd_false();
bool ndd_is_true(ndd_t ndd);
bool ndd_is_false(ndd_t ndd);
bool ndd_is_terminal(ndd_t ndd);

// Node creation and management
ndd_error_t ndd_create_node_safe(uint32_t field, ndd_t *result);  // Safe version
ndd_t ndd_create_node(uint32_t field);  // Compatible version
ndd_error_t ndd_add_edge_safe(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd);  // Safe version
void ndd_add_edge(ndd_t *ndd, ndd_t descendant, ndd_bdd_t label_bdd);  // Compatible version
ndd_t ndd_find_edge(ndd_t ndd, ndd_t descendant);
// Java version mk equivalent: create/reuse node based on field and edge set (deduplication)
ndd_t ndd_mk(uint32_t field, hash_table_t *edges_map);

// Logic operations - unified parallel implementation
ndd_t ndd_and(ndd_t a, ndd_t b);
ndd_t ndd_or(ndd_t a, ndd_t b);
ndd_t ndd_not(ndd_t a);
ndd_t ndd_diff(ndd_t a, ndd_t b);
ndd_t ndd_exist(ndd_t a, uint32_t field);

// Backward compatible parallel interface (calls unified implementation internally)
#define ndd_and_parallel(a, b) ndd_and(a, b)
#define ndd_or_parallel(a, b) ndd_or(a, b)
#define ndd_not_parallel(a) ndd_not(a)
#define ndd_diff_parallel(a, b) ndd_diff(a, b)
#define ndd_exist_parallel(a, field) ndd_exist(a, field)

// Encoding operations
ndd_error_t ndd_encode_prefix_safe(uint32_t* prefix_binary, uint32_t len, uint32_t field, ndd_t *result);
ndd_t ndd_encode_prefix(uint32_t* prefix_binary, uint32_t len, uint32_t field);  // Compatible version
ndd_error_t ndd_from_bdd_safe(ndd_bdd_t bdd, uint32_t field, ndd_t *result);
ndd_t ndd_from_bdd(ndd_bdd_t bdd, uint32_t field);  // Compatible version
ndd_error_t ndd_to_bdd_safe(ndd_t ndd, ndd_bdd_t *result);
ndd_bdd_t ndd_to_bdd(ndd_t ndd);  // Compatible version

// Reference counting and GC - unified memory management system
ndd_t ndd_ref(ndd_t ndd);
void ndd_deref(ndd_t ndd);
void ndd_gc();

// Thread-safe reference counting operations
ndd_t ndd_ref_safe(ndd_t ndd);
void ndd_deref_safe(ndd_t ndd);

// Memory management statistics (using performance monitoring system definitions)

ndd_memory_stats_t ndd_get_memory_stats();
void ndd_print_memory_stats();

// Statistics information
ndd_stats_t ndd_get_stats();
void ndd_print_stats();

// Performance optimization
void ndd_set_gc_threshold(uint32_t threshold);
void ndd_enable_reordering(bool enable);
void ndd_set_cache_ratio(double ratio);

// Parallel task management
void ndd_spawn_task(void (*task_func)(void*), void *arg);
ndd_t ndd_sync_task(ndd_t result);

// Lace framework management functions
int ndd_lace_init(uint32_t n_workers, size_t dqsize);
void ndd_lace_cleanup();

// Parallel support check
bool ndd_parallel_available();

// Parallel BDD operation interface
ndd_bdd_t ndd_bdd_and_parallel(ndd_bdd_t a, ndd_bdd_t b);
ndd_bdd_t ndd_bdd_or_parallel(ndd_bdd_t a, ndd_bdd_t b);
ndd_bdd_t ndd_bdd_not_parallel(ndd_bdd_t a);

#endif // NDD_H
