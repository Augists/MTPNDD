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
    .gcProtect = NULL,
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
    if (g_mtpndd_pal_config.quick_growth_threshold <= 0.0) {
        g_mtpndd_pal_config.quick_growth_threshold = DEFAULT_QUICK_GROWTH_THRESHOLD;
    }
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
    return true;
}

/********************************
 * Field management
 ********************************/
static void mtpndd_field_info_teardown(mtpndd_field_info_t *field) {
    if (!field) {
        return;
    }
    uint32_t width = field->bit_width;
    free(field->mtpndd_vars);
    free(field->mtpndd_not_vars);
    field->mtpndd_vars = NULL;
    field->mtpndd_not_vars = NULL;

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

mtpndd_error_t mtpndd_declare_field(uint32_t bit_width) {
    MTPNDD_CHECK_INIT();
    MTPNDD_CHECK_PARAM(bit_width > 0, MTPNDD_ERROR_INVALID_PARAM);

    if (g_mtpndd_config.field_count + 1 >= g_mtpndd_config.field_capacity) {
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

    g_mtpndd_config.field_count++;
    mtpndd_field_info_t *new_field = (mtpndd_field_info_t *)calloc(1, sizeof(mtpndd_field_info_t));
    if (!new_field) {
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }

    new_field->field_id = g_mtpndd_config.field_count;
    new_field->bit_width = bit_width;

    mtpndd_field_info_t *prev_field = g_mtpndd_config.field_info[g_mtpndd_config.field_count - 1];
    new_field->start_var = (g_mtpndd_config.field_count == 1) ? 0 : (prev_field->start_var + prev_field->bit_width);

    new_field->bdd_vars = (mtpndd_bdd_t *)calloc(bit_width, sizeof(mtpndd_bdd_t));
    new_field->bdd_not_vars = (mtpndd_bdd_t *)calloc(bit_width, sizeof(mtpndd_bdd_t));
    if (!new_field->bdd_vars || !new_field->bdd_not_vars) {
        mtpndd_field_info_teardown(new_field);
        free(new_field);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    for (uint32_t i = 0; i < bit_width; i++) {
        new_field->bdd_vars[i] = sylvan_ithvar(new_field->start_var + i);
        sylvan_protect(new_field->bdd_vars + i);
        new_field->bdd_not_vars[i] = sylvan_not(new_field->bdd_vars[i]);
        sylvan_protect(new_field->bdd_not_vars + i);
    }

    new_field->mtpndd_vars = (mtpndd_t *)calloc(bit_width, sizeof(mtpndd_t));
    new_field->mtpndd_not_vars = (mtpndd_t *)calloc(bit_width, sizeof(mtpndd_t));
    if (!new_field->mtpndd_vars || !new_field->mtpndd_not_vars) {
        mtpndd_field_info_teardown(new_field);
        free(new_field);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }

    for (uint32_t i = 0; i < bit_width; i++) {
        // var node
        mtpndd_edge_builder_t builder = {0};
        mtpndd_edge_builder_init(&builder, 1);
        if (!builder.edges) {
            mtpndd_field_info_teardown(new_field);
            free(new_field);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        if (!mtpndd_edge_builder_push(&builder, MTPNDD_TRUE, sylvan_ref(new_field->bdd_vars[i]))) {
            mtpndd_edge_builder_destroy(&builder);
            mtpndd_field_info_teardown(new_field);
            free(new_field);
            return mtpndd_get_last_error().code;
        }
        mtpndd_t var_node = mtpndd_mk(new_field->field_id, &builder);
        mtpndd_edge_builder_destroy(&builder);
        if (!mtpndd_node_is_valid(var_node)) {
            mtpndd_field_info_teardown(new_field);
            free(new_field);
            return mtpndd_get_last_error().code;
        }
        mtpndd_protect(var_node);
        new_field->mtpndd_vars[i] = var_node;

        // not var node
        mtpndd_edge_builder_t not_builder = {0};
        mtpndd_edge_builder_init(&not_builder, 1);
        if (!not_builder.edges) {
            mtpndd_field_info_teardown(new_field);
            free(new_field);
            MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        }
        if (!mtpndd_edge_builder_push(&not_builder, MTPNDD_TRUE, sylvan_ref(new_field->bdd_not_vars[i]))) {
            mtpndd_edge_builder_destroy(&not_builder);
            mtpndd_field_info_teardown(new_field);
            free(new_field);
            return mtpndd_get_last_error().code;
        }
        mtpndd_t not_node = mtpndd_mk(new_field->field_id, &not_builder);
        mtpndd_edge_builder_destroy(&not_builder);
        if (!mtpndd_node_is_valid(not_node)) {
            mtpndd_field_info_teardown(new_field);
            free(new_field);
            return mtpndd_get_last_error().code;
        }
        mtpndd_protect(not_node);
        new_field->mtpndd_not_vars[i] = not_node;
    }

    g_mtpndd_config.field_info[g_mtpndd_config.field_count] = new_field;
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
 * gcProtect implementation (serial)
 ********************************/
static size_t mtpndd_gc_protect_next_pow2(size_t v) {
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    if (sizeof(size_t) == 8) v |= v >> 32;
    return v + 1;
}

static bool mtpndd_gc_protect_grow(mtpndd_gc_protect_t *set) {
    size_t new_capacity = set->capacity ? set->capacity * 2 : 1024;
    new_capacity = mtpndd_gc_protect_next_pow2(new_capacity);
    mtpndd_t *new_slots = (mtpndd_t *)calloc(new_capacity, sizeof(mtpndd_t));
    if (!new_slots) {
        MTPNDD_SET_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
        return false;
    }
    size_t new_mask = new_capacity - 1;
    for (size_t i = 0; i < set->capacity; ++i) {
        mtpndd_t stored = set->slots[i];
        if (stored == 0) continue;
        size_t h = mtpndd_hash_u64(stored) & new_mask;
        while (new_slots[h] != 0) {
            h = (h + 1) & new_mask;
        }
        new_slots[h] = stored;
    }
    free(set->slots);
    set->slots = new_slots;
    set->capacity = new_capacity;
    set->mask = new_mask;
    return true;
}

void mtpndd_gc_protect_clear(void) {
    if (!g_mtpndd_config.gcProtect || !g_mtpndd_config.gcProtect->slots) {
        return;
    }
    memset(g_mtpndd_config.gcProtect->slots, 0, g_mtpndd_config.gcProtect->capacity * sizeof(mtpndd_t));
    g_mtpndd_config.gcProtect->count = 0;
}

void mtpndd_gc_protect_add(mtpndd_t node) {
    if (!mtpndd_is_initialized() || node < 2) {
        return;
    }
    mtpndd_gc_protect_t *set = g_mtpndd_config.gcProtect;
    if (!set || !set->slots || set->capacity == 0) {
        return;
    }
    if ((set->count + 1) * 10 >= set->capacity * 7) {
        (void)mtpndd_gc_protect_grow(set);
    }
    mtpndd_t stored = node + 1; // store+1, 0 means empty
    size_t h = mtpndd_hash_u64(stored) & set->mask;
    while (set->slots[h] != 0) {
        if (set->slots[h] == stored) {
            return;
        }
        h = (h + 1) & set->mask;
    }
    set->slots[h] = stored;
    set->count++;
}

bool mtpndd_gc_protect_contains(mtpndd_t node) {
    if (!mtpndd_is_initialized() || node < 2) {
        return false;
    }
    mtpndd_gc_protect_t *set = g_mtpndd_config.gcProtect;
    if (!set || !set->slots || set->capacity == 0) {
        return false;
    }
    mtpndd_t stored = node + 1;
    size_t h = mtpndd_hash_u64(stored) & set->mask;
    while (set->slots[h] != 0) {
        if (set->slots[h] == stored) {
            return true;
        }
        h = (h + 1) & set->mask;
    }
    return false;
}

void mtpndd_gc_protect_remove(mtpndd_t node) {
    if (!mtpndd_is_initialized() || node < 2) {
        return;
    }
    mtpndd_gc_protect_t *set = g_mtpndd_config.gcProtect;
    if (!set || !set->slots || set->capacity == 0) {
        return;
    }
    mtpndd_t stored = node + 1;
    size_t idx = mtpndd_hash_u64(stored) & set->mask;
    while (set->slots[idx] != 0) {
        if (set->slots[idx] == stored) {
            set->slots[idx] = 0;
            if (set->count > 0) set->count--;
            // backshift deletion
            size_t hole = idx;
            size_t scan = (hole + 1) & set->mask;
            while (set->slots[scan] != 0) {
                mtpndd_t value = set->slots[scan];
                size_t ideal = mtpndd_hash_u64(value) & set->mask;
                size_t dist_ideal = (scan + set->capacity - ideal) & set->mask;
                size_t dist_hole = (scan + set->capacity - hole) & set->mask;
                if (dist_ideal >= dist_hole) {
                    set->slots[hole] = value;
                    set->slots[scan] = 0;
                    hole = scan;
                }
                scan = (scan + 1) & set->mask;
            }
            return;
        }
        idx = (idx + 1) & set->mask;
    }
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

    // sylvan/lace
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

    // nodetable + edge pool
    size_t node_cap_hint = g_mtpndd_pal_config.mtpndd_nodetable_size ? g_mtpndd_pal_config.mtpndd_nodetable_size : (1 << 14);
    size_t hash_cap_hint = g_mtpndd_pal_config.nodetable_bucket_count ? g_mtpndd_pal_config.nodetable_bucket_count : (node_cap_hint * 2);
    size_t edge_cap_hint = (size_t)g_mtpndd_pal_config.edge_entry_slab_capacity * 8;
    mtpndd_nodetable_init(&g_mtpndd_nodetable, node_cap_hint + 2, hash_cap_hint, edge_cap_hint);
    if (!g_mtpndd_nodetable.data || !g_mtpndd_nodetable.hash) {
        MTPNDD_RETURN_ERROR(mtpndd_get_last_error().code ? mtpndd_get_last_error().code : MTPNDD_ERROR_OUT_OF_MEMORY);
    }

    // gcProtect
    g_mtpndd_config.gcProtect = (mtpndd_gc_protect_t *)calloc(1, sizeof(mtpndd_gc_protect_t));
    if (!g_mtpndd_config.gcProtect) {
        mtpndd_nodetable_destroy(&g_mtpndd_nodetable);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    size_t gc_cap = g_mtpndd_pal_config.gc_bucket_count ? g_mtpndd_pal_config.gc_bucket_count : MTPNDD_DEFAULT_GC_BUCKET_COUNT;
    gc_cap = mtpndd_gc_protect_next_pow2(gc_cap);
    g_mtpndd_config.gcProtect->slots = (mtpndd_t *)calloc(gc_cap, sizeof(mtpndd_t));
    if (!g_mtpndd_config.gcProtect->slots) {
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
        mtpndd_nodetable_destroy(&g_mtpndd_nodetable);
        MTPNDD_RETURN_ERROR(MTPNDD_ERROR_OUT_OF_MEMORY);
    }
    g_mtpndd_config.gcProtect->capacity = gc_cap;
    g_mtpndd_config.gcProtect->mask = gc_cap - 1;
    g_mtpndd_config.gcProtect->count = 0;

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

    if (g_mtpndd_config.gcProtect) {
        free(g_mtpndd_config.gcProtect->slots);
        free(g_mtpndd_config.gcProtect);
        g_mtpndd_config.gcProtect = NULL;
    }

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
