// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#ifndef MTPNDD_COMMON_H
#define MTPNDD_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "mtpndd_nodetable.h"
#include "mtpndd_operation_cache.h"

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

static const char* mtpndd_error_messages[] = {
    "Success",
    "Invalid parameter",
    "NDD system initialization failed",
    "NDD system not initialized",
    "NDD system already initialized",
    "Out of memory",
    "Invalid field ID",
    "Null pointer",
    "Capacity exceeded",
    "Parallel initialization failed",
    "BDD operation failed",
    "Thread safety error",

    "Unknown error"
};

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
    uint32_t n_workers;
    size_t lace_dqsize;
    size_t bdd_nodetable_size;
    size_t mtpndd_nodetable_size;
    size_t op_cache_size;
    double quick_growth_threshold;
} mtpndd_pal_config_t;

static mtpndd_pal_config_t g_mtpndd_pal_config = {0};

typedef struct mtpndd_stats_s {
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t bdd_node_count;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t gc_count;
    double parallel_efficiency;
} mtpndd_stats_t;

static mtpndd_stats_t g_mtpndd_stats = {0};

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

static mtpndd_field_info_t MTPNDD_TERMINAL_FIELD = {0, 0, 0};

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

static mtpndd_config_t g_mtpndd_config = {
    0, NULL, NULL, NULL, NULL, NULL
};

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
 * GC protect
 ********************************/
#define GC_PROTECT_BUCKET_CNT 1024
typedef struct mtpndd_gc_protect_s {

} mtpndd_gc_protect_t;

#endif // MTPNDD_COMMON_H