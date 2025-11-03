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
#include <pthread.h>
#include <stdatomic.h>

/********************************
 * Global singletons
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

/********************************
 * Internal helpers
 ********************************/
static bool mtpndd_lace_init(void);
static mtpndd_error_t mtpndd_edge_map_init(mtpndd_edge_t *edges);
static bool mtpndd_gc_protect_contains_with_hash(mtpndd_t *node, size_t hash);
static void mtpndd_gc_run_hooks(mtpndd_gc_hook_t *hooks, size_t count);

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

/********************************
 * Default configurations
 ********************************/
#define DEFAULT_QUICK_GROWTH_THRESHOLD 0.1
#define DEFAULT_FIELD_CAPACITY 8

static mtpndd_error_t mtpndd_edge_map_init(mtpndd_edge_t *edges)
{
    EDGE_MAP_INIT(edges);
    return MTPNDD_SUCCESS;
}

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

    // check and expand capacity
    if (g_mtpndd_config.field_count + 1 >= g_mtpndd_config.field_capacity) {
        uint32_t new_capacity = g_mtpndd_config.field_capacity * 2;

        mtpndd_field_info_t **new_field_info = (mtpndd_field_info_t **)calloc(
                new_capacity, sizeof(mtpndd_field_info_t*));
        if (!new_field_info) {
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }

        mtpndd_nodetable_t **new_node_tables = (mtpndd_nodetable_t **)calloc(
                new_capacity, sizeof(mtpndd_nodetable_t*));
        if (!new_node_tables) {
            free(new_field_info);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }

        for (uint32_t i = 0; i <= g_mtpndd_config.field_count; i++) {
            new_field_info[i] = g_mtpndd_config.field_info[i];
            new_node_tables[i] = g_mtpndd_config.node_tables_by_field[i];
        }

        free(g_mtpndd_config.field_info);
        free(g_mtpndd_config.node_tables_by_field);
        g_mtpndd_config.field_info = new_field_info;
        g_mtpndd_config.node_tables_by_field = new_node_tables;
        g_mtpndd_config.field_capacity = new_capacity;
    }

    g_mtpndd_config.field_count++;

    mtpndd_field_info_t *new_field = (mtpndd_field_info_t *)malloc(sizeof(mtpndd_field_info_t));
    if (!new_field) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    memset(new_field, 0, sizeof(mtpndd_field_info_t));
    new_field->field_id = g_mtpndd_config.field_count;
    new_field->bit_width = bit_width;
    if (g_mtpndd_config.field_count == 1) {
        new_field->start_var = 0;
    } else {
        mtpndd_field_info_t *prev_field = g_mtpndd_config.field_info[g_mtpndd_config.field_count - 1];
        new_field->start_var = prev_field->start_var + prev_field->bit_width;
    }

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
                if (new_field->mtpndd_vars[j]->edges) { \
                    mtpndd_edge_map_free(new_field->mtpndd_vars[j]->edges); \
                    new_field->mtpndd_vars[j]->edges = NULL; \
                } \
                mtpndd_memory_release_node(new_field->mtpndd_vars[j]); \
                new_field->mtpndd_vars[j] = NULL; \
            } \
            if (new_field->mtpndd_not_vars && new_field->mtpndd_not_vars[j]) { \
                if (new_field->mtpndd_not_vars[j]->edges) { \
                    mtpndd_edge_map_free(new_field->mtpndd_not_vars[j]->edges); \
                    new_field->mtpndd_not_vars[j]->edges = NULL; \
                } \
                mtpndd_memory_release_node(new_field->mtpndd_not_vars[j]); \
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
        new_field->mtpndd_vars[i] = mtpndd_memory_acquire_node();
        new_field->mtpndd_not_vars[i] = mtpndd_memory_acquire_node();
        if (!new_field->mtpndd_vars[i] || !new_field->mtpndd_not_vars[i]) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        memset(new_field->mtpndd_vars[i], 0, sizeof(mtpndd_node_t));
        memset(new_field->mtpndd_not_vars[i], 0, sizeof(mtpndd_node_t));

        new_field->mtpndd_vars[i]->field = new_field;
        new_field->mtpndd_vars[i]->edges = mtpndd_memory_acquire_edge_map();
        if (!new_field->mtpndd_vars[i]->edges) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        memset(new_field->mtpndd_vars[i]->edges, 0, sizeof(mtpndd_edge_t));
        err = mtpndd_edge_map_init(new_field->mtpndd_vars[i]->edges);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        atomic_init(&new_field->mtpndd_vars[i]->ref_count, 0);

        new_field->mtpndd_not_vars[i]->field = new_field;
        new_field->mtpndd_not_vars[i]->edges = mtpndd_memory_acquire_edge_map();
        if (!new_field->mtpndd_not_vars[i]->edges) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        memset(new_field->mtpndd_not_vars[i]->edges, 0, sizeof(mtpndd_edge_t));
        err = mtpndd_edge_map_init(new_field->mtpndd_not_vars[i]->edges);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        atomic_init(&new_field->mtpndd_not_vars[i]->ref_count, 0);

        // Initialize the variable node to point to TRUE terminal node
        err = mtpndd_add_edge(new_field->mtpndd_vars[i]->edges, &MTPNDD_TRUE, sylvan_ref(new_field->bdd_vars[i]));
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        err = mtpndd_protect(new_field->mtpndd_vars[i]);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }

        err = mtpndd_add_edge(new_field->mtpndd_not_vars[i]->edges, &MTPNDD_TRUE, sylvan_ref(new_field->bdd_not_vars[i]));
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        err = mtpndd_protect(new_field->mtpndd_not_vars[i]);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
    }

    g_mtpndd_config.field_info[g_mtpndd_config.field_count] = new_field;
    mtpndd_nodetable_t *nodetable = mtpndd_nodetable_declare_field();
    if (!nodetable) {
        FREE_MTPNDD_VARS_WITH_NODES(bit_width);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.node_tables_by_field[g_mtpndd_config.field_count] = nodetable;

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

    MTPNDD_TRUE.field = &MTPNDD_TERMINAL_FIELD;
    MTPNDD_TRUE.edges = NULL;
    mtpndd_protect(&MTPNDD_TRUE);
    MTPNDD_FALSE.field = &MTPNDD_TERMINAL_FIELD;
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

    g_mtpndd_config.node_tables_by_field = (mtpndd_nodetable_t **)calloc(
            DEFAULT_FIELD_CAPACITY, sizeof(mtpndd_nodetable_t *));
    if (!g_mtpndd_config.node_tables_by_field) {
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }

    g_mtpndd_config.gcProtect = (mtpndd_gc_protect_t *)malloc(sizeof(mtpndd_gc_protect_t));
    if (!g_mtpndd_config.gcProtect) {
        free(g_mtpndd_config.node_tables_by_field);
        g_mtpndd_config.node_tables_by_field = NULL;
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    atomic_init(&g_mtpndd_config.gcProtect->gc_protect_count, 0);
    g_mtpndd_config.gcProtect->bucket_count = mtpndd_config_gc_bucket_count();
    size_t gc_bucket_cnt = g_mtpndd_config.gcProtect->bucket_count;
    if (gc_bucket_cnt == 0) {
        gc_bucket_cnt = MTPNDD_DEFAULT_GC_BUCKET_COUNT;
        g_mtpndd_config.gcProtect->bucket_count = gc_bucket_cnt;
    }
    g_mtpndd_config.gcProtect->buckets = (gc_protect_entry_t **)calloc(
            gc_bucket_cnt, sizeof(gc_protect_entry_t *));
    if (!g_mtpndd_config.gcProtect->buckets) {
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
        free(g_mtpndd_config.node_tables_by_field);
        g_mtpndd_config.node_tables_by_field = NULL;
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.gcProtect->bucket_locks = (pthread_rwlock_t *)malloc(
            gc_bucket_cnt * sizeof(pthread_rwlock_t));
    if (!g_mtpndd_config.gcProtect->bucket_locks) {
        free(g_mtpndd_config.gcProtect->buckets);
        g_mtpndd_config.gcProtect->buckets = NULL;
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
        free(g_mtpndd_config.node_tables_by_field);
        g_mtpndd_config.node_tables_by_field = NULL;
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    for (size_t i = 0; i < gc_bucket_cnt; i++) {
        if (pthread_rwlock_init(&g_mtpndd_config.gcProtect->bucket_locks[i], NULL) != 0) {
            for (size_t j = 0; j < i; j++) {
                pthread_rwlock_destroy(&g_mtpndd_config.gcProtect->bucket_locks[j]);
            }
            free(g_mtpndd_config.gcProtect->bucket_locks);
            g_mtpndd_config.gcProtect->bucket_locks = NULL;
            free(g_mtpndd_config.gcProtect->buckets);
            g_mtpndd_config.gcProtect->buckets = NULL;
            free(g_mtpndd_config.gcProtect);
            g_mtpndd_config.gcProtect = NULL;
            free(g_mtpndd_config.node_tables_by_field);
            g_mtpndd_config.node_tables_by_field = NULL;
            free(g_mtpndd_config.field_info);
            g_mtpndd_config.field_info = NULL;
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_THREAD_SAFETY);
        }
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

    mtpndd_error_t pool_status = mtpndd_memory_pools_init();
    if (pool_status != MTPNDD_SUCCESS) {
        mtpndd_op_cache_destroy();
        MTPNDD_RETURN_ERROR(pool_status);
    }

#ifdef ENABLE_RECORDING
    // Before and after garbage collection, call gc_start and gc_end
    sylvan_gc_hook_pregc(TASK(sylvan_gc_start));
    sylvan_gc_hook_postgc(TASK(sylvan_gc_end));
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

#define SYLVAN_INFO(s, ...) fprintf(stdout, "[% 8.2f] " s, 0.0, ##__VA_ARGS__)

VOID_TASK_0(sylvan_gc_start)
{
    SYLVAN_INFO("(GC-Sylvan) Starting garbage collection...\n");
}

VOID_TASK_0(sylvan_gc_end)
{
    SYLVAN_INFO("(GC-Sylvan) Garbage collection done.\n");
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

    mtpndd_op_cache_destroy();
    mtpndd_memory_pools_shutdown();

    if (g_mtpndd_config.gcProtect) {
        GC_PROTECT_CLEAR(g_mtpndd_config.gcProtect);
        size_t gc_bucket_cnt = g_mtpndd_config.gcProtect->bucket_count ? g_mtpndd_config.gcProtect->bucket_count : mtpndd_config_gc_bucket_count();
        if (gc_bucket_cnt == 0) {
            gc_bucket_cnt = MTPNDD_DEFAULT_GC_BUCKET_COUNT;
        }
        if (g_mtpndd_config.gcProtect->bucket_locks) {
            for (size_t i = 0; i < gc_bucket_cnt; i++) {
                pthread_rwlock_destroy(&g_mtpndd_config.gcProtect->bucket_locks[i]);
            }
            free(g_mtpndd_config.gcProtect->bucket_locks);
            g_mtpndd_config.gcProtect->bucket_locks = NULL;
        }
        free(g_mtpndd_config.gcProtect->buckets);
        g_mtpndd_config.gcProtect->buckets = NULL;
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
    }

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
void mtpndd_gc_protect_clear() {
    if (!g_mtpndd_config.gcProtect) {
        return;
    }
    GC_PROTECT_CLEAR(g_mtpndd_config.gcProtect);
}

mtpndd_error_t mtpndd_gc_protect_add(mtpndd_t *node) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(node != NULL, MTPNDD_ERROR_NULL_POINTER);
    if (node == &MTPNDD_TRUE || node == &MTPNDD_FALSE) {
        return MTPNDD_SUCCESS;
    }

    mtpndd_gc_protect_t *gc_protect = g_mtpndd_config.gcProtect;
    gc_protect_entry_t *entry = (gc_protect_entry_t *)malloc(sizeof(gc_protect_entry_t));
    if (!entry) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    if (!gc_protect || !gc_protect->buckets || !gc_protect->bucket_locks) {
        free(entry);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_THREAD_SAFETY);
    }
    size_t hash = GC_PROTECT_HASH_VAL(gc_protect, node);

    if (GC_PROTECT_BUCKET_RDLOCK(gc_protect, hash) != 0) {
        free(entry);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_THREAD_SAFETY);
    }
    gc_protect_entry_t *existing = gc_protect_bucket_find(gc_protect, hash, node);
    if (existing) {
        GC_PROTECT_BUCKET_UNLOCK(gc_protect, hash);
        free(entry);
        return MTPNDD_SUCCESS;
    }
    GC_PROTECT_BUCKET_UNLOCK(gc_protect, hash);

    if (GC_PROTECT_BUCKET_WRLOCK(gc_protect, hash) != 0) {
        free(entry);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_THREAD_SAFETY);
    }
    existing = gc_protect_bucket_find(gc_protect, hash, node);
    if (existing) {
        GC_PROTECT_BUCKET_UNLOCK(gc_protect, hash);
        free(entry);
        return MTPNDD_SUCCESS;
    }

    entry->node = node;
    entry->next = gc_protect->buckets[hash];
    entry->prev = NULL;
    if (gc_protect->buckets[hash]) {
        gc_protect->buckets[hash]->prev = entry;
    }
    gc_protect->buckets[hash] = entry;
    atomic_fetch_add_explicit(&gc_protect->gc_protect_count, 1, memory_order_relaxed);
    GC_PROTECT_BUCKET_UNLOCK(gc_protect, hash);

    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_gc_protect_remove(mtpndd_t *node) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(node != NULL, MTPNDD_ERROR_NULL_POINTER);
    MTPNDD_CHECK_PARAM(node != &MTPNDD_TRUE && node != &MTPNDD_FALSE, MTPNDD_ERROR_INVALID_PARAM);

    mtpndd_gc_protect_t *gc_protect = g_mtpndd_config.gcProtect;
    if (!gc_protect || !gc_protect->buckets || !gc_protect->bucket_locks) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_THREAD_SAFETY);
    }
    size_t hash = GC_PROTECT_HASH_VAL(gc_protect, node);
    if (GC_PROTECT_BUCKET_WRLOCK(gc_protect, hash) != 0) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_THREAD_SAFETY);
    }
    gc_protect_entry_t *entry = gc_protect_bucket_find(gc_protect, hash, node);
    if (entry) {
        if (entry->prev) {
            entry->prev->next = entry->next;
        } else {
            gc_protect->buckets[hash] = entry->next;
        }
        if (entry->next) {
            entry->next->prev = entry->prev;
        }
        free(entry);
        atomic_fetch_sub_explicit(&gc_protect->gc_protect_count, 1, memory_order_relaxed);
        GC_PROTECT_BUCKET_UNLOCK(gc_protect, hash);
        return MTPNDD_SUCCESS;
    }
    GC_PROTECT_BUCKET_UNLOCK(gc_protect, hash);

    MTPNDD_RETURN_ERROR(MTPNDD_ERROR_NULL_POINTER);
}

bool mtpndd_gc_protect_contains(mtpndd_t *node) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(node != NULL, false);
    MTPNDD_CHECK_PARAM(node != &MTPNDD_TRUE && node != &MTPNDD_FALSE, false);

    mtpndd_gc_protect_t *gc_protect = g_mtpndd_config.gcProtect;
    if (!gc_protect || !gc_protect->buckets || !gc_protect->bucket_locks) {
        return false;
    }
    size_t hash = GC_PROTECT_HASH_VAL(gc_protect, node);
    return mtpndd_gc_protect_contains_with_hash(node, hash);
}

static bool mtpndd_gc_protect_contains_with_hash(mtpndd_t *node, size_t hash) {
    mtpndd_gc_protect_t *gc_protect = g_mtpndd_config.gcProtect;
    if (!gc_protect || !gc_protect->buckets || !gc_protect->bucket_locks) {
        return false;
    }
    size_t bucket_cnt = gc_protect->bucket_count ? gc_protect->bucket_count : mtpndd_config_gc_bucket_count();
    if (bucket_cnt == 0) {
        bucket_cnt = MTPNDD_DEFAULT_GC_BUCKET_COUNT;
    }
    if (bucket_cnt) {
        hash %= bucket_cnt;
    } else {
        hash = 0;
    }
    if (GC_PROTECT_BUCKET_RDLOCK(gc_protect, hash) != 0) {
        mtpndd_set_error(MTPNDD_ERROR_THREAD_SAFETY, __func__, __LINE__);
        return false;
    }
    bool found = gc_protect_bucket_find(gc_protect, hash, node) != NULL;
    GC_PROTECT_BUCKET_UNLOCK(gc_protect, hash);
    if (found) {
        return true;
    }

    return false;
}
