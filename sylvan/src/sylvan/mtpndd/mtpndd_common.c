// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_common.h"

#include <lace.h>
#include "sylvan.h"
#include "sylvan_table.h"
#include <pthread.h>

/********************************
 * Default configurations
 ********************************/
#define DEFAULT_QUICK_GROWTH_THRESHOLD 0.1
#define DEFAULT_FIELD_CAPACITY 8

/********************************
 * Error handling system
 ********************************/
static __thread mtpndd_error_info_t g_last_error = {MTPNDD_SUCCESS, NULL, NULL, 0};

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
 * MTPNDD field management
 ********************************/
mtpndd_field_info_t* mtpndd_get_field_info(uint32_t field_id) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(field_id <= g_mtpndd_config.field_count, MTPNDD_ERROR_INVALID_FIELD);

    return g_mtpndd_config.field_info[field_id];
}

// only append field
mtpndd_error_t mtpndd_declare_field(uint32_t bit_width) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(bit_width > 0, MTPNDD_ERROR_INVALID_PARAM);

    // check and expand capacity
    if (g_mtpndd_config.field_count + 1 >= g_mtpndd_config.field_capacity) {
        uint32_t new_capacity = g_mtpndd_config.field_capacity * 2;

        mtpndd_field_info_t **new_field_info = (mtpndd_field_info_t **)realloc(
                g_mtpndd_config.field_info, sizeof(mtpndd_field_info_t*) * new_capacity);
        if (!new_field_info) {
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        for (uint32_t i = 0; i < g_mtpndd_config.field_capacity; i++) {
            new_field_info[i] = g_mtpndd_config.field_info[i];
        }
        free(g_mtpndd_config.field_info);
        g_mtpndd_config.field_info = new_field_info;

        mtpndd_nodetable_t **new_node_tables = (mtpndd_nodetable_t **)realloc(
                g_mtpndd_config.node_tables_by_field, sizeof(mtpndd_nodetable_t*) * new_capacity);
        if (!new_node_tables) {
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        for (uint32_t i = 0; i < g_mtpndd_config.field_capacity; i++) {
            new_node_tables[i] = g_mtpndd_config.node_tables_by_field[i];
        }
        free(g_mtpndd_config.node_tables_by_field);
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
        free(new_field->bdd_vars); \
        free(new_field->bdd_not_vars); \
        free(new_field); \
    } while(0)
#define FREE_MTPNDD_VARS() do { \
        free(new_field->mtpndd_vars); \
        free(new_field->mtpndd_not_vars); \
        FREE_BDD_VARS(); \
    } while(0)
#define FREE_MTPNDD_VARS_WITH_NODES(i) do { \
        for (uint32_t j = 0; j < (i); j++) { \
            if (new_field->mtpndd_vars[j]) free(new_field->mtpndd_vars[j]); \
            if (new_field->mtpndd_not_vars[j]) free(new_field->mtpndd_not_vars[j]); \
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
    new_field->mtpndd_vars = (mtpndd_node_t *)malloc(sizeof(mtpndd_node_t) * bit_width);
    new_field->mtpndd_not_vars = (mtpndd_node_t *)malloc(sizeof(mtpndd_node_t) * bit_width);
    if (!new_field->mtpndd_vars || !new_field->mtpndd_not_vars) {
        FREE_MTPNDD_VARS();
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    for (uint32_t i = 0; i < bit_width; i++) {
        new_field->mtpndd_vars[i] = (mtpndd_node_t *)malloc(sizeof(mtpndd_node_t));
        new_field->mtpndd_not_vars[i] = (mtpndd_node_t *)malloc(sizeof(mtpndd_node_t));
        if (!new_field->mtpndd_vars[i] || !new_field->mtpndd_not_vars[i]) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        memset(new_field->mtpndd_vars[i], 0, sizeof(mtpndd_node_t));
        memset(new_field->mtpndd_not_vars[i], 0, sizeof(mtpndd_node_t));

        new_field->mtpndd_vars[i]->field = new_field;
        EDGE_MAP_INIT(new_field->mtpndd_vars[i]->edges);
        new_field->mtpndd_vars[i]->ref_count = 0;

        new_field->mtpndd_not_vars[i]->field = new_field;
        EDGE_MAP_INIT(new_field->mtpndd_not_vars[i]->edges);
        new_field->mtpndd_not_vars[i]->ref_count = 0;

        // Initialize the variable node to point to TRUE terminal node
        mtpndd_error_t err;
        err = mtpndd_add_edge(new_field->mtpndd_vars[i], &MTPNDD_TRUE, new_field->bdd_vars[i]);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDD_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        err = mtpndd_protect(new_field->mtpndd_vars[i]);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDV_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }

        err = mtpndd_add_edge(new_field->mtpndd_not_vars[i], &MTPNDD_TRUE, new_field->bdd_not_vars[i]);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDV_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
        err = mtpndd_protect(new_field->mtpndd_not_vars[i]);
        if (err != MTPNDD_SUCCESS) {
            FREE_MTPNDV_VARS_WITH_NODES(bit_width);
            MTPNDD_RETURN_ERROR(err);
        }
    }

    g_mtpndd_config.field_info[g_mtpndd_config.field_count] = new_field;
    g_mtpndd_config.node_tables_by_field[g_mtpndd_config.field_count] = mtpndd_nodetable_declare_field();
}

/********************************
 * MTPNDD get node
 ********************************/
mtpndd_t *mtpndd_get_var(uint32_t field, uint32_t var_index) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(field > 0 && field <= g_mtpndd_config.field_count, NULL);
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    MTPNDD_CHECK_PARAM(var_index < field_info->bit_width, NULL);

    return field_info->mtpndd_vars[var_index];
}

mtpndd_t *mtpndd_get_not_var(uint32_t field, uint32_t var_index) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(field > 0 && field <= g_mtpndd_config.field_count, NULL);
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    MTPNDD_CHECK_PARAM(var_index < field_info->bit_width, NULL);

    return field_info->mtpndd_not_vars[var_index];
}

mtpndd_bdd_t mtpndd_get_bdd_var(uint32_t field, uint32_t var_index) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(field > 0 && field <= g_mtpndd_config.field_count, 0);
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    MTPNDD_CHECK_PARAM(var_index < field_info->bit_width, 0);

    return field_info->bdd_vars[var_index];
}

mtpndd_bdd_t mtpndd_get_bdd_not_var(uint32_t field, uint32_t var_index) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(field > 0 && field <= g_mtpndd_config.field_count, 0);
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    MTPNDD_CHECK_PARAM(var_index < field_info->bit_width, 0);

    return field_info->bdd_not_vars[var_index];
}

/********************************
 * Global status control
 ********************************/
mtpndd_stats_t *mtpndd_get_stats() {
    return &g_mtpndd_stats;
}

bool mtpndd_is_initialized() {
    return g_mtpndd_pal_config.n_workers > 0
            || g_mtpndd_pal_config.lace_dqsize > 0
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
            config->n_workers > 0
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

    memset(&g_mtpndd_config, 0, sizeof(g_mtpndd_config));
    g_mtpndd_config.field_count = 0;
    g_mtpndd_config.field_capacity = DEFAULT_FIELD_CAPACITY;
    g_mtpndd_config.field_info = (mtpndd_field_info_t **)malloc(sizeof(mtpndd_field_info_t*) * DEFAULT_FIELD_CAPACITY);
    g_mtpndd_config.field_info[0] = &MTPNDD_TERMINAL_FIELD; // Reserve field 0 for terminal nodes
    g_mtpndd_config.node_tables_by_field = (mtpndd_nodetable_t **)malloc(sizeof(mtpndd_nodetable_t*) * DEFAULT_FIELD_CAPACITY);
    
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

#ifdef ENABLE_RECORDING
    // Before and after garbage collection, call gc_start and gc_end
    sylvan_gc_hook_pregc(TASK(sylvan_gc_start));
    sylvan_gc_hook_postgc(TASK(sylvan_gc_end));
    memset(&g_mtpndd_stats, 0, sizeof(g_mtpndd_stats));
#endif
    
    return MTPNDD_SUCCESS;
}

bool mtpndd_lace_init() {
    lace_start(g_mtpndd_pal_config.n_workers, g_mtpndd_pal_config.lace_dqsize);

    sylvan_set_sizes(
            g_mtpndd_pal_config.bdd_nodetable_size,
            g_mtpndd_pal_config.bdd_nodetable_size,
            g_mtpndd_pal_config.op_cache_size,
            g_mtpndd_pal_config.op_cache_size);
    sylvan_init_package();
    sylvan_init_bdd();

    // check if sylvan is initialized
    if (!sylvan_is_initialized()) {
        return false;
    }

    return true;
}

bool sylvan_is_initialized() {
    // Check if sylvan is initialized by checking if nodes table exists
    // and table_max is set (which happens in sylvan_set_sizes)
    extern llmsset_t nodes;
    extern size_t table_max;
    
    return (nodes != NULL && table_max > 0);
}

#define SYLVAN_INFO(s, ...) fprintf(stdout, "[% 8.2f] " s, ##__VA_ARGS__)

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
        free(g_mtpndd_config.field_info[i]);
        g_mtpndd_config.field_info[i] = NULL;
        free(g_mtpndd_config.node_tables_by_field[i]);
        g_mtpndd_config.node_tables_by_field[i] = NULL;
    }
    free(g_mtpndd_config.field_info);
    g_mtpndd_config.field_info = NULL;
    g_mtpndd_config.field_count = 0;
    g_mtpndd_config.field_capacity = 0;

    mtpndd_op_cache_destroy();

    lace_stop();
    sylvan_quit();

    memset(&g_mtpndd_pal_config, 0, sizeof(g_mtpndd_pal_config));
    memset(&g_mtpndd_stats, 0, sizeof(g_mtpndd_stats));

    mtpndd_clear_error();

    return MTPNDD_SUCCESS;
}