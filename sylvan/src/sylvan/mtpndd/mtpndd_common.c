// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_common.h"
#include "mtpndd_node.h"
#include "mtpndd_nodetable.h"
#include "mtpndd_operation_cache.h"
#include "mtpndd_memory_pool.h"

#include <stdlib.h>
#include <string.h>
#include <lace.h>
#include "sylvan.h"
#include "sylvan_table.h"
#include <stdatomic.h>

/********************************
 * Global singletons and defaults
 ********************************/
mtpndd_pal_config_t g_mtpndd_pal_config = {0};
mtpndd_stats_t g_mtpndd_stats = {0};
mtpndd_field_info_t MTPNDD_TERMINAL_FIELD = {0, 0, 0, NULL, NULL, NULL, NULL};
mtpndd_config_t g_mtpndd_config = {
    .field_count = 0,
    .field_capacity = 0,
    .field_info = NULL,
    .node_tables_by_field = NULL,
    .and_cache = NULL,
    .or_cache = NULL,
    .not_cache = NULL,
    .gcProtect = NULL,
};

#define MTPNDD_GC_HOOK_CAPACITY 16
static mtpndd_gc_hook_t g_mtpndd_gc_prehooks[MTPNDD_GC_HOOK_CAPACITY] = {0};
static mtpndd_gc_hook_t g_mtpndd_gc_posthooks[MTPNDD_GC_HOOK_CAPACITY] = {0};
static _Atomic size_t g_mtpndd_gc_prehook_count = 0;
static _Atomic size_t g_mtpndd_gc_posthook_count = 0;

#define DEFAULT_QUICK_GROWTH_THRESHOLD 0.1
#define DEFAULT_FIELD_CAPACITY 8

/********************************
 * Internal helpers
 ********************************/
static bool mtpndd_lace_init(void);
static void mtpndd_gc_run_hooks(mtpndd_gc_hook_t *hooks, size_t count);
static void mtpndd_gc_hook_sylvan_pre(WorkerP *worker, Task *task);
static void mtpndd_gc_hook_sylvan_post(WorkerP *worker, Task *task);
static void mtpndd_gc_hook_mtpndd_pre(void);
static void mtpndd_gc_hook_mtpndd_post(void);

static void mtpndd_field_info_teardown(mtpndd_field_info_t *field) {
    if (!field) {
        return;
    }
    uint32_t width = field->bit_width;
    if (field->mtpndd_vars) {
        for (uint32_t i = 0; i < width; ++i) {
            mtpndd_node_t *node = field->mtpndd_vars[i];
            if (node) {
                if (node->edges) {
                    mtpndd_edge_map_free(node->edges);
                    node->edges = NULL;
                }
                mtpndd_memory_release_node(node);
                field->mtpndd_vars[i] = NULL;
            }
        }
        free(field->mtpndd_vars);
        field->mtpndd_vars = NULL;
    }
    if (field->mtpndd_not_vars) {
        for (uint32_t i = 0; i < width; ++i) {
            mtpndd_node_t *node = field->mtpndd_not_vars[i];
            if (node) {
                if (node->edges) {
                    mtpndd_edge_map_free(node->edges);
                    node->edges = NULL;
                }
                mtpndd_memory_release_node(node);
                field->mtpndd_not_vars[i] = NULL;
            }
        }
        free(field->mtpndd_not_vars);
        field->mtpndd_not_vars = NULL;
    }
    if (field->bdd_vars) {
        for (uint32_t i = 0; i < width; ++i) {
            sylvan_unprotect(field->bdd_vars + i);
        }
        free(field->bdd_vars);
        field->bdd_vars = NULL;
    }
    if (field->bdd_not_vars) {
        for (uint32_t i = 0; i < width; ++i) {
            sylvan_unprotect(field->bdd_not_vars + i);
        }
        free(field->bdd_not_vars);
        field->bdd_not_vars = NULL;
    }
}

/********************************
 * Error handling system
 ********************************/
static __thread mtpndd_error_info_t g_last_error = {MTPNDD_SUCCESS, NULL, NULL, 0};

const char* mtpndd_error_messages[] = {
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

const char* mtpndd_error_string(mtpndd_error_t error) {
    if (error >= 0 && error <= MTPNDD_ERROR_UNKNOWN) {
        return mtpndd_error_messages[error];
    }
    return "Invalid error code";
}

void mtpndd_set_error(mtpndd_error_t error, const char *function, int line) {
    g_last_error.code = error;
    g_last_error.message = mtpndd_error_string(error);
    g_last_error.function = function;
    g_last_error.line = line;
}

mtpndd_error_info_t mtpndd_get_last_error() {
    return g_last_error;
}

void mtpndd_clear_error() {
    g_last_error.code = MTPNDD_SUCCESS;
    g_last_error.message = NULL;
    g_last_error.function = NULL;
    g_last_error.line = 0;
}

/********************************
 * GC hooks
 ********************************/
void mtpndd_gc_hook_pregc(mtpndd_gc_hook_t hook) {
    if (!hook) {
        return;
    }
    size_t idx = __atomic_load_n(&g_mtpndd_gc_prehook_count, __ATOMIC_RELAXED);
    if (idx >= MTPNDD_GC_HOOK_CAPACITY) {
        return;
    }
    g_mtpndd_gc_prehooks[idx] = hook;
    __atomic_store_n(&g_mtpndd_gc_prehook_count, idx + 1, __ATOMIC_RELAXED);
}

void mtpndd_gc_hook_postgc(mtpndd_gc_hook_t hook) {
    if (!hook) {
        return;
    }
    size_t idx = __atomic_load_n(&g_mtpndd_gc_posthook_count, __ATOMIC_RELAXED);
    if (idx >= MTPNDD_GC_HOOK_CAPACITY) {
        return;
    }
    g_mtpndd_gc_posthooks[idx] = hook;
    __atomic_store_n(&g_mtpndd_gc_posthook_count, idx + 1, __ATOMIC_RELAXED);
}

static void mtpndd_gc_run_hooks(mtpndd_gc_hook_t *hooks, size_t count) {
    if (!hooks) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        mtpndd_gc_hook_t hook = hooks[i];
        if (hook) {
            hook();
        }
    }
}

void mtpndd_gc_run_prehooks(void) {
    size_t count = __atomic_load_n(&g_mtpndd_gc_prehook_count, __ATOMIC_RELAXED);
    mtpndd_gc_run_hooks(g_mtpndd_gc_prehooks, count);
}

void mtpndd_gc_run_posthooks(void) {
    size_t count = __atomic_load_n(&g_mtpndd_gc_posthook_count, __ATOMIC_RELAXED);
    mtpndd_gc_run_hooks(g_mtpndd_gc_posthooks, count);
}

/********************************
 * MTPNDD field management
 ********************************/
mtpndd_field_info_t* mtpndd_get_field_info(uint32_t field_id) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return NULL;
    }
    if (field_id == 0 || field_id > g_mtpndd_config.field_count) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return NULL;
    }

    return g_mtpndd_config.field_info[field_id];
}

// only append field
mtpndd_error_t mtpndd_declare_field(uint32_t bit_width) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(bit_width > 0, MTPNDD_ERROR_INVALID_PARAM);

    g_mtpndd_config.field_count++;

    // check and expand capacity
    if (g_mtpndd_config.field_count >= g_mtpndd_config.field_capacity) {
        uint32_t old_capacity = g_mtpndd_config.field_capacity;
        uint32_t new_capacity = old_capacity ? old_capacity * 2 : DEFAULT_FIELD_CAPACITY;
        if (new_capacity <= g_mtpndd_config.field_count) {
            new_capacity = g_mtpndd_config.field_count + 1;
        }

        size_t fi_size = sizeof(mtpndd_field_info_t*) * new_capacity;
        size_t nt_size = sizeof(mtpndd_nodetable_t*) * new_capacity;
        size_t old_fi_bytes = sizeof(mtpndd_field_info_t*) * old_capacity;
        size_t old_nt_bytes = sizeof(mtpndd_nodetable_t*) * old_capacity;

        mtpndd_field_info_t **new_field_info =
                (mtpndd_field_info_t **)malloc(fi_size);
        void *aligned_nt = NULL;
        if (posix_memalign(&aligned_nt, 64, nt_size) != 0) {
            free(new_field_info);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        mtpndd_nodetable_t **new_node_tables =
                (mtpndd_nodetable_t **)aligned_nt;
        if (!new_field_info || !new_node_tables) {
            free(new_field_info);
            free(new_node_tables);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        // copy existing slots
        if (g_mtpndd_config.field_info && old_capacity > 0) {
            memcpy(new_field_info, g_mtpndd_config.field_info, old_fi_bytes);
        }
        if (g_mtpndd_config.node_tables_by_field && old_capacity > 0) {
            memcpy(new_node_tables, g_mtpndd_config.node_tables_by_field, old_nt_bytes);
        }
        // zero-initialize the newly added slots only
        if (new_capacity > old_capacity) {
            size_t fi_bytes = sizeof(mtpndd_field_info_t*) * (new_capacity - old_capacity);
            size_t nt_bytes = sizeof(mtpndd_nodetable_t*) * (new_capacity - old_capacity);
            memset(new_field_info + old_capacity, 0, fi_bytes);
            memset(new_node_tables + old_capacity, 0, nt_bytes);
        }

        free(g_mtpndd_config.field_info);
        free(g_mtpndd_config.node_tables_by_field);

        g_mtpndd_config.field_info = new_field_info;
        g_mtpndd_config.node_tables_by_field = new_node_tables;
        g_mtpndd_config.field_capacity = new_capacity;
    }

    mtpndd_field_info_t *new_field = (mtpndd_field_info_t *)malloc(sizeof(mtpndd_field_info_t));
    if (!new_field) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    memset(new_field, 0, sizeof(mtpndd_field_info_t));
    new_field->field_id = g_mtpndd_config.field_count;
    new_field->bit_width = bit_width;
    mtpndd_field_info_t *prev_field = g_mtpndd_config.field_info[g_mtpndd_config.field_count - 1];
    new_field->start_var = g_mtpndd_config.field_count == 1 ? 0 :
        prev_field->start_var + prev_field->bit_width;
    
    // node table initialize
    mtpndd_nodetable_t *nodetable = mtpndd_nodetable_declare_field();
    if (!nodetable) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.node_tables_by_field[g_mtpndd_config.field_count] = nodetable;

#define FREE_BDD_VARS() do { \
        if (new_field->bdd_vars) { \
            for (uint32_t _k = 0; _k < (bit_width); ++_k) { \
                sylvan_unprotect(new_field->bdd_vars + _k); \
            } \
            free(new_field->bdd_vars); \
            new_field->bdd_vars = NULL; \
        } \
        if (new_field->bdd_not_vars) { \
            for (uint32_t _k = 0; _k < (bit_width); ++_k) { \
                sylvan_unprotect(new_field->bdd_not_vars + _k); \
            } \
            free(new_field->bdd_not_vars); \
            new_field->bdd_not_vars = NULL; \
        } \
        free(new_field); \
    } while(0)
#define FREE_MTPNDD_VARS() do { \
        free(new_field->mtpndd_vars); \
        free(new_field->mtpndd_not_vars); \
        FREE_BDD_VARS(); \
    } while(0)
#define FREE_MTPNDD_VARS_WITH_NODES(i) do { \
        for (uint32_t j = 0; j < (i); j++) { \
            if (new_field->mtpndd_vars && new_field->mtpndd_vars[j]) { \
                mtpndd_edge_map_free(new_field->mtpndd_vars[j]->edges); \
                new_field->mtpndd_vars[j] = NULL; \
            } \
            if (new_field->mtpndd_not_vars && new_field->mtpndd_not_vars[j]) { \
                mtpndd_edge_map_free(new_field->mtpndd_not_vars[j]->edges); \
                new_field->mtpndd_not_vars[j] = NULL; \
            } \
        } \
        FREE_MTPNDD_VARS(); \
    } while(0)

    // Allocate BDD and NDD variables for the field
    // BDD variables
    new_field->bdd_vars = (mtpndd_bdd_t *)malloc(sizeof(mtpndd_bdd_t) * bit_width);
    new_field->bdd_not_vars = (mtpndd_bdd_t *)malloc(sizeof(mtpndd_bdd_t) * bit_width);
    if (!new_field->bdd_vars || !new_field->bdd_not_vars) {
        FREE_BDD_VARS();
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    memset(new_field->bdd_vars, 0, sizeof(mtpndd_bdd_t) * bit_width);
    memset(new_field->bdd_not_vars, 0, sizeof(mtpndd_bdd_t) * bit_width);
    for (uint32_t i = 0; i < bit_width; i++) {
        new_field->bdd_vars[i] = sylvan_ithvar(new_field->start_var + i);
        sylvan_protect(new_field->bdd_vars + i);
        new_field->bdd_not_vars[i] = sylvan_not(new_field->bdd_vars[i]);
        sylvan_protect(new_field->bdd_not_vars + i);
    }
    // use mtpndd_mk so that these mtpndd_node_t will be added into node table
    // NDD variables
    new_field->mtpndd_vars = (mtpndd_node_t **)malloc(sizeof(mtpndd_node_t *) * bit_width);
    new_field->mtpndd_not_vars = (mtpndd_node_t **)malloc(sizeof(mtpndd_node_t *) * bit_width);
    if (!new_field->mtpndd_vars || !new_field->mtpndd_not_vars) {
        FREE_MTPNDD_VARS();
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    memset(new_field->mtpndd_vars, 0, sizeof(mtpndd_node_t *) * bit_width);
    memset(new_field->mtpndd_not_vars, 0, sizeof(mtpndd_node_t *) * bit_width);
    mtpndd_error_t err = MTPNDD_SUCCESS;
    for (uint32_t i = 0; i < bit_width; i++) {
        // build var node
        mtpndd_edge_t *edges_var = mtpndd_memory_acquire_edge_map();
        if (!edges_var) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        mtpndd_edge_map_init(edges_var);
        err = mtpndd_add_edge(edges_var, &MTPNDD_TRUE, sylvan_ref(new_field->bdd_vars[i]));
        if (err != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(edges_var);
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        mtpndd_node_t *node_var = NULL;
        mtpndd_mk(new_field->field_id, edges_var, &node_var);
        if (!node_var) {
            mtpndd_edge_map_free(edges_var);
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(mtpndd_get_last_error().code);
        }
        err = mtpndd_protect(node_var);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        new_field->mtpndd_vars[i] = node_var;

        // build not var node
        mtpndd_edge_t *edges_not = mtpndd_memory_acquire_edge_map();
        if (!edges_not) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        mtpndd_edge_map_init(edges_not);
        err = mtpndd_add_edge(edges_not, &MTPNDD_TRUE, sylvan_ref(new_field->bdd_not_vars[i]));
        if (err != MTPNDD_SUCCESS) {
            mtpndd_edge_map_free(edges_not);
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        mtpndd_node_t *node_not = NULL;
        mtpndd_mk(new_field->field_id, edges_not, &node_not);
        if (!node_not) {
            mtpndd_edge_map_free(edges_not);
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(mtpndd_get_last_error().code);
        }
        err = mtpndd_protect(node_not);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        new_field->mtpndd_not_vars[i] = node_not;
    }

    g_mtpndd_config.field_info[g_mtpndd_config.field_count] = new_field;

    return MTPNDD_SUCCESS;
}

/********************************
 * MTPNDD get node
 ********************************/
mtpndd_t *mtpndd_get_var(uint32_t field, uint32_t var_index) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return NULL;
    }
    if (field == 0 || field > g_mtpndd_config.field_count) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return NULL;
    }
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    if (!field_info || var_index >= field_info->bit_width) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_PARAM);
        return NULL;
    }

    return field_info->mtpndd_vars[var_index];
}

mtpndd_t *mtpndd_get_not_var(uint32_t field, uint32_t var_index) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return NULL;
    }
    if (field == 0 || field > g_mtpndd_config.field_count) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return NULL;
    }
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    if (!field_info || var_index >= field_info->bit_width) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_PARAM);
        return NULL;
    }

    return field_info->mtpndd_not_vars[var_index];
}

mtpndd_bdd_t mtpndd_get_bdd_var(uint32_t field, uint32_t var_index) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return 0;
    }
    if (field == 0 || field > g_mtpndd_config.field_count) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return 0;
    }
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    if (!field_info || var_index >= field_info->bit_width) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_PARAM);
        return 0;
    }

    return field_info->bdd_vars[var_index];
}

mtpndd_bdd_t mtpndd_get_bdd_not_var(uint32_t field, uint32_t var_index) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return 0;
    }
    if (field == 0 || field > g_mtpndd_config.field_count) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return 0;
    }
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    if (!field_info || var_index >= field_info->bit_width) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_PARAM);
        return 0;
    }

    return field_info->bdd_not_vars[var_index];
}

/********************************
 * Global status control
 ********************************/
mtpndd_stats_t *mtpndd_get_stats() {
    return &g_mtpndd_stats;
}

bool mtpndd_is_initialized() {
    return g_mtpndd_pal_config.lace_dqsize > 0
            || g_mtpndd_pal_config.bdd_nodetable_size > 0
            || g_mtpndd_pal_config.mtpndd_nodetable_size > 0
            || g_mtpndd_pal_config.op_cache_size > 0
            || g_mtpndd_config.field_count > 0;
}

static void mtpndd_apply_pal_config_defaults(void) {
    if (g_mtpndd_pal_config.edge_bucket_count == 0) {
        g_mtpndd_pal_config.edge_bucket_count = MTPNDD_DEFAULT_EDGE_BUCKET_COUNT;
    }
    if (g_mtpndd_pal_config.nodetable_bucket_count == 0) {
        g_mtpndd_pal_config.nodetable_bucket_count = MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
    }
    if (g_mtpndd_pal_config.gc_bucket_count == 0) {
        g_mtpndd_pal_config.gc_bucket_count = MTPNDD_DEFAULT_GC_BUCKET_COUNT;
    }
    if (g_mtpndd_pal_config.node_slab_capacity == 0) {
        g_mtpndd_pal_config.node_slab_capacity = MTPNDD_DEFAULT_NODE_SLAB_CAPACITY;
    }
    if (g_mtpndd_pal_config.edge_entry_slab_capacity == 0) {
        g_mtpndd_pal_config.edge_entry_slab_capacity = MTPNDD_DEFAULT_EDGE_ENTRY_SLAB_CAPACITY;
    }
    if (g_mtpndd_pal_config.nodetable_entry_slab_capacity == 0) {
        g_mtpndd_pal_config.nodetable_entry_slab_capacity = MTPNDD_DEFAULT_NODETABLE_ENTRY_SLAB_CAPACITY;
    }
    if (g_mtpndd_pal_config.edge_map_slab_capacity == 0) {
        g_mtpndd_pal_config.edge_map_slab_capacity = MTPNDD_DEFAULT_EDGE_MAP_SLAB_CAPACITY;
    }
    if (g_mtpndd_pal_config.gc_protect_entry_slab_capacity == 0) {
        g_mtpndd_pal_config.gc_protect_entry_slab_capacity = MTPNDD_DEFAULT_GC_PROTECT_ENTRY_SLAB_CAPACITY;
    }
}

mtpndd_error_t mtpndd_init(mtpndd_pal_config_t *config) {
    if (mtpndd_is_initialized()) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_IS_INITIALIZED);
    }

    if (config == NULL || !(
            config->n_workers >= 0
            && config->lace_dqsize > 0
            && config->bdd_nodetable_size > 0
            && config->mtpndd_nodetable_size > 0
            && config->op_cache_size > 0)) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_INVALID_PARAM);
    }
    
    memset(&g_mtpndd_pal_config, 0, sizeof(g_mtpndd_pal_config));
    g_mtpndd_pal_config.n_workers = config->n_workers;
    g_mtpndd_pal_config.lace_dqsize = config->lace_dqsize;
    g_mtpndd_pal_config.bdd_nodetable_size = config->bdd_nodetable_size;
    g_mtpndd_pal_config.mtpndd_nodetable_size = config->mtpndd_nodetable_size;
    g_mtpndd_pal_config.op_cache_size = config->op_cache_size;
    g_mtpndd_pal_config.quick_growth_threshold = DEFAULT_QUICK_GROWTH_THRESHOLD;
    g_mtpndd_pal_config.edge_bucket_count = config->edge_bucket_count;
    g_mtpndd_pal_config.nodetable_bucket_count = config->nodetable_bucket_count;
    g_mtpndd_pal_config.gc_bucket_count = config->gc_bucket_count;
    g_mtpndd_pal_config.node_slab_capacity = config->node_slab_capacity;
    g_mtpndd_pal_config.edge_entry_slab_capacity = config->edge_entry_slab_capacity;
    g_mtpndd_pal_config.nodetable_entry_slab_capacity = config->nodetable_entry_slab_capacity;
    g_mtpndd_pal_config.edge_map_slab_capacity = config->edge_map_slab_capacity;
    g_mtpndd_pal_config.gc_protect_entry_slab_capacity = config->gc_protect_entry_slab_capacity;
    mtpndd_apply_pal_config_defaults();

    MTPNDD_TRUE.field_id = 0;
    MTPNDD_TRUE.edges = NULL;
    mtpndd_protect(&MTPNDD_TRUE);
    MTPNDD_FALSE.field_id = 0;
    MTPNDD_FALSE.edges = NULL;
    mtpndd_protect(&MTPNDD_FALSE);

    memset(&g_mtpndd_config, 0, sizeof(g_mtpndd_config));
    g_mtpndd_config.field_count = 0;
    g_mtpndd_config.field_capacity = DEFAULT_FIELD_CAPACITY;
    g_mtpndd_config.field_info = (mtpndd_field_info_t **)calloc(
            DEFAULT_FIELD_CAPACITY, sizeof(mtpndd_field_info_t *));
    if (!g_mtpndd_config.field_info) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.field_info[0] = &MTPNDD_TERMINAL_FIELD; // Reserve field 0 for terminal nodes
    // aligned allocation for hot node_tables_by_field to reduce cache-line crossings
    void *aligned_nt = NULL;
    if (posix_memalign(&aligned_nt, 64, sizeof(mtpndd_nodetable_t *) * DEFAULT_FIELD_CAPACITY) != 0) {
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.node_tables_by_field = (mtpndd_nodetable_t **)aligned_nt;
    memset(g_mtpndd_config.node_tables_by_field, 0, sizeof(mtpndd_nodetable_t *) * DEFAULT_FIELD_CAPACITY);

    g_mtpndd_config.gcProtect = (mtpndd_gc_protect_t *)malloc(sizeof(mtpndd_gc_protect_t));
    if (!g_mtpndd_config.gcProtect) {
        free(g_mtpndd_config.node_tables_by_field);
        g_mtpndd_config.node_tables_by_field = NULL;
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    atomic_init(&g_mtpndd_config.gcProtect->gc_protect_count, 0);
    g_mtpndd_config.gcProtect->bucket_count = g_mtpndd_pal_config.gc_bucket_count;
    g_mtpndd_config.gcProtect->used_bucket_count = 0;
    g_mtpndd_config.gcProtect->used_bucket_capacity = 16;
    g_mtpndd_config.gcProtect->used_bucket_indices = (size_t *)malloc(
            g_mtpndd_config.gcProtect->used_bucket_capacity * sizeof(size_t));
    if (!g_mtpndd_config.gcProtect->used_bucket_indices) {
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
        free(g_mtpndd_config.node_tables_by_field);
        g_mtpndd_config.node_tables_by_field = NULL;
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    size_t gc_bucket_cnt = g_mtpndd_config.gcProtect->bucket_count;
    if (gc_bucket_cnt == 0) {
        gc_bucket_cnt = MTPNDD_DEFAULT_GC_BUCKET_COUNT;
        g_mtpndd_config.gcProtect->bucket_count = gc_bucket_cnt;
    }
    g_mtpndd_config.gcProtect->buckets = (gc_protect_entry_t **)calloc(
            gc_bucket_cnt, sizeof(gc_protect_entry_t *));
    if (!g_mtpndd_config.gcProtect->buckets) {
        free(g_mtpndd_config.gcProtect->used_bucket_indices);
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
        free(g_mtpndd_config.node_tables_by_field);
        g_mtpndd_config.node_tables_by_field = NULL;
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }

    if (!mtpndd_lace_init()) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_PARALLEL_INIT);
    }
    if (!mtpndd_op_cache_initialize(
                config->op_cache_size,
                &g_mtpndd_config.and_cache,
                &g_mtpndd_config.or_cache,
                &g_mtpndd_config.not_cache)) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_INITIALIZE_FAILED);
    }

    mtpndd_memory_pools_init();

    // Attach GC logging hooks for both Sylvan and MTPNDD
    sylvan_gc_hook_pregc(mtpndd_gc_hook_sylvan_pre);
    sylvan_gc_hook_postgc(mtpndd_gc_hook_sylvan_post);
    mtpndd_gc_hook_pregc(mtpndd_gc_hook_mtpndd_pre);
    mtpndd_gc_hook_postgc(mtpndd_gc_hook_mtpndd_post);

#ifdef ENABLE_RECORDING
    memset(&g_mtpndd_stats, 0, sizeof(g_mtpndd_stats));
#endif
    
    return MTPNDD_SUCCESS;
}

static bool mtpndd_lace_init(void) {
    lace_start(g_mtpndd_pal_config.n_workers, g_mtpndd_pal_config.lace_dqsize);

    sylvan_set_sizes(
            g_mtpndd_pal_config.bdd_nodetable_size,
            g_mtpndd_pal_config.bdd_nodetable_size,
            g_mtpndd_pal_config.op_cache_size,
            g_mtpndd_pal_config.op_cache_size);
    sylvan_init_package();
    sylvan_init_bdd();

    // check if sylvan is initialized
    return true;
}

static void mtpndd_gc_hook_sylvan_pre(WorkerP *worker, Task *task) {
    (void)worker;
    (void)task;
    size_t refs = sylvan_count_refs();
    fprintf(stdout, "[Sylvan GC] start refs=%zu capacity=%zu\n",
            refs, g_mtpndd_pal_config.bdd_nodetable_size);
    fflush(stdout);
}

static void mtpndd_gc_hook_sylvan_post(WorkerP *worker, Task *task) {
    (void)worker;
    (void)task;
    size_t refs = sylvan_count_refs();
    fprintf(stdout, "[Sylvan GC] end refs=%zu\n", refs);
    fflush(stdout);
}

static void mtpndd_gc_hook_mtpndd_pre(void) {
    size_t nodes = __atomic_load_n(&g_mtpndd_stats.node_count, __ATOMIC_RELAXED);
    size_t capacity = g_mtpndd_pal_config.mtpndd_nodetable_size;
    fprintf(stdout, "[MTPNDD GC] start nodes=%zu capacity=%zu\n", nodes, capacity);
    fflush(stdout);
}

static void mtpndd_gc_hook_mtpndd_post(void) {
    size_t nodes = __atomic_load_n(&g_mtpndd_stats.node_count, __ATOMIC_RELAXED);
#ifdef ENABLE_RECORDING
    unsigned long long reclaimed = __atomic_load_n(&g_mtpndd_stats.nodes_collected_last, __ATOMIC_RELAXED);
#else
    unsigned long long reclaimed = 0;
#endif
    fprintf(stdout, "[MTPNDD GC] end nodes=%zu reclaimed=%llu\n", nodes, reclaimed);
    fflush(stdout);
}

mtpndd_error_t mtpndd_quit() {
    if (!mtpndd_is_initialized()) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
    }

    // free every fields and their node tables
    for (uint32_t i = 1; i <= g_mtpndd_config.field_count; i++) {
        mtpndd_field_info_t *field = g_mtpndd_config.field_info[i];
        if (field) {
            mtpndd_field_info_teardown(field);
            free(field);
            g_mtpndd_config.field_info[i] = NULL;
        }
        if (g_mtpndd_config.node_tables_by_field[i]) {
            free(g_mtpndd_config.node_tables_by_field[i]);
            g_mtpndd_config.node_tables_by_field[i] = NULL;
        }
    }
    free(g_mtpndd_config.field_info);
    g_mtpndd_config.field_info = NULL;
    free(g_mtpndd_config.node_tables_by_field);
    g_mtpndd_config.node_tables_by_field = NULL;
    g_mtpndd_config.field_count = 0;
    g_mtpndd_config.field_capacity = 0;

    if (g_mtpndd_config.gcProtect) {
        mtpndd_gc_protect_clear();
        free(g_mtpndd_config.gcProtect->buckets);
        g_mtpndd_config.gcProtect->buckets = NULL;
        free(g_mtpndd_config.gcProtect->used_bucket_indices);
        g_mtpndd_config.gcProtect->used_bucket_indices = NULL;
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
    }

    mtpndd_op_cache_destroy();
    mtpndd_memory_pools_shutdown();

    lace_stop();
    sylvan_quit();

    memset(&g_mtpndd_pal_config, 0, sizeof(g_mtpndd_pal_config));
    memset(&g_mtpndd_stats, 0, sizeof(g_mtpndd_stats));

    mtpndd_clear_error();

    return MTPNDD_SUCCESS;
}

/********************************
 * GC protection hash set
 ********************************/
static bool gc_protect_record_used_bucket(mtpndd_gc_protect_t *gc_protect, size_t bucket_idx) {
    if (!gc_protect || !gc_protect->used_bucket_indices) {
        return false;
    }
    // only record when bucket becomes non-empty
    if (gc_protect->used_bucket_count >= gc_protect->used_bucket_capacity) {
        size_t new_cap = gc_protect->used_bucket_capacity * 2;
        size_t *new_arr = (size_t *)realloc(gc_protect->used_bucket_indices, new_cap * sizeof(size_t));
        if (!new_arr) {
            return false;
        }
        gc_protect->used_bucket_indices = new_arr;
        gc_protect->used_bucket_capacity = new_cap;
    }
    gc_protect->used_bucket_indices[gc_protect->used_bucket_count++] = bucket_idx;
    return true;
}

static void gc_protect_clear_all(mtpndd_gc_protect_t *gc_protect) {
    if (!gc_protect || atomic_load_explicit(&gc_protect->gc_protect_count, memory_order_relaxed) == 0) {
        return;
    }
    size_t used_cnt = gc_protect->used_bucket_count;
    for (size_t i = 0; i < used_cnt; ++i) {
        size_t bucket_idx = gc_protect->used_bucket_indices[i];
        gc_protect_entry_t *entry = gc_protect->buckets ? gc_protect->buckets[bucket_idx] : NULL;
        while (entry) {
            gc_protect_entry_t *next_entry = entry->next;
            mtpndd_memory_release_gc_protect_entry(entry);
            entry = next_entry;
        }
        if (gc_protect->buckets) {
            gc_protect->buckets[bucket_idx] = NULL;
        }
    }
    gc_protect->used_bucket_count = 0;
    atomic_store_explicit(&gc_protect->gc_protect_count, 0, memory_order_relaxed);
}

void mtpndd_gc_protect_clear() {
    if (!g_mtpndd_config.gcProtect) {
        return;
    }
    gc_protect_clear_all(g_mtpndd_config.gcProtect);
}

void mtpndd_gc_protect_add(mtpndd_t *node) {
    if (!mtpndd_is_initialized() || !node || node == &MTPNDD_TRUE || node == &MTPNDD_FALSE) {
        return;
    }

    mtpndd_gc_protect_t *gc_protect = g_mtpndd_config.gcProtect;
#ifdef ENABLE_RECORDING
    struct timespec phase_start = {0};
    struct timespec phase_end = {0};
    clock_gettime(CLOCK_MONOTONIC, &phase_start);
#endif
    size_t hash = GC_PROTECT_HASH_VAL(gc_protect, node);
#ifdef ENABLE_RECORDING
    clock_gettime(CLOCK_MONOTONIC, &phase_end);
    MTPNDD_STAT_ADD(gc_protect_hash_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
    clock_gettime(CLOCK_MONOTONIC, &phase_start);
#endif

    // Check if already protected before allocating
    gc_protect_entry_t *existing = gc_protect_bucket_find(gc_protect, hash, node);
    if (existing) {
#ifdef ENABLE_RECORDING
        clock_gettime(CLOCK_MONOTONIC, &phase_end);
        MTPNDD_STAT_ADD(gc_protect_lookup_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
#endif
        return;
    }
#ifdef ENABLE_RECORDING
    clock_gettime(CLOCK_MONOTONIC, &phase_end);
    MTPNDD_STAT_ADD(gc_protect_lookup_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
    clock_gettime(CLOCK_MONOTONIC, &phase_start);
#endif

    // Only allocate after confirming not already protected
    gc_protect_entry_t *entry = mtpndd_memory_acquire_gc_protect_entry();
    if (!entry) {
#ifdef ENABLE_RECORDING
        clock_gettime(CLOCK_MONOTONIC, &phase_end);
        MTPNDD_STAT_ADD(gc_protect_alloc_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
#endif
        return;
    }
#ifdef ENABLE_RECORDING
    clock_gettime(CLOCK_MONOTONIC, &phase_end);
    MTPNDD_STAT_ADD(gc_protect_alloc_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
    clock_gettime(CLOCK_MONOTONIC, &phase_start);
#endif

    if (!gc_protect->buckets[hash]) {
        // first entry in this bucket, record for fast clear
        if (!gc_protect_record_used_bucket(gc_protect, hash)) {
#ifdef ENABLE_RECORDING
            clock_gettime(CLOCK_MONOTONIC, &phase_end);
            MTPNDD_STAT_ADD(gc_protect_record_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
#endif
            mtpndd_memory_release_gc_protect_entry(entry);
            return;
        }
    }
#ifdef ENABLE_RECORDING
    clock_gettime(CLOCK_MONOTONIC, &phase_end);
    MTPNDD_STAT_ADD(gc_protect_record_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
    clock_gettime(CLOCK_MONOTONIC, &phase_start);
#endif

    entry->node = node;
    entry->next = gc_protect->buckets[hash];
    entry->prev = NULL;
    if (gc_protect->buckets[hash]) {
        gc_protect->buckets[hash]->prev = entry;
    }
    gc_protect->buckets[hash] = entry;
    atomic_fetch_add_explicit(&gc_protect->gc_protect_count, 1, memory_order_relaxed);
#ifdef ENABLE_RECORDING
    clock_gettime(CLOCK_MONOTONIC, &phase_end);
    MTPNDD_STAT_ADD(gc_protect_link_ns, mtpndd_timespec_diff_ns(&phase_start, &phase_end));
#endif
}
