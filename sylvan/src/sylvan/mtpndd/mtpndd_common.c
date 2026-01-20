// Multi-Terminal Parallel Network Decision Diagrams (MTPNDD)
// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab

#include "mtpndd_common.h"

#include <stdlib.h>
#include <string.h>

#include "mtpndd_edge_builder.h"
#include "mtpndd_nodetable.h"
#include "mtpndd_operation_cache.h"

#include "lace.h"
#include "sylvan.h"
#include "sylvan_table.h"

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
    .and_cache = NULL,
    .or_cache = NULL,
    .not_cache = NULL,
};

static bool g_mtpndd_initialized = false;

/********************************
 * Error handling system
 ********************************/
static __thread mtpndd_error_info_t g_last_error = {MTPNDD_SUCCESS, NULL, NULL, 0};

const char *mtpndd_error_messages[] = {
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
    "Unknown error",
};

const char *mtpndd_error_string(mtpndd_error_t error) {
    size_t idx = (size_t)error;
    size_t total = sizeof(mtpndd_error_messages) / sizeof(mtpndd_error_messages[0]);
    if (idx < total) {
        return mtpndd_error_messages[idx];
    }
    return "Unknown error";
}

void mtpndd_set_error(mtpndd_error_t error, const char *function, int line) {
    g_last_error.code = error;
    g_last_error.message = mtpndd_error_string(error);
    g_last_error.function = function;
    g_last_error.line = line;
}

mtpndd_error_info_t mtpndd_get_last_error(void) {
    return g_last_error;
}

void mtpndd_clear_error(void) {
    g_last_error.code = MTPNDD_SUCCESS;
    g_last_error.message = NULL;
    g_last_error.function = NULL;
    g_last_error.line = 0;
}

/********************************
 * GC hooks
 ********************************/
#define MTPNDD_GC_HOOK_CAPACITY 16
static mtpndd_gc_hook_t g_mtpndd_gc_prehooks[MTPNDD_GC_HOOK_CAPACITY] = {0};
static mtpndd_gc_hook_t g_mtpndd_gc_posthooks[MTPNDD_GC_HOOK_CAPACITY] = {0};
static _Atomic size_t g_mtpndd_gc_prehook_count = 0;
static _Atomic size_t g_mtpndd_gc_posthook_count = 0;

void mtpndd_gc_hook_pregc(mtpndd_gc_hook_t hook) {
    if (!hook) return;
    size_t idx = atomic_fetch_add_explicit(&g_mtpndd_gc_prehook_count, 1, memory_order_relaxed);
    if (idx < MTPNDD_GC_HOOK_CAPACITY) {
        g_mtpndd_gc_prehooks[idx] = hook;
    }
}

void mtpndd_gc_hook_postgc(mtpndd_gc_hook_t hook) {
    if (!hook) return;
    size_t idx = atomic_fetch_add_explicit(&g_mtpndd_gc_posthook_count, 1, memory_order_relaxed);
    if (idx < MTPNDD_GC_HOOK_CAPACITY) {
        g_mtpndd_gc_posthooks[idx] = hook;
    }
}

static void mtpndd_gc_run_hooks(mtpndd_gc_hook_t *hooks, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (hooks[i]) {
            hooks[i]();
        }
    }
}

void mtpndd_gc_run_prehooks(void) {
    size_t count = atomic_load_explicit(&g_mtpndd_gc_prehook_count, memory_order_relaxed);
    if (count > MTPNDD_GC_HOOK_CAPACITY) count = MTPNDD_GC_HOOK_CAPACITY;
    mtpndd_gc_run_hooks(g_mtpndd_gc_prehooks, count);
}

void mtpndd_gc_run_posthooks(void) {
    size_t count = atomic_load_explicit(&g_mtpndd_gc_posthook_count, memory_order_relaxed);
    if (count > MTPNDD_GC_HOOK_CAPACITY) count = MTPNDD_GC_HOOK_CAPACITY;
    mtpndd_gc_run_hooks(g_mtpndd_gc_posthooks, count);
}

/********************************
 * Default sizing helpers
 ********************************/
#define DEFAULT_FIELD_CAPACITY 8
#define DEFAULT_QUICK_GROWTH_THRESHOLD 0.1

static void mtpndd_apply_pal_config_defaults(void) {
    if (g_mtpndd_pal_config.sylvan_granularity <= 0) {
        g_mtpndd_pal_config.sylvan_granularity = 1;
    }

    if (g_mtpndd_pal_config.nodetable_bucket_count == 0) {
        g_mtpndd_pal_config.nodetable_bucket_count = MTPNDD_DEFAULT_NODETABLE_BUCKET_COUNT;
    }
    if (g_mtpndd_pal_config.edge_entry_slab_capacity == 0) {
        g_mtpndd_pal_config.edge_entry_slab_capacity = MTPNDD_DEFAULT_EDGE_ENTRY_SLAB_CAPACITY;
    }
    if (g_mtpndd_pal_config.quick_growth_threshold <= 0.0) {
        g_mtpndd_pal_config.quick_growth_threshold = DEFAULT_QUICK_GROWTH_THRESHOLD;
    }
}

static bool mtpndd_lace_init(void) {
    lace_start(g_mtpndd_pal_config.n_workers, g_mtpndd_pal_config.lace_dqsize);

    if (g_mtpndd_pal_config.sylvan_memory_cap > 0) {
        sylvan_set_limits(
                g_mtpndd_pal_config.sylvan_memory_cap,
                g_mtpndd_pal_config.sylvan_table_ratio,
                g_mtpndd_pal_config.sylvan_initial_ratio);
    } else {
        sylvan_set_sizes(
                g_mtpndd_pal_config.bdd_nodetable_size,
                g_mtpndd_pal_config.bdd_nodetable_size,
                g_mtpndd_pal_config.op_cache_size,
                g_mtpndd_pal_config.op_cache_size);
    }
    sylvan_init_package();
    sylvan_init_bdd();
    sylvan_set_granularity(g_mtpndd_pal_config.sylvan_granularity);
    return true;
}

/********************************
 * Field management
 ********************************/
static void mtpndd_field_info_teardown(mtpndd_field_info_t *field) {
    if (!field) {
        return;
    }

    // Free MTPNDD variable arrays (owned by field)
    free(field->mtpndd_vars);
    free(field->mtpndd_not_vars);
    field->mtpndd_vars = NULL;
    field->mtpndd_not_vars = NULL;

    // IMPORTANT: Do NOT free bdd_vars/bdd_not_vars!
    // They point into shared arrays owned by g_mtpndd_config
    // Do NOT unprotect them either - shared arrays are protected/unprotected separately
    field->bdd_vars = NULL;
    field->bdd_not_vars = NULL;
}

mtpndd_error_t mtpndd_declare_field(uint32_t bit_width) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(bit_width > 0, MTPNDD_ERROR_INVALID_PARAM);

    // Check if fields already generated
    if (g_mtpndd_config.fields_generated) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_IS_INITIALIZED);
    }

    // Grow field_info array if needed
    if (g_mtpndd_config.pending_field_count + 1 >= g_mtpndd_config.field_capacity) {
        uint32_t new_capacity = g_mtpndd_config.field_capacity ? g_mtpndd_config.field_capacity * 2 : DEFAULT_FIELD_CAPACITY;
        mtpndd_field_info_t **new_field_info =
                (mtpndd_field_info_t **)realloc(g_mtpndd_config.field_info, sizeof(*new_field_info) * new_capacity);
        if (!new_field_info) {
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        memset(new_field_info + g_mtpndd_config.field_capacity, 0,
               sizeof(*new_field_info) * (new_capacity - g_mtpndd_config.field_capacity));
        g_mtpndd_config.field_info = new_field_info;
        g_mtpndd_config.field_capacity = new_capacity;
    }

    // Grow pending_field_bit_widths array if needed
    if (g_mtpndd_config.pending_field_count + 1 >= g_mtpndd_config.pending_field_capacity) {
        uint32_t new_capacity = g_mtpndd_config.pending_field_capacity ? g_mtpndd_config.pending_field_capacity * 2 : DEFAULT_FIELD_CAPACITY;
        uint32_t *new_bit_widths =
                (uint32_t *)realloc(g_mtpndd_config.pending_field_bit_widths, sizeof(uint32_t) * new_capacity);
        if (!new_bit_widths) {
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        g_mtpndd_config.pending_field_bit_widths = new_bit_widths;
        g_mtpndd_config.pending_field_capacity = new_capacity;
    }

    // Store bit_width for later batch generation
    g_mtpndd_config.pending_field_bit_widths[g_mtpndd_config.pending_field_count] = bit_width;
    g_mtpndd_config.pending_field_count++;

    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_generate_fields(void) {
    MTPNDD_CHECK_INIT();

    // Check if fields already generated
    if (g_mtpndd_config.fields_generated) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_IS_INITIALIZED);
    }

    // Check if there are any pending fields
    if (g_mtpndd_config.pending_field_count == 0) {
        g_mtpndd_config.fields_generated = true;
        return MTPNDD_SUCCESS;
    }

    // Find max_bit_width and calculate total BDD variables needed
    uint32_t max_bit_width = 0;
    uint32_t total_bdd_vars = 0;
    for (uint32_t i = 0; i < g_mtpndd_config.pending_field_count; ++i) {
        if (g_mtpndd_config.pending_field_bit_widths[i] > max_bit_width) {
            max_bit_width = g_mtpndd_config.pending_field_bit_widths[i];
        }
        total_bdd_vars += g_mtpndd_config.pending_field_bit_widths[i];
    }
    g_mtpndd_config.max_bit_width = max_bit_width;
    g_mtpndd_config.total_bdd_vars = total_bdd_vars;

    // Allocate shared BDD variable arrays for all fields
    g_mtpndd_config.shared_bdd_vars = (mtpndd_bdd_t *)calloc(total_bdd_vars, sizeof(mtpndd_bdd_t));
    g_mtpndd_config.shared_bdd_not_vars = (mtpndd_bdd_t *)calloc(total_bdd_vars, sizeof(mtpndd_bdd_t));
    if (!g_mtpndd_config.shared_bdd_vars || !g_mtpndd_config.shared_bdd_not_vars) {
        free(g_mtpndd_config.shared_bdd_vars);
        free(g_mtpndd_config.shared_bdd_not_vars);
        g_mtpndd_config.shared_bdd_vars = NULL;
        g_mtpndd_config.shared_bdd_not_vars = NULL;
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }

    // Create shared BDD variables for all fields
    for (uint32_t i = 0; i < total_bdd_vars; ++i) {
        g_mtpndd_config.shared_bdd_vars[i] = sylvan_ithvar(i);
        sylvan_protect(g_mtpndd_config.shared_bdd_vars + i);
        g_mtpndd_config.shared_bdd_not_vars[i] = sylvan_not(g_mtpndd_config.shared_bdd_vars[i]);
        sylvan_protect(g_mtpndd_config.shared_bdd_not_vars + i);
    }

    // Generate each pending field
    uint32_t current_var_offset = 0;
    for (uint32_t field_idx = 0; field_idx < g_mtpndd_config.pending_field_count; ++field_idx) {
        uint32_t bit_width = g_mtpndd_config.pending_field_bit_widths[field_idx];
        uint32_t field_id = field_idx + 1;

        // Allocate field info structure
        mtpndd_field_info_t *new_field = (mtpndd_field_info_t *)calloc(1, sizeof(mtpndd_field_info_t));
        if (!new_field) {
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }

        new_field->field_id = field_id;
        new_field->bit_width = bit_width;

        // Store start_var for this field
        new_field->start_var = current_var_offset;

        // Point to shared BDD variables at the correct offset (NOT owned by field)
        new_field->bdd_vars = g_mtpndd_config.shared_bdd_vars + current_var_offset;
        new_field->bdd_not_vars = g_mtpndd_config.shared_bdd_not_vars + current_var_offset;

        // Advance offset for next field
        current_var_offset += bit_width;

        // Allocate MTPNDD variable arrays (owned by field)
        new_field->mtpndd_vars = (mtpndd_t *)calloc(bit_width, sizeof(mtpndd_t));
        new_field->mtpndd_not_vars = (mtpndd_t *)calloc(bit_width, sizeof(mtpndd_t));
        if (!new_field->mtpndd_vars || !new_field->mtpndd_not_vars) {
            free(new_field->mtpndd_vars);
            free(new_field->mtpndd_not_vars);
            free(new_field);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }

        // Create MTPNDD variable nodes
        for (uint32_t i = 0; i < bit_width; ++i) {
            // var node
            mtpndd_edge_builder_t builder = {0};
            mtpndd_edge_builder_init(&builder, 1);
            if (!builder.edges) {
                free(new_field->mtpndd_vars);
                free(new_field->mtpndd_not_vars);
                free(new_field);
                MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
            }
            if (!mtpndd_edge_builder_push(&builder, MTPNDD_TRUE, sylvan_ref(new_field->bdd_vars[i]))) {
                mtpndd_edge_builder_destroy(&builder);
                free(new_field->mtpndd_vars);
                free(new_field->mtpndd_not_vars);
                free(new_field);
                return mtpndd_get_last_error().code;
            }
            mtpndd_t var_node = mtpndd_mk(new_field->field_id, &builder);
            mtpndd_edge_builder_destroy(&builder);
            if (!mtpndd_node_is_valid(var_node)) {
                free(new_field->mtpndd_vars);
                free(new_field->mtpndd_not_vars);
                free(new_field);
                return mtpndd_get_last_error().code;
            }
            mtpndd_protect(var_node);
            new_field->mtpndd_vars[i] = var_node;

            // not var node
            mtpndd_edge_builder_t not_builder = {0};
            mtpndd_edge_builder_init(&not_builder, 1);
            if (!not_builder.edges) {
                free(new_field->mtpndd_vars);
                free(new_field->mtpndd_not_vars);
                free(new_field);
                MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
            }
            if (!mtpndd_edge_builder_push(&not_builder, MTPNDD_TRUE, sylvan_ref(new_field->bdd_not_vars[i]))) {
                mtpndd_edge_builder_destroy(&not_builder);
                free(new_field->mtpndd_vars);
                free(new_field->mtpndd_not_vars);
                free(new_field);
                return mtpndd_get_last_error().code;
            }
            mtpndd_t not_node = mtpndd_mk(new_field->field_id, &not_builder);
            mtpndd_edge_builder_destroy(&not_builder);
            if (!mtpndd_node_is_valid(not_node)) {
                free(new_field->mtpndd_vars);
                free(new_field->mtpndd_not_vars);
                free(new_field);
                return mtpndd_get_last_error().code;
            }
            mtpndd_protect(not_node);
            new_field->mtpndd_not_vars[i] = not_node;
        }

        // Store field info
        g_mtpndd_config.field_info[field_id] = new_field;
        g_mtpndd_config.field_count++;
    }

    g_mtpndd_config.fields_generated = true;
    return MTPNDD_SUCCESS;
}

mtpndd_field_info_t *mtpndd_get_field_info(uint32_t field_id) {
    if (!mtpndd_is_initialized()) {
        return NULL;
    }
    if (field_id == 0 || field_id > g_mtpndd_config.field_count) {
        return NULL;
    }
    return g_mtpndd_config.field_info[field_id];
}

/********************************
 * Get node / variable helpers
 ********************************/
mtpndd_t mtpndd_get_var(uint32_t field, uint32_t var_index) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return MTPNDD_INVALID;
    }
    if (field == 0 || field > g_mtpndd_config.field_count) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return MTPNDD_INVALID;
    }
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    if (!field_info || var_index >= field_info->bit_width) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_PARAM);
        return MTPNDD_INVALID;
    }
    return field_info->mtpndd_vars[var_index];
}

mtpndd_t mtpndd_get_not_var(uint32_t field, uint32_t var_index) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return MTPNDD_INVALID;
    }
    if (field == 0 || field > g_mtpndd_config.field_count) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_FIELD);
        return MTPNDD_INVALID;
    }
    mtpndd_field_info_t *field_info = g_mtpndd_config.field_info[field];
    if (!field_info || var_index >= field_info->bit_width) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_INVALID_PARAM);
        return MTPNDD_INVALID;
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
mtpndd_stats_t *mtpndd_get_stats(void) {
    return &g_mtpndd_stats;
}

bool mtpndd_is_initialized(void) {
    return g_mtpndd_initialized;
}

/********************************
 * Stop-the-world GC (serial)
 ********************************/
size_t mtpndd_gc_collect(void) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
        return 0;
    }

    // Follow Sylvan's approach: clear computed caches before reclaiming/reusing nodes.
    mtpndd_op_cache_clear(g_mtpndd_config.and_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.or_cache);
    mtpndd_op_cache_clear(g_mtpndd_config.not_cache);

    mtpndd_gc_run_prehooks();

    size_t collected = mtpndd_nodetable_collect_garbage(&g_mtpndd_nodetable);

#ifdef ENABLE_RECORDING
    if (collected > 0) {
        MTPNDD_STAT_SET(nodes_collected_last, collected);
    }
    MTPNDD_STAT_ADD(gc_runs, 1);
#endif

    mtpndd_gc_run_posthooks();
    return collected;
}

/********************************
 * Init / Quit
 ********************************/
mtpndd_error_t mtpndd_init(mtpndd_pal_config_t *config) {
    if (mtpndd_is_initialized()) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_IS_INITIALIZED);
    }
    if (!config) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_NULL_POINTER);
    }
    mtpndd_clear_error();

    g_mtpndd_pal_config = *config;
    mtpndd_apply_pal_config_defaults();

    memset(&g_mtpndd_stats, 0, sizeof(g_mtpndd_stats));
#ifdef ENABLE_RECORDING
    g_mtpndd_stats.recording_enabled = 1;
#endif
    memset(&g_mtpndd_config, 0, sizeof(g_mtpndd_config));
    g_mtpndd_config.field_count = 0;
    g_mtpndd_config.field_capacity = DEFAULT_FIELD_CAPACITY;
    g_mtpndd_config.field_info =
            (mtpndd_field_info_t **)calloc(g_mtpndd_config.field_capacity, sizeof(mtpndd_field_info_t *));
    if (!g_mtpndd_config.field_info) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.field_info[0] = &MTPNDD_TERMINAL_FIELD;

    // Initialize two-phase field generation state
    g_mtpndd_config.pending_field_count = 0;
    g_mtpndd_config.pending_field_capacity = DEFAULT_FIELD_CAPACITY;
    g_mtpndd_config.pending_field_bit_widths =
            (uint32_t *)calloc(DEFAULT_FIELD_CAPACITY, sizeof(uint32_t));
    if (!g_mtpndd_config.pending_field_bit_widths) {
        free(g_mtpndd_config.field_info);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.max_bit_width = 0;
    g_mtpndd_config.total_bdd_vars = 0;
    g_mtpndd_config.fields_generated = false;
    g_mtpndd_config.shared_bdd_vars = NULL;
    g_mtpndd_config.shared_bdd_not_vars = NULL;

    // sylvan/lace
    if (!mtpndd_lace_init()) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_PARALLEL_INIT);
    }

    // Register GC hook with Sylvan
    sylvan_gc_hook_pregc(mtpndd_gc_before_sylvan);

    if (!mtpndd_op_cache_initialize(
                g_mtpndd_pal_config.mtpndd_op_cache_size ? g_mtpndd_pal_config.mtpndd_op_cache_size : config->op_cache_size,
                &g_mtpndd_config.and_cache,
                &g_mtpndd_config.or_cache,
                &g_mtpndd_config.not_cache)) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_INITIALIZE_FAILED);
    }

    // nodetable + edge pool (min/max sizing similar to Sylvan: growth may double up to max)
    size_t node_cap_min = g_mtpndd_pal_config.mtpndd_nodetable_size ? g_mtpndd_pal_config.mtpndd_nodetable_size : (1 << 14);
    size_t node_cap_max = g_mtpndd_pal_config.mtpndd_nodetable_max_size ? g_mtpndd_pal_config.mtpndd_nodetable_max_size : node_cap_min;
    if (node_cap_max < node_cap_min) node_cap_max = node_cap_min;
    // Nodetable hash slot uses Sylvan-style 40-bit idx packing.
    const size_t nodetable_max_total = (size_t)MTPNDD_NODETABLE_SLOT_MASK_INDEX + 1; // includes terminals 0/1
    if (node_cap_min + 2 > nodetable_max_total || node_cap_max + 2 > nodetable_max_total) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_CAPACITY_EXCEEDED);
    }

    size_t hash_cap_min = g_mtpndd_pal_config.nodetable_bucket_count ? g_mtpndd_pal_config.nodetable_bucket_count : (node_cap_min * 2);
    size_t hash_cap_max = g_mtpndd_pal_config.nodetable_bucket_max_count ? g_mtpndd_pal_config.nodetable_bucket_max_count : hash_cap_min;
    if (hash_cap_max < hash_cap_min) hash_cap_max = hash_cap_min;

    size_t edge_cap_hint = (size_t)g_mtpndd_pal_config.edge_entry_slab_capacity * 8;
    mtpndd_nodetable_init(&g_mtpndd_nodetable, node_cap_min + 2, node_cap_max + 2, hash_cap_min, hash_cap_max, edge_cap_hint);
    if (!g_mtpndd_nodetable.data || !g_mtpndd_nodetable.hash) {
        MTPNDD_RETURN_ERROR(mtpndd_get_last_error().code ? mtpndd_get_last_error().code : MTPNDD_ERROR_OUT_OF_MEMORY);
    }

    g_mtpndd_initialized = true;
    return MTPNDD_SUCCESS;
}

mtpndd_error_t mtpndd_quit(void) {
    if (!mtpndd_is_initialized()) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_NOT_INITIALIZED);
    }

    for (uint32_t i = 1; i <= g_mtpndd_config.field_count; ++i) {
        mtpndd_field_info_t *field = g_mtpndd_config.field_info[i];
        if (!field) continue;
        mtpndd_field_info_teardown(field);
        free(field);
        g_mtpndd_config.field_info[i] = NULL;
    }

    free(g_mtpndd_config.field_info);
    g_mtpndd_config.field_info = NULL;
    g_mtpndd_config.field_count = 0;
    g_mtpndd_config.field_capacity = 0;

    // Clean up shared BDD variable arrays (must be after field teardown)
    if (g_mtpndd_config.shared_bdd_vars) {
        for (uint32_t i = 0; i < g_mtpndd_config.total_bdd_vars; ++i) {
            sylvan_unprotect(g_mtpndd_config.shared_bdd_vars + i);
        }
        free(g_mtpndd_config.shared_bdd_vars);
        g_mtpndd_config.shared_bdd_vars = NULL;
    }
    if (g_mtpndd_config.shared_bdd_not_vars) {
        for (uint32_t i = 0; i < g_mtpndd_config.total_bdd_vars; ++i) {
            sylvan_unprotect(g_mtpndd_config.shared_bdd_not_vars + i);
        }
        free(g_mtpndd_config.shared_bdd_not_vars);
        g_mtpndd_config.shared_bdd_not_vars = NULL;
    }

    // Clean up pending field tracking
    free(g_mtpndd_config.pending_field_bit_widths);
    g_mtpndd_config.pending_field_bit_widths = NULL;

    mtpndd_op_cache_destroy();
    mtpndd_nodetable_destroy(&g_mtpndd_nodetable);

    lace_stop();
    sylvan_quit();

    memset(&g_mtpndd_pal_config, 0, sizeof(g_mtpndd_pal_config));
    memset(&g_mtpndd_stats, 0, sizeof(g_mtpndd_stats));
    memset(&g_mtpndd_config, 0, sizeof(g_mtpndd_config));
    mtpndd_clear_error();
    g_mtpndd_initialized = false;

    return MTPNDD_SUCCESS;
}

